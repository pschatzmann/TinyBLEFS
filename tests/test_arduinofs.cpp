// ArduinoFsBackend with a mock of the Arduino FS classes, once with a
// mountpoint() (POSIX fast path, as on the ESP32) and once without (generic
// Arduino API). Run: ./test_arduinofs <empty scratch dir>
#define TINYBLEFS_ARDUINOFS_POSIX 1  // the fast path is enabled automatically on the ESP32

#include <chrono>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>

#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/Loopback.h"
#include "TinyBLEFS/backends/ArduinoFS.h"

using namespace tinyblefs;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) {                                           \
      printf("FAIL line %d: %s\n", __LINE__, #c);         \
      failures++;                                         \
    }                                                     \
  } while (0)

static int g_opens = 0;  // number of File objects opened (expensive on a real FS)

/// Mock of the File class of the Arduino cores (reference counted like the real one)
struct MockFile {
  struct Impl {
    FILE* f = nullptr;
    std::string path;
    bool dir = false;
    std::vector<std::string> names;
    size_t next = 0;
    ~Impl() { if (f) fclose(f); }
  };
  std::shared_ptr<Impl> p;
  std::string root;

  explicit operator bool() const { return p != nullptr; }
  bool isDirectory() const { return p->dir; }
  const char* name() const { return p->path.c_str(); }  // like the cores: the full path
  size_t size() { long cur = ftell(p->f); fseek(p->f, 0, SEEK_END); long n = ftell(p->f); fseek(p->f, cur, SEEK_SET); return n; }
  bool seek(uint32_t pos) { return fseek(p->f, pos, SEEK_SET) == 0; }
  size_t read(uint8_t* b, size_t n) { return fread(b, 1, n, p->f); }
  size_t write(const uint8_t* b, size_t n) { size_t w = fwrite(b, 1, n, p->f); fflush(p->f); return w; }
  void close() { p.reset(); }
  MockFile openNextFile();
};

struct MockFS {
  std::string root;
  MockFile open(const char* path, const char* mode = "r");
  bool exists(const char* path) { return fs::exists(root + path); }
  bool remove(const char* path) { std::error_code ec; return fs::is_regular_file(root + path) && fs::remove(root + path, ec); }
  bool mkdir(const char* path) { std::error_code ec; return fs::create_directory(root + path, ec); }
  bool rmdir(const char* path) { std::error_code ec; return fs::is_directory(root + path) && fs::remove(root + path, ec); }
  bool rename(const char* a, const char* b) { std::error_code ec; fs::rename(root + a, root + b, ec); return !ec; }
};
/// like the ESP32 FS classes
struct MockFSWithMount : MockFS {
  const char* mountpoint() { return root.c_str(); }
};

MockFile MockFS::open(const char* path, const char* mode) {
  g_opens++;
  MockFile r;
  r.root = root;
  std::string full = root + path;
  auto impl = std::make_shared<MockFile::Impl>();
  impl->path = path;
  if (fs::is_directory(full)) {
    impl->dir = true;
    for (auto& e : fs::directory_iterator(full)) impl->names.push_back(e.path().filename().string());
    std::sort(impl->names.begin(), impl->names.end());
  } else {
    const char* m = !strcmp(mode, "w") ? "wb" : !strcmp(mode, "r+") ? "r+b" : "rb";
    impl->f = fopen(full.c_str(), m);
    if (!impl->f) return r;  // invalid File
  }
  r.p = impl;
  return r;
}

MockFile MockFile::openNextFile() {
  MockFile r;
  if (!p || !p->dir || p->next >= p->names.size()) return r;
  std::string child = p->path + (p->path.back() == '/' ? "" : "/") + p->names[p->next++];
  MockFS tmp;
  tmp.root = root;
  return tmp.open(child.c_str(), "r");
}

static void idle(void* ctx) { ((FileServer*)ctx)->poll(); }

template <class FSTYPE>
void run(const char* label, const fs::path& dir, bool posix) {
  fs::create_directories(dir);
  FSTYPE mock;
  mock.root = dir.string();
  const int kFiles = 120;
  for (int i = 0; i < kFiles; i++) std::ofstream(dir / ("f" + std::to_string(i) + ".txt")) << std::string(i + 1, 'x');
  fs::create_directories(dir / "sub");

  ArduinoFsBackend<FSTYPE> backend(mock);
  LoopbackPair link(23);
  FileServer server(backend, link.server(), "mock", "/", 1, 4);
  FileClient client(link.client());
  client.setIdleCallback(idle, &server);
  Mounts m;
  CHECK(m.mount(client) == "mock");

  // listing: complete, right types and sizes
  std::vector<DirEntry> entries;
  g_opens = 0;
  auto t0 = std::chrono::steady_clock::now();
  CHECK(m.list("/mock", entries));
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
  int opens = g_opens;
  CHECK(entries.size() == (size_t)kFiles + 1);
  bool sizesOk = true, sawSub = false;
  for (auto& e : entries) {
    if (e.name == "sub") { sawSub = e.isDir && e.size == 0; continue; }
    int i = atoi(e.name.c_str() + 1);
    if (e.isDir || e.size != (uint32_t)(i + 1)) sizesOk = false;
  }
  CHECK(sizesOk && sawSub);
  // the POSIX variant must not open a File per entry
  if (posix) CHECK(opens == 0);
  else CHECK(opens > kFiles);
  printf("%-28s listing %d entries: %6lld us, %d File objects opened\n", label, kFiles + 1, (long long)us, opens);

  // listing that is interrupted by changes still ends cleanly
  uint16_t idx = 0;
  bool isDir;
  uint32_t size;
  std::string name;
  CHECK(client.list("/", idx++, isDir, size, name) == Status::Ok);
  CHECK(m.mkdir("/mock/newdir"));  // closes the open directory
  CHECK(client.list("/", idx++, isDir, size, name) == Status::Ok);
  int guard = 0;
  while (client.list("/", idx++, isDir, size, name) == Status::Ok && guard++ < 1000) {}
  CHECK(guard < 1000);
  CHECK(m.rmdir("/mock/newdir"));

  // an empty directory and one that does not exist
  CHECK(m.mkdir("/mock/empty"));
  CHECK(m.list("/mock/empty", entries) && entries.empty());
  // a missing directory lists as empty (callers check with stat() first)
  CHECK(!m.exists("/mock/nodir"));
  CHECK(m.list("/mock/nodir", entries) && entries.empty());

  // stat
  bool d;
  uint32_t sz;
  CHECK(m.stat("/mock", d, sz) && d);                       // the root
  CHECK(m.stat("/mock/f9.txt", d, sz) && !d && sz == 10);
  CHECK(m.stat("/mock/sub", d, sz) && d && sz == 0);
  CHECK(!m.stat("/mock/none", d, sz));

  // file access through the same backend
  {
    RemoteFile f = m.open("/mock/new.txt", "w");
    CHECK(f && f.print("hello world") == 11);
  }
  {
    RemoteFile f = m.open("/mock/new.txt", "r+");
    CHECK(f && f.seek(6) && f.print("WORLD") == 5);
  }
  {
    RemoteFile f = m.open("/mock/new.txt");
    char buf[32] = {0};
    CHECK(f && f.read((uint8_t*)buf, sizeof(buf)) == 11 && std::string(buf) == "hello WORLD");
  }
  CHECK(m.rename("/mock/new.txt", "/mock/renamed.txt") && m.exists("/mock/renamed.txt"));
  CHECK(m.remove("/mock/renamed.txt") && !m.exists("/mock/renamed.txt"));
  CHECK(!m.open("/mock/absent.txt"));
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  TinyBLEFSLogger.setLevel(LogLevel::None);
  run<MockFSWithMount>("POSIX path (mountpoint)", fs::path(argv[1]) / "posix", true);
  run<MockFS>("Arduino API (no mountpoint)", fs::path(argv[1]) / "generic", false);
  printf(failures ? "%d FAILURES\n" : "all ArduinoFS backend tests passed\n", failures);
  return failures ? 1 : 0;
}
