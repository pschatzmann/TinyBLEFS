#pragma once
// Wire protocol of TinyBLEFS: opcodes, status codes and message framing.
//
// Message frame (little endian):
//   [op:1][reqId:1][status:1][payloadLen:2][payload...]
// Requests have status 0; responses echo reqId and set op |= kResponseFlag.
// A message may be split over several transport packets (MTU sized); BLE
// delivers packets reliably and in order so the length field is all that is
// needed to reassemble.
//
// All file access is offset based (stateless cursor), so a retried READ or
// WRITE is idempotent.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace tinyblefs {

static constexpr uint8_t kProtocolVersion = 1;
static constexpr size_t kHeaderSize = 5;
static constexpr uint8_t kResponseFlag = 0x80;
static constexpr size_t kMaxPayload = 0xFFFF;

// GATT layout: one service, RX (client -> server, write without response),
// TX (server -> client, notify).
static constexpr const char* kServiceUUID = "6e1f0001-7a3b-4c5d-8e9f-0a1b2c3d4e5f";
static constexpr const char* kRxUUID = "6e1f0002-7a3b-4c5d-8e9f-0a1b2c3d4e5f";
static constexpr const char* kTxUUID = "6e1f0003-7a3b-4c5d-8e9f-0a1b2c3d4e5f";

enum class Op : uint8_t {
  Info = 1,    // -> [version][maxHandles][readOnly][name...]
  Open = 2,    // [flags][path...] -> [handle][size:4]
  Close = 3,   // [handle]
  Read = 4,    // [handle][offset:4][len:2] -> data
  Write = 5,   // [handle][offset:4][data...] -> [written:2]
  Stat = 6,    // [path...] -> [isDir][size:4]
  List = 7,    // [index:2][path...] -> [isDir][size:4][name...]
  Remove = 8,  // [path...]
  Mkdir = 9,   // [path...]
  Rmdir = 10,  // [path...]
  Rename = 11, // [fromLen:2][from][to]
  // [index:2][maxBytes:2][path...] -> [flags:1] then entries
  // [isDir:1][size:4][nameLen:1][name...]; flags bit 0: last entry included
  ListBatch = 12
};

/// Flags of the ListBatch answer
enum ListFlags : uint8_t { kListEnd = 1 };

/// One directory entry (RemoteFS::list(), FileClient::listBatch())
struct DirEntry {
  std::string name;
  bool isDir = false;
  uint32_t size = 0;
};

enum class Status : uint8_t {
  Ok = 0,
  Error = 1,
  NotFound = 2,
  BadRequest = 3,
  NoHandle = 4,
  Denied = 5,
  Exists = 6,
  IoError = 7,
  EndOfList = 8,
  IsDirectory = 9,
  Busy = 10,  // locked: the file is open for writing elsewhere
  // local only (never sent on the wire)
  SendFailed = 0xFD,
  Timeout = 0xFE
};

/// Readable name of a status code (for log messages)
inline const char* statusName(Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::Error: return "error";
    case Status::NotFound: return "not found";
    case Status::BadRequest: return "bad request";
    case Status::NoHandle: return "no file handle";
    case Status::Denied: return "denied";
    case Status::Exists: return "already exists";
    case Status::IoError: return "i/o error";
    case Status::EndOfList: return "end of list";
    case Status::IsDirectory: return "is a directory";
    case Status::Busy: return "busy";
    case Status::SendFailed: return "send failed";
    case Status::Timeout: return "timeout";
  }
  return "unknown status";
}

/// Readable name of an operation (for log messages)
inline const char* opName(uint8_t op) {
  switch ((Op)(op & ~kResponseFlag)) {
    case Op::Info: return "info";
    case Op::Open: return "open";
    case Op::Close: return "close";
    case Op::Read: return "read";
    case Op::Write: return "write";
    case Op::Stat: return "stat";
    case Op::List:
    case Op::ListBatch: return "list";
    case Op::Remove: return "remove";
    case Op::Mkdir: return "mkdir";
    case Op::Rmdir: return "rmdir";
    case Op::Rename: return "rename";
  }
  return "unknown op";
}

enum OpenFlags : uint8_t {
  kRead = 1,
  kWrite = 2,
  kAppend = 4,  // client side: start at end of file (server treats as write)
  kCreate = 8,  // create when missing
  kTrunc = 16   // truncate (implies create)
};

struct Message {
  uint8_t op = 0;
  uint8_t reqId = 0;
  uint8_t status = 0;
  std::vector<uint8_t> payload;
};

inline void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(x & 0xFF);
  v.push_back(x >> 8);
}
inline void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xFF);
}
inline void putStr(std::vector<uint8_t>& v, const std::string& s) {
  v.insert(v.end(), s.begin(), s.end());
}
inline uint16_t get16(const uint8_t* p) { return p[0] | (p[1] << 8); }
inline uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

/// Serialises a message (payload must not exceed kMaxPayload)
inline std::vector<uint8_t> encode(const Message& m) {
  std::vector<uint8_t> out;
  out.reserve(kHeaderSize + m.payload.size());
  out.push_back(m.op);
  out.push_back(m.reqId);
  out.push_back(m.status);
  put16(out, (uint16_t)m.payload.size());
  out.insert(out.end(), m.payload.begin(), m.payload.end());
  return out;
}

/// Collects transport packets and extracts complete messages
class Reassembler {
 public:
  void feed(const uint8_t* data, size_t len) {
    buf_.insert(buf_.end(), data, data + len);
  }
  bool next(Message& m) {
    if (buf_.size() < kHeaderSize) return false;
    size_t len = get16(&buf_[3]);
    if (buf_.size() < kHeaderSize + len) return false;
    m.op = buf_[0];
    m.reqId = buf_[1];
    m.status = buf_[2];
    m.payload.assign(buf_.begin() + kHeaderSize,
                     buf_.begin() + kHeaderSize + len);
    buf_.erase(buf_.begin(), buf_.begin() + kHeaderSize + len);
    return true;
  }
  void reset() { buf_.clear(); }

 private:
  std::vector<uint8_t> buf_;
};

}  // namespace tinyblefs
