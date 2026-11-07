// Tests the FUSE operations (tools/tinyblefs-cli/FuseFs.h) over loopback: mounts
// two servers with a real FUSE mount and uses the normal file API on it.
// Run: ./test_fuse <empty scratch dir>   (skips with exit 0 if FUSE is unusable)
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

#include "FuseFs.h"
#include "TinyBLEFS/adapters/Loopback.h"
#include "TinyBLEFS/backends/StdFilesystem.h"

using namespace tinyblefs;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c)                                  \
  do {                                            \
    if (!(c)) {                                   \
      printf("FAIL line %d: %s\n", __LINE__, #c); \
      failures++;                                 \
    }                                             \
  } while (0)

static void idle(void* ctx) { ((FileServer*)ctx)->poll(); }

static std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}
static bool spit(const fs::path& p, const std::string& s, std::ios::openmode m = std::ios::out) {
  std::ofstream f(p, std::ios::binary | m);
  f << s;
  f.flush();
  return (bool)f;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  fs::path base = argv[1];
  fs::create_directories(base / "a");
  fs::create_directories(base / "b");
  fs::create_directories(base / "mnt");

  if (access("/dev/fuse", R_OK | W_OK) != 0) {
    printf("SKIP: /dev/fuse not usable\n");
    return 0;
  }

  StdFilesystemBackend backend;
  LoopbackPair la(23), lb(185);
  FileServer sa(backend, la.server(), "alpha", (base / "a").c_str());
  FileServer sb(backend, lb.server(), "beta", (base / "b").c_str());
  FileClient ca(la.client()), cb(lb.client());
  ca.setIdleCallback(idle, &sa);
  cb.setIdleCallback(idle, &sb);
  Mounts mounts;
  mounts.mount(ca);
  mounts.mount(cb);
  FuseFs ffs(mounts);

  fs::create_directories(base / "a" / "dir");
  spit(base / "a" / "hello.txt", "hello world");

  // fuse_main in a thread; all server work happens in that thread (idle callbacks)
  std::string mnt = (base / "mnt").string();
  std::thread t([&] {
    const char* av[] = {"test_fuse", "-f", "-s", mnt.c_str()};
    fuse_main(4, (char**)av, &FuseFs::operations(), &ffs);
  });
  fs::path m = base / "mnt";
  bool up = false;
  for (int i = 0; i < 100 && !up; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    struct stat st;
    up = stat((m / "alpha").c_str(), &st) == 0;
  }
  if (!up) {
    printf("SKIP: FUSE mount not possible\n");
    system(("fusermount3 -u " + mnt + " 2>/dev/null").c_str());
    t.join();
    return 0;
  }

  // root: the mounts
  CHECK(fs::is_directory(m / "alpha") && fs::is_directory(m / "beta"));
  int n = 0;
  for (auto& e : fs::directory_iterator(m)) { (void)e; n++; }
  CHECK(n == 2);

  // read
  CHECK(slurp(m / "alpha" / "hello.txt") == "hello world");
  CHECK(fs::file_size(m / "alpha" / "hello.txt") == 11);
  CHECK(fs::is_directory(m / "alpha" / "dir"));
  CHECK(!fs::exists(m / "alpha" / "nope"));

  // create, write, overwrite, append
  CHECK(spit(m / "alpha" / "new.txt", "abc"));
  CHECK(slurp(base / "a" / "new.txt") == "abc");
  CHECK(spit(m / "alpha" / "new.txt", "xy"));  // O_TRUNC
  CHECK(slurp(base / "a" / "new.txt") == "xy");
  CHECK(spit(m / "alpha" / "new.txt", "z", std::ios::app));
  CHECK(slurp(base / "a" / "new.txt") == "xyz");

  // larger than a request (several chunks) and written in pieces
  std::string big(5000, 0);
  for (size_t i = 0; i < big.size(); i++) big[i] = (char)('a' + i % 26);
  CHECK(spit(m / "beta" / "big.bin", big));
  CHECK(slurp(base / "b" / "big.bin") == big);
  CHECK(slurp(m / "beta" / "big.bin") == big);

  // directories
  CHECK(fs::create_directory(m / "alpha" / "d2"));
  CHECK(fs::is_directory(base / "a" / "d2"));
  CHECK(spit(m / "alpha" / "d2" / "f", "1"));
  std::error_code ec;
  CHECK(!fs::remove(m / "alpha" / "d2", ec));  // not empty
  CHECK(fs::remove(m / "alpha" / "d2" / "f"));
  CHECK(fs::remove(m / "alpha" / "d2"));
  CHECK(!fs::exists(base / "a" / "d2"));

  // rename, also over an existing file
  CHECK(rename((m / "alpha" / "new.txt").c_str(), (m / "alpha" / "moved.txt").c_str()) == 0);
  CHECK(slurp(base / "a" / "moved.txt") == "xyz" && !fs::exists(base / "a" / "new.txt"));
  spit(m / "alpha" / "other.txt", "o");
  CHECK(rename((m / "alpha" / "other.txt").c_str(), (m / "alpha" / "moved.txt").c_str()) == 0);
  CHECK(slurp(base / "a" / "moved.txt") == "o");

  // truncate to 0 works, other sizes are refused, same size is a no-op
  CHECK(truncate((m / "alpha" / "moved.txt").c_str(), 1) == 0);
  CHECK(truncate((m / "alpha" / "moved.txt").c_str(), 5) != 0);
  CHECK(truncate((m / "alpha" / "moved.txt").c_str(), 0) == 0);
  CHECK(fs::file_size(base / "a" / "moved.txt") == 0);

  // touch (utimens) and chmod are accepted
  CHECK(spit(m / "alpha" / "t", ""));
  CHECK(chmod((m / "alpha" / "t").c_str(), 0600) == 0);
  fs::last_write_time(m / "alpha" / "t", fs::file_time_type::clock::now());

  // errors
  CHECK(fopen((m / "alpha" / "nodir" / "f").c_str(), "w") == nullptr && errno == ENOENT);
  CHECK(unlink((m / "alpha" / "nope").c_str()) == -1 && errno == ENOENT);
  CHECK(mkdir((m / "gamma").c_str(), 0755) != 0);  // no server of that name

  // unmount: fuse_main returns
  system(("fusermount3 -u " + mnt + " 2>/dev/null || fusermount -u " + mnt).c_str());
  t.join();

  printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
