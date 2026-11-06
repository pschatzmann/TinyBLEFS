#pragma once
// RPC engine of the client: synchronous request / response with timeout and
// retry (for idempotent operations).

#include "Logger.h"
#include "Protocol.h"
#include "Session.h"
#include "Transport.h"

namespace tinyblefs {

struct ServerInfo {
  uint8_t version = 0;
  uint8_t maxHandles = 0;
  bool readOnly = false;
  std::string name;
};

class FileClient {
 public:
  /// @param transport the BLE adapter (connected to the server); the client
  ///        joins a session of the server by itself with the first request
  /// @param timeoutMs how long to wait for an answer
  explicit FileClient(Transport& transport, uint32_t timeoutMs = 3000)
      : link_(transport, timeoutMs), t_(link_), timeout_(timeoutMs) {}

  /// Joins the session of the server now (optional: the first request does it)
  bool join() { return link_.join(); }
  /// Keeps the session alive while the application is idle: the server closes
  /// sessions without packets for 60 s (and with them the open files). Call it
  /// at least once in that time. If the session was closed, the next request
  /// fails and the one after joins again (files that were open are lost).
  void keepAlive() { link_.keepAlive(); }
  /// Id of the session (0 before the first request)
  uint8_t sessionId() const { return link_.id(); }

  /// Called while waiting for a response (e.g. to run a BLE event loop or, in
  /// tests, to poll a server in the same thread)
  void setIdleCallback(void (*cb)(void*), void* ctx) {
    idle_ = cb;
    idle_ctx_ = ctx;
    link_.setIdleCallback(cb, ctx);  // also while joining the session
  }
  void setTimeout(uint32_t ms) {
    timeout_ = ms;
    link_.setTimeout(ms);
  }
  /// Max bytes transferred per READ / WRITE request (read-ahead size)
  void setChunkSize(uint16_t n) { chunk_ = n; }
  uint16_t chunkSize() const { return chunk_; }
  bool connected() const { return t_.connected(); }
  /// Changes when the server lost the state of this client (see Transport)
  uint32_t generation() const { return t_.generation(); }

  // ---- high level operations -------------------------------------------
  Status info(ServerInfo& si) {
    std::vector<uint8_t> out;
    Status st = request(Op::Info, {}, &out, 1);
    if (st != Status::Ok) return st;
    if (out.size() < 3) return malformed(__func__);
    si.version = out[0];
    si.maxHandles = out[1];
    si.readOnly = out[2] != 0;
    si.name.assign((const char*)out.data() + 3, out.size() - 3);
    return Status::Ok;
  }

  Status open(const std::string& path, uint8_t flags, uint8_t& handle,
              uint32_t& size) {
    std::vector<uint8_t> in{flags}, out;
    putStr(in, path);
    Status st = request(Op::Open, in, &out);
    if (st != Status::Ok) return st;
    if (out.size() < 5) return malformed(__func__);
    handle = out[0];
    size = get32(&out[1]);
    return Status::Ok;
  }

  Status close(uint8_t handle) { return request(Op::Close, {handle}); }

  Status read(uint8_t handle, uint32_t offset, uint16_t len,
              std::vector<uint8_t>& data) {
    std::vector<uint8_t> in{handle};
    put32(in, offset);
    put16(in, len);
    return request(Op::Read, in, &data, 1);
  }

  Status write(uint8_t handle, uint32_t offset, const uint8_t* data,
               size_t len, uint16_t& written) {
    std::vector<uint8_t> in{handle}, out;
    put32(in, offset);
    in.insert(in.end(), data, data + len);
    Status st = request(Op::Write, in, &out, 1);
    if (st != Status::Ok) return st;
    if (out.size() < 2) return malformed(__func__);
    written = get16(&out[0]);
    return Status::Ok;
  }

  Status stat(const std::string& path, bool& isDir, uint32_t& size) {
    std::vector<uint8_t> in, out;
    putStr(in, path);
    Status st = request(Op::Stat, in, &out, 1);
    if (st != Status::Ok) return st;
    if (out.size() < 5) return malformed(__func__);
    isDir = out[0] != 0;
    size = get32(&out[1]);
    return Status::Ok;
  }

  /// Returns Status::EndOfList after the last entry
  Status list(const std::string& path, uint16_t index, bool& isDir,
              uint32_t& size, std::string& name) {
    std::vector<uint8_t> in, out;
    put16(in, index);
    putStr(in, path);
    Status st = request(Op::List, in, &out, 1);
    if (st != Status::Ok) return st;
    if (out.size() < 5) return malformed(__func__);
    isDir = out[0] != 0;
    size = get32(&out[1]);
    name.assign((const char*)out.data() + 5, out.size() - 5);
    return Status::Ok;
  }

  /// Lists the entries from `index` on, as many as fit into maxBytes of answer
  /// (about 20 entries per 400 bytes). `end` is true when the last entry of the
  /// directory is among them. A server without batched listing is detected and
  /// then asked entry by entry.
  Status listBatch(const std::string& path, uint16_t index, uint16_t maxBytes,
                   std::vector<DirEntry>& entries, bool& end) {
    entries.clear();
    end = false;
    if (batch_list_) {
      std::vector<uint8_t> in, out;
      put16(in, index);
      put16(in, maxBytes);
      putStr(in, path);
      Status st = request(Op::ListBatch, in, &out, 1);
      if (st == Status::Ok) {
        if (out.empty()) return malformed(__func__);
        size_t pos = 1;
        while (pos < out.size()) {
          if (pos + 6 > out.size() || pos + 6 + out[pos + 5] > out.size()) return malformed(__func__);
          DirEntry e;
          e.isDir = out[pos] != 0;
          e.size = get32(&out[pos + 1]);
          e.name.assign((const char*)&out[pos + 6], out[pos + 5]);
          pos += 6 + out[pos + 5];
          entries.push_back(std::move(e));
        }
        end = (out[0] & kListEnd) != 0;
        return Status::Ok;
      }
      if (st != Status::BadRequest) return st;
      // An older server answers "bad request" to the unknown operation (a bad
      // path does too): ask entry by entry, and remember it if that works.
      Status fb = listEntryByEntry(path, index, maxBytes, entries, end);
      if (fb == Status::Ok) {
        TINYBLEFS_LOGI("client", "server has no batched listing: listing entry by entry");
        batch_list_ = false;
      }
      return fb;
    }
    return listEntryByEntry(path, index, maxBytes, entries, end);
  }
  /// Switch off the batched listing (for servers of the first versions)
  void setBatchList(bool on) { batch_list_ = on; }
  /// Number of requests sent so far (every request counts once, retries not)
  uint32_t requestCount() const { return request_count_; }

  Status remove(const std::string& path) { return pathOp(Op::Remove, path); }
  Status mkdir(const std::string& path) { return pathOp(Op::Mkdir, path); }
  Status rmdir(const std::string& path) { return pathOp(Op::Rmdir, path); }

  Status rename(const std::string& from, const std::string& to) {
    std::vector<uint8_t> in;
    put16(in, (uint16_t)from.size());
    putStr(in, from);
    putStr(in, to);
    return request(Op::Rename, in);
  }

  // ---- low level -----------------------------------------------------
  /// Sends a request and waits for the response. Only operations that are
  /// safe to repeat should use retries > 0.
  Status request(Op op, const std::vector<uint8_t>& payload,
                 std::vector<uint8_t>* response = nullptr, int retries = 0) {
    request_count_++;
    for (int attempt = 0; attempt <= retries; attempt++) {
      Message req;
      req.op = (uint8_t)op;
      req.reqId = ++req_id_;
      req.payload = payload;
      if (!sendMessage(t_, req)) {
        TINYBLEFS_LOGE("client", "cannot send %s request", opName(req.op));
        if (attempt < retries && t_.connected()) continue;  // transient: try again
        return Status::SendFailed;
      }
      uint32_t start = nowMs();
      while (nowMs() - start < timeout_) {
        if (idle_) idle_(idle_ctx_);
        t_.poll();
        Message rsp;
        bool got = false;
        while (rx_.poll(t_, rsp)) {
          got = true;
          if (rsp.reqId == req.reqId && rsp.op == (req.op | kResponseFlag)) {
            Status st = (Status)rsp.status;
            if (st != Status::Ok && st != Status::NotFound &&
                st != Status::EndOfList && st != Status::IsDirectory &&
                st != Status::Busy)
              TINYBLEFS_LOGW("client", "%s request failed: %s", opName(req.op), statusName(st));
            if (response) *response = std::move(rsp.payload);
            return st;
          }
          TINYBLEFS_LOGD("client", "ignoring stale response %u", (unsigned)rsp.reqId);
        }
        if (!got) yieldCpu();
        if (!t_.connected()) {
          TINYBLEFS_LOGE("client", "link lost while waiting for %s response", opName(req.op));
          return Status::SendFailed;
        }
      }
      TINYBLEFS_LOGW("client", "%s request timed out after %u ms (attempt %d of %d)",
                  opName(req.op), (unsigned)timeout_, attempt + 1, retries + 1);
    }
    return Status::Timeout;
  }

 private:
  Status listEntryByEntry(const std::string& path, uint16_t index, uint16_t maxBytes,
                          std::vector<DirEntry>& entries, bool& end) {
    size_t used = 1;
    while (true) {
      DirEntry e;
      Status st = list(path, (uint16_t)(index + entries.size()), e.isDir, e.size, e.name);
      if (st == Status::EndOfList) {
        end = true;
        return Status::Ok;
      }
      if (st != Status::Ok) return st;
      used += 6 + e.name.size();
      entries.push_back(std::move(e));
      if (used >= maxBytes) return Status::Ok;
    }
  }
  Status malformed(const char* what) {
    TINYBLEFS_LOGW("client", "%s: malformed response from server", what);
    return Status::BadRequest;
  }
  Status pathOp(Op op, const std::string& path) {
    std::vector<uint8_t> in;
    putStr(in, path);
    return request(op, in);
  }

  detail::SessionLink link_;  // must be declared before t_
  Transport& t_;
  Receiver rx_;
  uint32_t timeout_;
  uint16_t chunk_ = 400;
  bool batch_list_ = true;
  uint32_t request_count_ = 0;
  uint8_t req_id_ = 0;
  void (*idle_)(void*) = nullptr;
  void* idle_ctx_ = nullptr;
};

}  // namespace tinyblefs
