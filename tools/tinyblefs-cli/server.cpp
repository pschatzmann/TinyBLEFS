// tinyblefs-server: shares a directory of this computer over BLE (Linux, BlueZ).
//
//   tinyblefs-server [options] <directory>
//
// options:
//   -n <name>     name announced to clients = mount name (default: host name)
//   -r            read-only
//   -f <n>        max simultaneously open files (default 8)
//   -P <bytes>    bytes per notification (default 20, see docs/transports.md)
//   -D <ms>       pause after every notification (default 10)
//   -l <level>    log level on stderr: none, error, warning, info (default), debug
//
// The BLE device name is the alias of the Bluetooth adapter:
//   bluetoothctl system-alias "TinyFS PC"
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "TinyBLEFS.h"
#include "TinyBLEFS/transports/SimpleBLE.h"
#include "TinyBLEFS/fs/StdFilesystem.h"

namespace {
std::atomic_bool running{true};
void onSignal(int) { running = false; }

void usage(const char* prog) {
  std::cerr << "usage: " << prog
            << " [-n name] [-r] [-f max-open-files] [-P packet-bytes] [-D pacing-ms]"
               " [-l level] <directory>\n";
}
}  // namespace

int main(int argc, char** argv) {
  std::string name;
  bool readOnly = false;
  size_t maxFiles = 8, packet = 20;
  uint32_t pacing = 10;
  tinyblefs::LogLevel level = tinyblefs::LogLevel::Info;

  int i = 1;
  for (; i < argc && argv[i][0] == '-'; i++) {
    std::string o = argv[i];
    auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (o == "-r") {
      readOnly = true;
    } else if (o == "-n" || o == "-f" || o == "-P" || o == "-D" || o == "-l") {
      const char* v = value();
      if (!v) { usage(argv[0]); return 2; }
      if (o == "-n") name = v;
      else if (o == "-f") maxFiles = (size_t)atoi(v);
      else if (o == "-P") packet = (size_t)atoi(v);
      else if (o == "-D") pacing = (uint32_t)atoi(v);
      else {
        std::string l = v;
        if (l == "none") level = tinyblefs::LogLevel::None;
        else if (l == "error") level = tinyblefs::LogLevel::Error;
        else if (l == "warning") level = tinyblefs::LogLevel::Warning;
        else if (l == "info") level = tinyblefs::LogLevel::Info;
        else if (l == "debug") level = tinyblefs::LogLevel::Debug;
        else { usage(argv[0]); return 2; }
      }
    } else {
      usage(argv[0]);
      return o == "-h" || o == "--help" ? 0 : 2;
    }
  }
  if (i + 1 != argc) { usage(argv[0]); return 2; }

  std::error_code ec;
  std::filesystem::path root = std::filesystem::canonical(argv[i], ec);
  if (ec || !std::filesystem::is_directory(root)) {
    std::cerr << argv[i] << ": not a directory\n";
    return 1;
  }
  if (name.empty()) {
    char host[64] = {};
    gethostname(host, sizeof(host) - 1);
    name = host[0] ? host : "tinyblefs";
  }
  TinyBLEFSLogger.setLevel(level);

  // the backend works with absolute paths; the server exposes `root` as "/"
  tinyblefs::StdFilesystemBackend backend;
  tinyblefs::SimpleBLEServerTransport transport;
  tinyblefs::FileServer server(backend, transport, name.c_str(), root.c_str(), 1, maxFiles);
  server.setReadOnly(readOnly);
  transport.setPacketSize(packet);
  transport.setPacingMs(pacing);

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  if (!transport.begin()) {
    std::cerr << "cannot start the BLE server (is bluetoothd running and the adapter on?)\n";
    return 1;
  }
  std::cerr << "sharing " << root.string() << " as /" << name
            << (readOnly ? " (read-only)" : "") << ", press Ctrl+C to stop\n";

  while (running) {
    if (!server.poll()) usleep(1000);
  }
  transport.end();
  return 0;
}
