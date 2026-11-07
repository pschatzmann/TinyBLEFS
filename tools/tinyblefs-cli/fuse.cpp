// tinyblefs-fuse: mounts TinyBLEFS servers as a Linux directory (libfuse3, via SimpleBLE).
//
//   tinyblefs-fuse [options] <mountpoint> [fuse options]
//
// The mountpoint lists the servers in range as subdirectories (/<name>), so the
// usual file commands (ls, cat, cp, rsync, editors ...) work. Unmount with
//   fusermount3 -u <mountpoint>
//
// options (same meaning as for tinyblefs):
//   --prefix=<prefix>    only servers whose BLE device name starts with prefix
//   --address=<address>  only the server with this BLE address (repeatable)
//   --scan-time=<ms>     scan time (default 5000)
//   --timeout=<ms>       request timeout (default 10000)
//   --chunk=<bytes>      data per read / write request (default 400)
//   --log=<level>        none, error (default), warning, info, debug
// fuse options: -f (foreground), -d (debug), -o allow_other ... (see --help)
//
// The BLE scan runs when the file system starts (after it went into the
// background), so the first access can take a few seconds.
#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <fuse_opt.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

#include "FuseFs.h"
#include "TinyBLEFS/transports/SimpleBLE.h"

using namespace tinyblefs;

namespace {

struct Options {
  std::string prefix;
  std::vector<std::string> addresses;
  int scanMs = 5000, timeoutMs = 10000, chunk = 0;
  LogLevel level = LogLevel::Error;
  bool bad = false;
};

struct Remote {
  SimpleBLEClientTransport transport;
  std::unique_ptr<FileClient> client;
};

struct State {
  Options opt;
  Mounts mounts;
  std::vector<std::unique_ptr<Remote>> remotes;
  FuseFs* fs = nullptr;
  ~State() {
    mounts = Mounts();  // before the clients and transports go
    delete fs;
  }
};

enum { K_PREFIX = 1, K_ADDRESS, K_SCAN, K_TIMEOUT, K_CHUNK, K_LOG, K_HELP };

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return tolower(c); });
  return s;
}

void printUsage(const char* prog) {
  std::cerr << "usage: " << prog << " [options] <mountpoint> [fuse options]\n\n"
            << "options:\n"
               "  --prefix=<prefix>    only servers whose BLE name starts with prefix\n"
               "  --address=<address>  only this BLE address (repeatable)\n"
               "  --scan-time=<ms>     scan time (5000)\n"
               "  --timeout=<ms>       request timeout (10000)\n"
               "  --chunk=<bytes>      data per read / write request (400)\n"
               "  --log=<level>        none, error (default), warning, info, debug\n"
               "  -f                   stay in the foreground\n"
               "  -d                   fuse debug output (implies -f)\n\n"
               "unmount: fusermount3 -u <mountpoint>\n";
}

int optProc(void* data, const char* arg, int key, fuse_args*) {
  Options& o = *static_cast<Options*>(data);
  const char* v = strchr(arg, '=');
  if (v) v++;
  switch (key) {
    case K_PREFIX: o.prefix = v; return 0;
    case K_ADDRESS: o.addresses.push_back(lower(v)); return 0;
    case K_SCAN: o.scanMs = atoi(v); return 0;
    case K_TIMEOUT: o.timeoutMs = atoi(v); return 0;
    case K_CHUNK: o.chunk = atoi(v); return 0;
    case K_LOG: {
      std::string l = lower(v);
      if (l == "none") o.level = LogLevel::None;
      else if (l == "error") o.level = LogLevel::Error;
      else if (l == "warning") o.level = LogLevel::Warning;
      else if (l == "info") o.level = LogLevel::Info;
      else if (l == "debug") o.level = LogLevel::Debug;
      else o.bad = true;
      return 0;
    }
  }
  return 1;  // keep for fuse (mountpoint, -f, -o ...)
}

// Scans, connects and mounts the servers; runs inside the fuse process
bool connectAll(State& st) {
  const Options& o = st.opt;
  auto found = SimpleBLEClientTransport::scan(o.scanMs, o.prefix);
  for (auto& p : found) {
    if (!o.addresses.empty() &&
        std::find(o.addresses.begin(), o.addresses.end(), lower(p.address())) == o.addresses.end())
      continue;
    auto r = std::make_unique<Remote>();
    if (!r->transport.connect(p)) {
      std::cerr << "cannot connect to " << p.address() << "\n";
      continue;
    }
    r->client = std::make_unique<FileClient>(r->transport, (uint32_t)o.timeoutMs);
    if (o.chunk > 0) r->client->setChunkSize((uint16_t)o.chunk);
    std::string name = st.mounts.mount(*r->client);
    if (name.empty()) {
      std::cerr << p.address() << " does not answer as TinyBLEFS server\n";
      continue;
    }
    st.remotes.push_back(std::move(r));
  }
  return !st.remotes.empty();
}

}  // namespace

int main(int argc, char** argv) {
  State state;
  static const fuse_opt spec[] = {
      FUSE_OPT_KEY("--prefix=", K_PREFIX), FUSE_OPT_KEY("--address=", K_ADDRESS),
      FUSE_OPT_KEY("--scan-time=", K_SCAN), FUSE_OPT_KEY("--timeout=", K_TIMEOUT),
      FUSE_OPT_KEY("--chunk=", K_CHUNK),    FUSE_OPT_KEY("--log=", K_LOG),
      FUSE_OPT_END};
  fuse_args args = FUSE_ARGS_INIT(argc, argv);
  if (fuse_opt_parse(&args, &state.opt, spec, optProc) == -1 || state.opt.bad) {
    printUsage(argv[0]);
    return 2;
  }
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      printUsage(argv[0]);
      return 0;
    }
  TinyBLEFSLogger.setLevel(state.opt.level);

  state.fs = new FuseFs(state.mounts);
  fuse_opt_add_arg(&args, "-s");  // single threaded: one request in flight per client

  static fuse_operations ops;
  ops = FuseFs::operations();
  ops.init = [](fuse_conn_info*, fuse_config* cfg) -> void* {
    cfg->attr_timeout = 2.0;
    cfg->entry_timeout = 2.0;
    cfg->negative_timeout = 1.0;
    // BLE must be used after the fork into the background, so connect here
    State* st = static_cast<State*>(fuse_get_context()->private_data);
    if (!connectAll(*st)) {
      std::cerr << "no TinyBLEFS server found\n";
      fuse_exit(fuse_get_context()->fuse);
    }
    return st->fs;
  };
  // the operations find the FuseFs through private_data: hand it over after init
  // (init replaces it with the FuseFs*, see FuseFs.h); until then no request runs
  int rc = fuse_main(args.argc, args.argv, &ops, &state);
  fuse_opt_free_args(&args);
  return rc;
}
