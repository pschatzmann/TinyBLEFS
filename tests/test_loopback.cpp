// Host test of the whole library over loopback transports: protocol, server,
// client, mounts, copy, locking, batched listing, logger, and several clients
// (sessions) on one server.
// Build: g++ -std=c++17 -pthread -I../src test_loopback.cpp -o test_loopback
// Run:   ./test_loopback <empty scratch dir>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/Loopback.h"
#include "TinyBLEFS/backends/StdFilesystem.h"

using namespace tinyblefs;
namespace fs = std::filesystem;

static std::atomic<int> failures{0};
#define CHECK(c)                                                  \
  do {                                                            \
    if (!(c)) {                                                   \
      printf("FAIL line %d: %s\n", __LINE__, #c);                 \
      failures++;                                                 \
    }                                                             \
  } while (0)

struct CapturePrint : public LogPrint {
  std::string text;
  size_t write(const uint8_t* d, size_t n) override {
    text.append((const char*)d, n);
    return n;
  }
};

static void idle(void* ctx) { ((FileServer*)ctx)->poll(); }

static std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

/// Several clients on one server over a shared link that broadcasts every reply
/// to every client and does not tell the server who wrote (ArduinoBLE / Pico W
/// behaviour). The clients run in their own threads.
static void testSeveralClients(const fs::path& base) {
  fs::create_directories(base / "root");
  LogLevel savedLevel = TinyBLEFSLogger.level();
  TinyBLEFSLogger.setLevel(LogLevel::None);

  const size_t kClients = 5;  // 0..2 work, 3 repeats hello, 4 finds the server full
  LoopbackBroadcastHub hub(kClients, 20);
  StdFilesystemBackend backend;
  FileServer server(backend, hub.server(), "multi", (base / "root").c_str(), 4, 4);

  // the server runs in its own thread
  std::mutex srvMutex;
  std::atomic<bool> running{true};
  std::thread srvThread([&] {
    while (running) {
      {
        std::lock_guard<std::mutex> g(srvMutex);
        server.poll();
      }
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });
  auto clientCount = [&] {
    std::lock_guard<std::mutex> g(srvMutex);
    return server.sessionCount();
  };

  // ---- repeated hello (retransmission) keeps one session -------------------
  {
    Transport& raw = hub.client(3);
    std::vector<uint8_t> hello{0, kSessionHello, 0x11, 0x22, 0x33, 0x44};
    CHECK(raw.send(hello.data(), hello.size()));
    CHECK(raw.send(hello.data(), hello.size()));
    std::vector<uint8_t> sids;
    uint8_t buf[64];
    uint32_t start = nowMs();
    while (sids.size() < 2 && nowMs() - start < 2000) {
      size_t n = raw.receive(buf, sizeof(buf));
      if (n >= 7 && buf[0] == 0 && buf[1] == kSessionWelcome) sids.push_back(buf[6]);
      else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(sids.size() == 2 && sids[0] == sids[1] && sids[0] != 0);
    CHECK(clientCount() == 1);
  }

  // ---- three clients work concurrently --------------------------------------
  std::vector<std::string> content(3);
  for (int i = 0; i < 3; i++) {
    content[i].resize(60000 + i * 1000);
    for (size_t k = 0; k < content[i].size(); k++)
      content[i][k] = (char)('a' + (k * (i + 3) + i) % 26);
  }
  std::vector<std::unique_ptr<FileClient>> clients;
  std::vector<std::unique_ptr<Mounts>> mounts;
  for (int i = 0; i < 3; i++) {
    clients.emplace_back(new FileClient(hub.client(i), 5000));
    mounts.emplace_back(new Mounts());
  }
  std::vector<std::thread> threads;
  std::atomic<int> okCount{0};
  for (int i = 0; i < 3; i++) {
    threads.emplace_back([&, i] {
      bool ok = mounts[i]->mount(*clients[i]) == "multi";
      std::string path = "/multi/c" + std::to_string(i) + ".bin";
      {
        RemoteFile f = mounts[i]->open(path.c_str(), "w");
        ok = ok && f && f.write((const uint8_t*)content[i].data(), content[i].size()) == content[i].size();
      }
      // read back what this client wrote and verify
      RemoteFile f = mounts[i]->open(path.c_str());
      std::string back(content[i].size(), 0);
      ok = ok && f && f.read((uint8_t*)&back[0], back.size()) == back.size() && back == content[i];
      if (ok) okCount++;
    });
  }
  for (auto& t : threads) t.join();
  CHECK(okCount == 3);
  for (int i = 0; i < 3; i++)
    CHECK(slurp(base / "root" / ("c" + std::to_string(i) + ".bin")) == content[i]);
  CHECK(clientCount() == 4);
  CHECK(clients[0]->sessionId() != clients[1]->sessionId() && clients[1]->sessionId() != clients[2]->sessionId() &&
        clients[0]->sessionId() != clients[2]->sessionId());

  // a client reads what another one wrote
  {
    RemoteFile f = mounts[1]->open("/multi/c0.bin");
    std::string back(content[0].size(), 0);
    CHECK(f && f.read((uint8_t*)&back[0], back.size()) == back.size() && back == content[0]);
  }

  // ---- no free session ----------------------------------------------------
  {
    FileClient extra(hub.client(4), 1000);
    CHECK(!extra.join());
  }

  // ---- open files are private to a session ---------------------------------
  {
    uint8_t h = 0;
    uint32_t size = 0;
    CHECK(clients[0]->open("/c0.bin", kRead, h, size) == Status::Ok);
    std::vector<uint8_t> data;
    CHECK(clients[0]->read(h, 0, 10, data) == Status::Ok && data.size() == 10);
    // client 1 has no open file: the same handle number is invalid for it
    CHECK(clients[1]->read(h, 0, 10, data) == Status::NoHandle);
    CHECK(clients[0]->close(h) == Status::Ok);
  }

  // ---- file locks work across sessions -----------------------------------
  {
    RemoteFile w = mounts[0]->open("/multi/lock.txt", "w");
    CHECK(w);
    CHECK(!mounts[1]->open("/multi/lock.txt", "a"));      // other client: busy
    CHECK(!mounts[1]->open("/multi/lock.txt", "w"));
    RemoteFile r = mounts[1]->open("/multi/lock.txt");    // reading is allowed
    CHECK(r);
    CHECK(!mounts[2]->remove("/multi/lock.txt"));
    CHECK(!mounts[2]->rename("/multi/lock.txt", "/multi/lock2.txt"));
    r.close();
    w.close();
    CHECK(mounts[1]->remove("/multi/lock.txt"));
  }

  // ---- concurrent appenders must not lose or mix lines ------------------------
  {
    std::vector<std::thread> appenders;
    std::atomic<int> failed{0};
    for (int i = 0; i < 3; i++) {
      appenders.emplace_back([&, i] {
        for (int k = 0; k < 25; k++) {
          RemoteFile f = mounts[i]->openWait("/multi/log.txt", "a", 20000, 2);
          std::string line = "c" + std::to_string(i) + " line " + std::to_string(k) + "\n";
          if (!f || f.write((const uint8_t*)line.data(), line.size()) != line.size()) failed++;
        }
      });
    }
    for (auto& t : appenders) t.join();
    CHECK(failed == 0);
    std::string log = slurp(base / "root" / "log.txt");
    int lines = 0;
    bool intact = true;
    for (int i = 0; i < 3; i++)
      for (int k = 0; k < 25; k++) {
        std::string line = "c" + std::to_string(i) + " line " + std::to_string(k) + "\n";
        size_t pos = log.find(line);
        if (pos == std::string::npos || (pos > 0 && log[pos - 1] != '\n')) intact = false;
        else lines++;
      }
    CHECK(intact && lines == 75);
    CHECK(std::count(log.begin(), log.end(), '\n') == 75);  // nothing overwritten or extra
  }

  // ---- idle sessions expire; the client joins again ------------------------
  {
    // a file that is open when the session is lost; its lock must be released
    RemoteFile stale = mounts[0]->open("/multi/c0.bin");
    RemoteFile wlock = mounts[1]->open("/multi/held.txt", "w");
    CHECK(stale && wlock);
    {
      std::lock_guard<std::mutex> g(srvMutex);
      server.setSessionTimeout(300);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    CHECK(clientCount() == 0);
    {
      std::lock_guard<std::mutex> g(srvMutex);
      server.setSessionTimeout(60000);
    }
    uint8_t oldId = clients[0]->sessionId();
    // (a file from the lost session is invalid, even if its handle number is
    // reused by a file of the new session)
    // the first request finds out that the session is gone and fails quickly...
    CHECK(!mounts[0]->exists("/multi/c0.bin"));
    // ...the next one joins again and works
    CHECK(mounts[0]->exists("/multi/c0.bin"));
    CHECK(clientCount() == 1);
    CHECK(clients[0]->sessionId() != oldId);

    // the lock of the lost session is gone: another client can write the file
    // clients 1 and 2 find out that their sessions are gone (the first request
    // fails), the next one joins again
    CHECK(!mounts[1]->exists("/multi/held.txt"));
    CHECK(mounts[1]->exists("/multi/held.txt"));
    CHECK(!mounts[2]->exists("/multi/held.txt"));
    CHECK(mounts[2]->exists("/multi/held.txt"));
    {
      RemoteFile again = mounts[2]->open("/multi/held.txt", "w");
      CHECK(again);
      // client 0 opens another file in its new session: it gets handle 0, the
      // same number the stale file had. The stale file must not read it.
      RemoteFile fresh = mounts[0]->open("/multi/c1.bin");
      CHECK(fresh);
      uint8_t b[8];
      CHECK(stale.read(b, sizeof(b)) == 0);  // closed together with its session
      stale.close();                         // and must not close 'fresh'
      CHECK(fresh.read(b, sizeof(b)) == sizeof(b));
    }
    wlock.close();
  }

  // ---- keep alive ------------------------------------------------------------
  {
    {
      std::lock_guard<std::mutex> g(srvMutex);
      server.setSessionTimeout(400);
    }
    for (int i = 0; i < 6; i++) {
      clients[0]->keepAlive();
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    CHECK(clientCount() == 1);  // would have expired without keepAlive()
  }

  // ---- link down closes all sessions --------------------------------------
  for (size_t i = 0; i < kClients; i++) hub.setConnected(i, false);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(clientCount() == 0);

  running = false;
  srvThread.join();
  TinyBLEFSLogger.setLevel(savedLevel);
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  fs::path base = argv[1];
  fs::create_directories(base / "a");
  fs::create_directories(base / "b");

  StdFilesystemBackend backend;
  LoopbackPair linkA(20), linkB(185);  // tiny MTU forces fragmentation
  FileServer srvA(backend, linkA.server(), "sensor1", (base / "a").c_str());
  FileServer srvB(backend, linkB.server(), "sensor1", (base / "b").c_str(), 1, 2);
  FileClient cA(linkA.client()), cB(linkB.client());
  cA.setIdleCallback(idle, &srvA);
  cB.setIdleCallback(idle, &srvB);

  Mounts m;
  CHECK(m.mount(cA) == "sensor1");
  CHECK(m.mount(cB) == "sensor1-2");  // name collision
  CHECK(m.mount(cB, "my dev!") == "my_dev_");

  // write / read back a large file (spans many chunks + packets)
  std::vector<uint8_t> data(100000);
  for (size_t i = 0; i < data.size(); i++) data[i] = (uint8_t)(i * 7 + i / 251);
  {
    RemoteFile f = m.open("/sensor1/big.bin", "w");
    CHECK(f);
    CHECK(f.write(data.data(), data.size()) == data.size());
    CHECK(f.size() == data.size());
  }
  CHECK(fs::file_size(base / "a" / "big.bin") == data.size());
  {
    RemoteFile f = m.open("/sensor1/big.bin");
    CHECK(f && f.size() == data.size());
    std::vector<uint8_t> back(data.size());
    CHECK(f.read(back.data(), back.size()) == back.size());
    CHECK(back == data);
    CHECK(f.read() == -1);  // EOF
    CHECK(f.seek(10));
    CHECK(f.read() == data[10]);
    CHECK(f.peek() == data[11]);
    CHECK(f.available() == (int)data.size() - 11);
  }

  // same file name on the other server is independent
  CHECK(!m.exists("/sensor1-2/big.bin"));
  { RemoteFile f = m.open("/sensor1-2/hello.txt", "w"); f.println("hi"); }
  CHECK(fs::file_size(base / "b" / "hello.txt") == 3);

  // append
  { RemoteFile f = m.open("/sensor1-2/hello.txt", "a"); f.print("there"); }
  CHECK(fs::file_size(base / "b" / "hello.txt") == 8);

  // directories, listing, rename, remove
  CHECK(m.mkdir("/sensor1/dir"));
  CHECK(!m.mkdir("/sensor1/dir"));
  CHECK(m.rename("/sensor1/big.bin", "/sensor1/dir/moved.bin"));
  CHECK(!m.rename("/sensor1/dir/moved.bin", "/sensor1-2/x"));  // cross server
  {
    RemoteFile d = m.open("/sensor1");
    CHECK(d && d.isDirectory());
    int n = 0;
    for (RemoteFile e = d.openNextFile(); e; e = d.openNextFile()) {
      CHECK(std::string(e.name()) == "dir" && e.isDirectory());
      n++;
    }
    CHECK(n == 1);
  }
  CHECK(m.remove("/sensor1/dir/moved.bin"));
  CHECK(m.rmdir("/sensor1/dir"));
  CHECK(!m.exists("/sensor1/dir"));

  // copy a file between servers, and a directory tree
  { RemoteFile f = m.open("/sensor1/src.bin", "w"); f.write(data.data(), data.size()); }
  CHECK(m.copy("/sensor1/src.bin", "/sensor1-2/copy.bin"));
  CHECK(fs::file_size(base / "b" / "copy.bin") == data.size());
  {
    RemoteFile f = m.open("/sensor1-2/copy.bin");
    std::vector<uint8_t> back(data.size());
    CHECK(f.read(back.data(), back.size()) == back.size());
    CHECK(back == data);
  }
  CHECK(m.mkdir("/sensor1/tree"));
  CHECK(m.mkdir("/sensor1/tree/sub"));
  { RemoteFile f = m.open("/sensor1/tree/a.txt", "w"); f.print("A"); }
  { RemoteFile f = m.open("/sensor1/tree/sub/b.txt", "w"); f.print("BB"); }
  CHECK(m.copy("/sensor1/tree", "/sensor1-2/tree2"));
  CHECK(fs::file_size(base / "b" / "tree2" / "a.txt") == 1);
  CHECK(fs::file_size(base / "b" / "tree2" / "sub" / "b.txt") == 2);
  CHECK(!m.copy("/sensor1/tree", "/sensor1/tree/inner"));  // into itself
  CHECK(!m.copy("/sensor1/missing", "/sensor1-2/x"));

  // path jail
  CHECK(!m.exists("/sensor1/../b/hello.txt"));
  CHECK(!m.open("/sensor1/../../etc/passwd"));
  CHECK(!m.open("/sensor1/missing.txt"));
  CHECK(!m.open("/nobody/x"));

  // handle exhaustion on srvB (2 handles) and release by close
  {
    RemoteFile f1 = m.open("/sensor1-2/h1", "w");
    RemoteFile f2 = m.open("/sensor1-2/h2", "w");
    RemoteFile f3 = m.open("/sensor1-2/h3", "w");
    CHECK(f1 && f2 && !f3);
    f1.close();
    RemoteFile f4 = m.open("/sensor1-2/h3", "w");
    CHECK(f4);
  }

  // batched directory listing: many entries per request
  {
    fs::create_directories(base / "a" / "many");
    for (int i = 0; i < 150; i++) std::ofstream(base / "a" / "many" / ("f" + std::to_string(i) + ".txt")) << "x";
    std::vector<DirEntry> es;
    uint32_t r0 = cA.requestCount();
    CHECK(m.list("/sensor1/many", es));
    uint32_t used = cA.requestCount() - r0;
    CHECK(es.size() == 150);
    std::set<std::string> names;
    for (auto& e : es) names.insert(e.name);
    CHECK(names.size() == 150 && names.count("f0.txt") && names.count("f149.txt"));
    CHECK(used < 12);  // 151 requests without batching
    // a tiny budget still makes progress (at least one entry per request)
    std::vector<DirEntry> part;
    bool end = false;
    CHECK(cA.listBatch("/many", 0, 10, part, end) == Status::Ok && part.size() == 1 && !end);
    CHECK(cA.listBatch("/many", 149, 400, part, end) == Status::Ok && part.size() == 1 && end);
    CHECK(cA.listBatch("/many", 150, 400, part, end) == Status::Ok && part.empty() && end);
    // directory iteration with openNextFile() fetches batches, not single entries
    {
      RemoteFile d = m.open("/sensor1/many");
      int n = 0;
      for (RemoteFile f = d.openNextFile(); f; f = d.openNextFile()) n++;
      CHECK(n == 150);
      d.rewindDirectory();
      RemoteFile first = d.openNextFile();
      CHECK(first && std::string(first.name()).size() > 0);
    }
    // an older server does not know the operation: the client falls back
    LoopbackPair linkC(512);
    struct NoBatch : public Transport {  // turns the new operation into an unknown one
      explicit NoBatch(Transport& t) : t_(t) {}
      size_t mtu() const override { return t_.mtu(); }
      bool send(const uint8_t* d, size_t n) override {
        std::vector<uint8_t> c(d, d + n);
        // packet = [session id][operation]...; control packets have session id 0
        if (n >= 2 && c[0] != 0 && c[1] == (uint8_t)Op::ListBatch) c[1] = 99;
        return t_.send(c.data(), c.size());
      }
      size_t receive(uint8_t* b, size_t max) override {
        size_t n = t_.receive(b, max);
        // the answer of the unknown operation 99 belongs to the request that was ListBatch
        if (n >= 2 && b[0] != 0 && b[1] == (99 | kResponseFlag)) b[1] = (uint8_t)Op::ListBatch | kResponseFlag;
        return n;
      }
      bool connected() const override { return t_.connected(); }
      Transport& t_;
    } noBatch(linkC.client());
    FileServer srvC(backend, linkC.server(), "old", (base / "a").c_str());
    FileClient cC(noBatch);
    cC.setIdleCallback(idle, &srvC);
    r0 = cC.requestCount();
    CHECK(cC.listBatch("/many", 0, 400, part, end) == Status::Ok && !part.empty());
    std::vector<DirEntry> all = part;
    for (int guard = 0; !end && guard < 100; guard++) {
      Status st = cC.listBatch("/many", (uint16_t)all.size(), 400, part, end);
      CHECK(st == Status::Ok);
      if (st != Status::Ok) break;
      all.insert(all.end(), part.begin(), part.end());
    }
    CHECK(all.size() == 150);
    CHECK(cC.requestCount() - r0 > 150);  // entry by entry
  }

  // file locking: many readers, one writer, no truncate/remove while open
  {
    CHECK(m.mkdir("/sensor1/lk"));
    RemoteFile w = m.open("/sensor1/lk/a.txt", "w");  // writer
    CHECK(w);
    CHECK(!m.open("/sensor1/lk/a.txt", "r+"));        // second writer refused
    CHECK(!m.open("/sensor1/lk/a.txt", "a"));
    uint8_t h = 0;
    uint32_t sz = 0;
    CHECK(cA.open("/lk/a.txt", kWrite, h, sz) == Status::Busy);
    RemoteFile r = m.open("/sensor1/lk/a.txt");       // readers are fine
    CHECK(r);
    CHECK(!m.remove("/sensor1/lk/a.txt"));            // busy while open
    CHECK(!m.rename("/sensor1/lk/a.txt", "/sensor1/lk/b.txt"));
    CHECK(!m.rename("/sensor1/lk", "/sensor1/lk2"));  // a file below is open
    CHECK(!m.rmdir("/sensor1/lk"));
    uint32_t t0 = nowMs();
    CHECK(!m.openWait("/sensor1/lk/a.txt", "a", 150, 20));  // gives up after 150 ms
    CHECK(nowMs() - t0 >= 140);
    r.close();
    w.close();
    RemoteFile r2 = m.open("/sensor1/lk/a.txt");
    CHECK(r2);
    CHECK(!m.open("/sensor1/lk/a.txt", "w"));         // truncate needs a closed file
    r2.close();
    { RemoteFile t = m.open("/sensor1/lk/a.txt", "w"); CHECK(t); }
    CHECK(m.rename("/sensor1/lk/a.txt", "/sensor1/lk/b.txt"));  // closed: allowed
    CHECK(m.remove("/sensor1/lk/b.txt"));
    CHECK(m.rmdir("/sensor1/lk"));
  }

  // read only server
  srvA.setReadOnly(true);
  CHECK(!m.open("/sensor1/new.txt", "w"));
  CHECK(!m.remove("/sensor1/x"));
  CHECK(m.open("/sensor1"));  // reading still works

  // logger: levels and output
  {
    CapturePrint cap;
    TinyBLEFSLogger.begin(cap, LogLevel::Warning);
    TinyBLEFSLogger.setTimestamps(false);
    TINYBLEFS_LOGD("test", "hidden %d", 1);
    TINYBLEFS_LOGI("test", "hidden %d", 2);
    TINYBLEFS_LOGW("test", "shown %d", 3);
    TINYBLEFS_LOGE("test", "shown %s", "four");
    CHECK(cap.text == "W test: shown 3\nE test: shown four\n");
    cap.text.clear();
    TinyBLEFSLogger.setLevel(LogLevel::Debug);
    CHECK(!m.open("/sensor1/new.txt", "w"));  // server (read only) logs the failure
    CHECK(cap.text.find("server: open failed: denied") != std::string::npos);
    CHECK(cap.text.find("open for writing ") != std::string::npos);
    CHECK(cap.text.find("client: open request failed: denied") != std::string::npos);
    CHECK(cap.text.find("file: cannot open /new.txt: denied") != std::string::npos);
    cap.text.clear();
    CHECK(!m.exists("/nobody/x"));  // unknown mount
    CHECK(cap.text.find("mounts: no server mounted as /nobody") != std::string::npos);
    cap.text.clear();
    CHECK(!m.open("/sensor1/../x"));  // leaves the shared directory
    CHECK(cap.text.find("leaves the shared directory") != std::string::npos);
    cap.text.clear();
    CHECK(!m.exists("/sensor1/none"));  // normal "not found": debug only
    CHECK(cap.text.find(" W ") == std::string::npos);
    TinyBLEFSLogger.setLevel(LogLevel::None);
    cap.text.clear();
    TINYBLEFS_LOGE("test", "silent");
    CHECK(cap.text.empty());
    TinyBLEFSLogger.end();
  }

  // timeout when server is not polled
  FileClient dead(linkA.client(), 50);
  ServerInfo si;
  // no answer to the hello: the client cannot join a session
  CHECK(dead.info(si) == Status::SendFailed || dead.info(si) == Status::Timeout);

  // several clients on one server (own threads, shared link)
  testSeveralClients(base / "several");

  printf(failures ? "%d FAILURES\n" : "all tests passed\n", failures.load());
  return failures ? 1 : 0;
}
