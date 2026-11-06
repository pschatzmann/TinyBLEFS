// tinyblefs: command line client for TinyBLEFS servers (Linux, via SimpleBLE).
//
//   tinyblefs [options] <command> [args]     run one command
//   tinyblefs [options] shell                interactive shell (connects once)
//   tinyblefs [options] scan                 list servers in range, don't connect
//
// options:
//   -p <prefix>   only servers whose BLE device name starts with prefix
//   -a <address>  only the server with this BLE address (repeatable)
//   -t <ms>       scan time (default 5000)
//   -T <ms>       request timeout (default 10000)
//   -c <bytes>    data per read / write request (default 400; larger is faster
//                 if the server has the RAM for it)
//   -l <level>    log level on stderr: none, error (default), warning, info, debug
//   -v            same as -l info (shows what is mounted)
//
// Run "tinyblefs help" for the file commands.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

#include "Commands.h"
#include "TinyBLEFS/adapters/SimpleBLE.h"

using namespace tinyblefs;

namespace {

struct Remote {
  SimpleBLEClientTransport transport;
  std::unique_ptr<FileClient> client;
};

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return tolower(c); });
  return s;
}

void printUsage(const char* prog) {
  std::cerr << "usage: " << prog
            << " [-p prefix] [-a address] [-t scan-ms] [-T timeout-ms] [-c chunk-bytes] [-l level] [-v] "
               "<command> [args...]\n"
               "       "
            << prog << " shell | scan | help\n\n"
            << Cli::usage();
}

}  // namespace

int main(int argc, char** argv) {
  std::string prefix;
  std::vector<std::string> addresses;
  int scanMs = 5000, timeoutMs = 10000, chunk = 0;
  bool verbose = false;
  LogLevel logLevel = LogLevel::Error;

  int i = 1;
  for (; i < argc && argv[i][0] == '-'; i++) {
    std::string o = argv[i];
    auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (o == "-p" || o == "-a" || o == "-t" || o == "-T" || o == "-l" || o == "-c") {
      const char* v = value();
      if (!v) { printUsage(argv[0]); return 2; }
      if (o == "-p") prefix = v;
      else if (o == "-a") addresses.push_back(lower(v));
      else if (o == "-l") {
        std::string l = lower(v);
        if (l == "none") logLevel = LogLevel::None;
        else if (l == "error") logLevel = LogLevel::Error;
        else if (l == "warning") logLevel = LogLevel::Warning;
        else if (l == "info") logLevel = LogLevel::Info;
        else if (l == "debug") logLevel = LogLevel::Debug;
        else { printUsage(argv[0]); return 2; }
      }
      else if (o == "-t") scanMs = atoi(v);
      else if (o == "-c") chunk = atoi(v);
      else timeoutMs = atoi(v);
    } else if (o == "-v") {
      verbose = true;
      logLevel = LogLevel::Info;
    } else {
      printUsage(argv[0]);
      return o == "-h" || o == "--help" ? 0 : 2;
    }
  }
  if (i >= argc) { printUsage(argv[0]); return 2; }
  std::vector<std::string> command(argv + i, argv + argc);
  TinyBLEFSLogger.setLevel(logLevel);  // the logger writes to stderr by default

  if (command[0] == "help") { printUsage(argv[0]); return 0; }

  // ---- scan --------------------------------------------------------------
  auto found = SimpleBLEClientTransport::scan(scanMs, prefix);
  auto wanted = [&](SimpleBLE::Peripheral& p) {
    return addresses.empty() ||
           std::find(addresses.begin(), addresses.end(), lower(p.address())) != addresses.end();
  };
  if (command[0] == "scan") {
    for (auto& p : found)
      if (wanted(p)) std::cout << p.address() << "  " << p.identifier() << "  rssi=" << p.rssi() << "\n";
    return 0;
  }

  // ---- connect and mount ---------------------------------------------------
  std::vector<std::unique_ptr<Remote>> remotes;
  Mounts mounts;
  for (auto& p : found) {
    if (!wanted(p)) continue;
    auto r = std::make_unique<Remote>();
    if (!r->transport.connect(p)) {
      std::cerr << "cannot connect to " << p.address() << "\n";
      continue;
    }
    r->client = std::make_unique<FileClient>(r->transport, (uint32_t)timeoutMs);
    if (chunk > 0) r->client->setChunkSize((uint16_t)chunk);
    std::string name = mounts.mount(*r->client);
    if (name.empty()) {
      std::cerr << p.address() << " does not answer as TinyBLEFS server\n";
      continue;
    }
    if (verbose) std::cerr << "mounted " << p.address() << " as /" << name << "\n";
    remotes.push_back(std::move(r));
  }
  if (remotes.empty()) {
    std::cerr << "no TinyBLEFS server found\n";
    return 1;
  }

  Cli cli(mounts);
  if (command[0] != "shell") return cli.run(command);

  // ---- interactive shell ---------------------------------------------------
  std::string line;
  while (true) {
    std::cout << "tinyblefs:" << cli.cwd() << "> " << std::flush;
    if (!std::getline(std::cin, line)) break;
    auto args = Cli::split(line);
    if (args.empty()) continue;
    if (args[0] == "exit" || args[0] == "quit") break;
    cli.run(args);
  }
  return 0;
}
