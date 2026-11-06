// Tests the command layer of the tinyblefs command line tool over loopback.
// Build: g++ -std=c++17 -I../src -I../tools/tinyblefs-cli test_cli.cpp -o test_cli
// Run:   ./test_cli <empty scratch dir>
#include <cstdio>

#include "Commands.h"
#include "TinyBLEFS/adapters/Loopback.h"
#include "TinyBLEFS/backends/StdFilesystem.h"

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

static void idle(void* ctx) { ((FileServer*)ctx)->poll(); }

static std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  fs::path base = argv[1];
  fs::create_directories(base / "a");
  fs::create_directories(base / "b");
  fs::create_directories(base / "local");

  StdFilesystemBackend backend;
  LoopbackPair la(23), lb(185);
  FileServer sa(backend, la.server(), "alpha", (base / "a").c_str());
  FileServer sb(backend, lb.server(), "beta", (base / "b").c_str());
  sb.setReadOnly(false);
  FileClient ca(la.client()), cb(lb.client());
  ca.setIdleCallback(idle, &sa);
  cb.setIdleCallback(idle, &sb);
  Mounts mounts;
  mounts.mount(ca);
  mounts.mount(cb);

  std::ostringstream out, err;
  Cli cli(mounts, out, err);
  auto run = [&](const std::string& line) {
    out.str("");
    err.str("");
    return cli.run(Cli::split(line));
  };

  // split()
  auto t = Cli::split("write 'a b' \"c \\\"d\\\"\" e\\ f");
  CHECK(t.size() == 4 && t[1] == "a b" && t[2] == "c \"d\"" && t[3] == "e f");

  CHECK(run("mounts") == 0);
  CHECK(out.str().find("/alpha  server=alpha") != std::string::npos);
  CHECK(out.str().find("/beta  server=beta") != std::string::npos);
  CHECK(out.str().find("access=read-write") != std::string::npos);

  CHECK(run("ls /") == 0 && out.str() == "alpha/\nbeta/\n");
  CHECK(run("mkdir -p /alpha/x/y/z") == 0);
  CHECK(fs::is_directory(base / "a/x/y/z"));
  CHECK(run("write /alpha/x/hello.txt hello   world") == 0);
  CHECK(run("append /alpha/x/hello.txt second line") == 0);
  CHECK(run("cat /alpha/x/hello.txt") == 0);
  CHECK(out.str() == "hello world\nsecond line\n");
  CHECK(run("touch /alpha/x/hello.txt /alpha/x/empty") == 0);
  CHECK(slurp(base / "a/x/hello.txt") == "hello world\nsecond line\n");  // touch keeps content
  CHECK(fs::exists(base / "a/x/empty"));

  CHECK(run("stat /alpha/x/hello.txt") == 0);
  CHECK(out.str() == "/alpha/x/hello.txt: file, 24 bytes\n");

  // cwd + relative paths
  CHECK(run("cd /alpha/x") == 0 && cli.cwd() == "/alpha/x");
  CHECK(run("ls -l") == 0);
  CHECK(out.str().find("-         24 hello.txt") != std::string::npos);
  CHECK(out.str().find("d          0 y") != std::string::npos);
  CHECK(run("cd y/../y/z") == 0 && cli.cwd() == "/alpha/x/y/z");
  CHECK(run("cd ../../..") == 0 && cli.cwd() == "/alpha");
  CHECK(run("cd /alpha/x/hello.txt") == 1);  // not a directory
  CHECK(run("cd /") == 0);

  // cp: between servers, into a directory
  CHECK(run("mkdir /beta/dest") == 0);
  CHECK(run("cp /alpha/x/hello.txt /beta/dest") == 0);
  CHECK(slurp(base / "b/dest/hello.txt") == "hello world\nsecond line\n");
  CHECK(run("cp /alpha/x /beta/dest") == 1);  // directory needs -r
  CHECK(run("cp -r /alpha/x /beta/dest") == 0);
  CHECK(fs::exists(base / "b/dest/x/y/z"));
  CHECK(run("ls -R /beta") == 0);
  CHECK(out.str().find("/beta/dest/x/y:") != std::string::npos);

  // mv: same server (rename), other server (copy + delete)
  CHECK(run("mv /beta/dest/hello.txt /beta/dest/renamed.txt") == 0);
  CHECK(fs::exists(base / "b/dest/renamed.txt") && !fs::exists(base / "b/dest/hello.txt"));
  CHECK(run("mv /beta/dest/renamed.txt /alpha") == 0);
  CHECK(fs::exists(base / "a/renamed.txt") && !fs::exists(base / "b/dest/renamed.txt"));
  CHECK(run("mv /beta/dest/x /alpha/moved") == 0);
  CHECK(fs::exists(base / "a/moved/y/z") && !fs::exists(base / "b/dest/x"));

  // put / get (single file and trees)
  std::string big(70000, 'q');
  for (size_t i = 0; i < big.size(); i++) big[i] = (char)('a' + i % 23);
  { std::ofstream(base / "local/big.bin", std::ios::binary) << big; }
  fs::create_directories(base / "local/tree/sub");
  { std::ofstream(base / "local/tree/one.txt") << "1"; }
  { std::ofstream(base / "local/tree/sub/two.txt") << "22"; }
  CHECK(run(std::string("put ") + (base / "local/big.bin").string() + " /beta/big.bin") == 0);
  CHECK(slurp(base / "b/big.bin") == big);
  CHECK(run(std::string("put ") + (base / "local/tree").string() + " /beta/t") == 1);  // needs -r
  CHECK(run(std::string("put -r ") + (base / "local/tree").string() + " /beta/t") == 0);
  CHECK(slurp(base / "b/t/sub/two.txt") == "22");
  fs::create_directories(base / "dl");
  CHECK(run("get /beta/big.bin " + (base / "dl").string()) == 0);  // into dir
  CHECK(slurp(base / "dl/big.bin") == big);
  CHECK(run("get -r /beta/t " + (base / "dl/t").string()) == 0);
  CHECK(slurp(base / "dl/t/one.txt") == "1");

  // rm / rmdir
  CHECK(run("rm /beta/t") == 1);  // directory needs -r
  CHECK(run("rm -r /beta/t") == 0 && !fs::exists(base / "b/t"));
  CHECK(run("rm /beta/big.bin /beta/nothing") == 1);  // second one fails
  CHECK(!fs::exists(base / "b/big.bin"));
  CHECK(run("rmdir /alpha/x/y/z") == 0);
  CHECK(run("rmdir /alpha/x") == 1);  // not empty

  // errors
  CHECK(run("cat /alpha/none") == 1 && !err.str().empty());
  CHECK(run("ls /nobody") == 1);
  CHECK(run("bogus") == 2);
  CHECK(run("cp /alpha/only-one") == 2);

  // read-only server
  sb.setReadOnly(true);
  CHECK(run("write /beta/no.txt x") == 1);
  CHECK(run("mounts") == 0 && out.str().find("access=read-only") != std::string::npos);

  printf(failures ? "%d FAILURES\n" : "all CLI tests passed\n", failures);
  return failures ? 1 : 0;
}
