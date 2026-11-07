// TinyFatBackend (TinyFATFS) on a RAM disk, through the full path
// FileClient -> loopback -> FileServer -> TinyFatBackend -> FatFs.
// Needs the TinyFATFS library (see TINYBLEFS_TINYFATFS_DIR in CMakeLists.txt).
// TinyFATFS provides main() on the desktop and calls setup().
#include <chrono>
#include <cstdio>

#include "TinyBLEFS.h"
#include "TinyBLEFS/transports/Loopback.h"
#include "TinyBLEFS/fs/TinyFATFS.h"

using namespace tinyblefs;

static int failures = 0;
#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) {                                           \
      printf("FAIL line %d: %s\n", __LINE__, #c);         \
      failures++;                                         \
    }                                                     \
  } while (0)

static void idle(void* ctx) { ((FileServer*)ctx)->poll(); }

fatfs::RamIO drive{4000, 512};  // 2 MB

void setup() {
  TinyBLEFSLogger.setLevel(LogLevel::None);
  if (!SD.begin(drive)) {
    printf("FAIL: SD.begin()\n");
    _Exit(1);
  }

  TinyFatBackend backend(SD);
  LoopbackPair link(23);
  FileServer server(backend, link.server(), "fat", "/", 1, 4);
  FileClient client(link.client());
  client.setIdleCallback(idle, &server);
  Mounts m;
  CHECK(m.mount(client) == "fat");

  // root directory
  bool isDir = false;
  uint32_t size = 1;
  CHECK(m.stat("/fat", isDir, size) && isDir && size == 0);
  std::vector<DirEntry> entries;
  CHECK(m.list("/fat", entries) && entries.empty());

  // big file: write, read back, size
  std::string data(70000, 0);
  for (size_t i = 0; i < data.size(); i++) data[i] = (char)('a' + (i * 7 + i / 251) % 26);
  {
    RemoteFile f = m.open("/fat/big.bin", "w");
    CHECK(f && f.write((const uint8_t*)data.data(), data.size()) == data.size());
  }
  CHECK(m.stat("/fat/big.bin", isDir, size) && !isDir && size == data.size());
  {
    RemoteFile f = m.open("/fat/big.bin");
    std::string back(data.size(), 0);
    CHECK(f && f.read((uint8_t*)&back[0], back.size()) == back.size() && back == data);
    uint8_t b;
    CHECK(f.read(&b, 1) == 0);  // end of file
    CHECK(f.seek(100000) && f.read(&b, 1) == 0);  // far beyond the end
  }

  // overwrite with "w" truncates, "a" appends, "r+" patches in place
  {
    RemoteFile f = m.open("/fat/t.txt", "w");
    CHECK(f && f.print("hello world") == 11);
  }
  {
    RemoteFile f = m.open("/fat/t.txt", "w");
    CHECK(f && f.print("hi") == 2);
  }
  CHECK(m.stat("/fat/t.txt", isDir, size) && size == 2);
  {
    RemoteFile f = m.open("/fat/t.txt", "a");
    CHECK(f && f.print(" there") == 6);
  }
  {
    RemoteFile f = m.open("/fat/t.txt", "r+");
    CHECK(f && f.seek(1) && f.print("O") == 1);
  }
  {
    RemoteFile f = m.open("/fat/t.txt");
    char buf[16] = {0};
    CHECK(f && f.read((uint8_t*)buf, sizeof(buf)) == 8 && std::string(buf) == "hO there");
  }
  CHECK(!m.open("/fat/missing.txt"));
  CHECK(!m.open("/fat/missing.txt", "r+"));  // must exist

  // directories
  CHECK(m.mkdir("/fat/dir"));
  CHECK(!m.mkdir("/fat/dir"));
  CHECK(m.mkdir("/fat/dir/sub"));
  { RemoteFile f = m.open("/fat/dir/one.txt", "w"); f.print("1"); }
  { RemoteFile f = m.open("/fat/dir/sub/two.txt", "w"); f.print("22"); }
  CHECK(m.list("/fat/dir", entries) && entries.size() == 2);
  bool sawSub = false, sawOne = false;
  for (auto& e : entries) {
    if (e.name == "sub" && e.isDir) sawSub = true;
    if (e.name == "one.txt" && !e.isDir && e.size == 1) sawOne = true;
  }
  CHECK(sawSub && sawOne);
  CHECK(!m.rmdir("/fat/dir"));  // not empty
  CHECK(m.copy("/fat/dir", "/fat/dir2"));
  CHECK(m.stat("/fat/dir2/sub/two.txt", isDir, size) && size == 2);

  // rename and remove
  CHECK(m.rename("/fat/dir2", "/fat/moved"));
  CHECK(!m.exists("/fat/dir2") && m.exists("/fat/moved/one.txt"));
  CHECK(!m.rename("/fat/moved", "/fat/dir"));  // target exists
  CHECK(m.remove("/fat/moved/one.txt"));
  CHECK(m.remove("/fat/moved/sub/two.txt"));
  CHECK(m.rmdir("/fat/moved/sub"));
  CHECK(m.rmdir("/fat/moved"));
  CHECK(!m.remove("/fat/nothing"));

  // locks of the server protect the file system
  {
    RemoteFile w = m.open("/fat/lock.txt", "w");
    CHECK(w);
    CHECK(!m.open("/fat/lock.txt", "a"));
    CHECK(m.open("/fat/lock.txt"));  // reader
    CHECK(!m.remove("/fat/lock.txt"));
  }
  CHECK(m.remove("/fat/lock.txt"));

  // a path outside the shared root is refused, not looked up
  CHECK(!m.exists("/fat/../x"));

  // listing: one request per entry, each scans the directory from the start
  {
    CHECK(m.mkdir("/fat/many"));
    const int kFiles = 150;
    for (int i = 0; i < kFiles; i++) {
      std::string p = "/fat/many/f" + std::to_string(i) + ".txt";
      RemoteFile f = m.open(p.c_str(), "w");
      CHECK(f);
    }
    auto t0 = std::chrono::steady_clock::now();
    CHECK(m.list("/fat/many", entries) && entries.size() == (size_t)kFiles);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    printf("listing %d files: %lld us on the host, %d requests\n", kFiles,
           (long long)us, kFiles + 1);
  }

  SD.end();
  printf(failures ? "%d FAILURES\n" : "all TinyFATFS backend tests passed\n", failures);
  fflush(stdout);
  _Exit(failures ? 1 : 0);
}

void loop() {}
