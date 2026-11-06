#pragma once
// Command layer of the tinyblefs command line tool. It only needs a
// tinyblefs::Mounts object (no BLE code), so it can be tested over a loopback
// transport.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "TinyBLEFS.h"

namespace tinyblefs {

class Cli {
 public:
  Cli(Mounts& mounts, std::ostream& out = std::cout, std::ostream& err = std::cerr)
      : m_(mounts), out_(out), err_(err) {}

  const std::string& cwd() const { return cwd_; }

  static const char* usage() {
    return
        "commands (paths are /<mount>/<path>; \"/\" lists the mounts):\n"
        "  mounts                     list mounted servers and their properties\n"
        "  pwd | cd <dir>             working directory (useful in the shell)\n"
        "  ls [-l] [-R] [path...]     list directories\n"
        "  stat <path...>             type and size\n"
        "  cat <file...>              print files\n"
        "  get [-r] <remote> <local>  download file (-r: directory tree)\n"
        "  put [-r] <local> <remote>  upload file (-r: directory tree)\n"
        "  cp [-r] <src> <dst>        copy (also between servers)\n"
        "  mv <src> <dst>             move / rename (also between servers)\n"
        "  rm [-r] <path...>          delete files (-r: directories too)\n"
        "  mkdir [-p] <dir...>        create directories (-p: parents)\n"
        "  rmdir <dir...>             delete empty directories\n"
        "  touch <file...>            create empty file\n"
        "  write <file> <text...>     write text to file (replaces content)\n"
        "  append <file> <text...>    append text to file\n"
        "  help\n";
  }

  /// Splits a command line into arguments (supports "double" and 'single'
  /// quotes and backslash escapes)
  static std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> args;
    std::string cur;
    bool inArg = false;
    char quote = 0;
    for (size_t i = 0; i < line.size(); i++) {
      char c = line[i];
      if (quote) {
        if (c == quote) quote = 0;
        else if (c == '\\' && quote == '"' && i + 1 < line.size()) cur += line[++i];
        else cur += c;
      } else if (c == '"' || c == '\'') {
        quote = c;
        inArg = true;
      } else if (c == '\\' && i + 1 < line.size()) {
        cur += line[++i];
        inArg = true;
      } else if (isspace((unsigned char)c)) {
        if (inArg) args.push_back(cur);
        cur.clear();
        inArg = false;
      } else {
        cur += c;
        inArg = true;
      }
    }
    if (inArg) args.push_back(cur);
    return args;
  }

  /// Executes one command (args[0] is the command name); returns the exit code
  int run(const std::vector<std::string>& args) {
    if (args.empty()) return 0;
    const std::string& cmd = args[0];

    // write / append: everything after the file name is text
    if ((cmd == "write" || cmd == "append") && args.size() >= 2) {
      std::string text;
      for (size_t i = 2; i < args.size(); i++) text += (i > 2 ? " " : "") + args[i];
      return cmdWrite(cmd, args[1], text + "\n");
    }

    std::string flags;
    std::vector<std::string> a;
    for (size_t i = 1; i < args.size(); i++) {
      if (args[i].size() > 1 && args[i][0] == '-') flags += args[i].substr(1);
      else a.push_back(args[i]);
    }
    auto has = [&](char c) { return flags.find(c) != std::string::npos; };

    if (cmd == "help") { out_ << usage(); return 0; }
    if (cmd == "pwd") { out_ << cwd_ << "\n"; return 0; }
    if (cmd == "cd") return cmdCd(a);
    if (cmd == "mounts") return cmdMounts();
    if (cmd == "ls") return cmdLs(has('l'), has('R'), a);
    if (cmd == "stat") return forEach(cmd, a, 1, [&](const std::string& p) { return cmdStat(p); });
    if (cmd == "cat") return forEach(cmd, a, 1, [&](const std::string& p) { return cmdCat(p); });
    if (cmd == "get") return need(cmd, a, 2) ? cmdGet(a[0], a[1], has('r')) : 2;
    if (cmd == "put") return need(cmd, a, 2) ? cmdPut(a[0], a[1], has('r')) : 2;
    if (cmd == "cp") return need(cmd, a, 2) ? cmdCp(a[0], a[1], has('r')) : 2;
    if (cmd == "mv") return need(cmd, a, 2) ? cmdMv(a[0], a[1]) : 2;
    if (cmd == "rm") return forEach(cmd, a, 1, [&](const std::string& p) { return cmdRm(p, has('r')); });
    if (cmd == "mkdir") return forEach(cmd, a, 1, [&](const std::string& p) { return cmdMkdir(p, has('p')); });
    if (cmd == "rmdir") return forEach(cmd, a, 1, [&](const std::string& p) { return cmdRmdir(p); });
    if (cmd == "touch") return forEach(cmd, a, 1, [&](const std::string& p) { return cmdWrite("touch", p, "") == 0; });
    err_ << "unknown command: " << cmd << " (try help)\n";
    return 2;
  }

 private:
  // ---- path helpers ------------------------------------------------------
  /// Absolute, normalised path ("." and ".." resolved against the cwd)
  std::string resolve(const std::string& p) const {
    std::string full = (p.empty() || p[0] != '/') ? cwd_ + "/" + p : p;
    std::vector<std::string> segs;
    size_t i = 0;
    while (i <= full.size()) {
      size_t j = full.find('/', i);
      if (j == std::string::npos) j = full.size();
      std::string s = full.substr(i, j - i);
      if (s == "..") { if (!segs.empty()) segs.pop_back(); }
      else if (!s.empty() && s != ".") segs.push_back(s);
      i = j + 1;
    }
    std::string r;
    for (auto& s : segs) r += "/" + s;
    return r.empty() ? "/" : r;
  }
  static std::string join(const std::string& dir, const std::string& name) {
    return dir == "/" ? "/" + name : dir + "/" + name;
  }
  static std::string base(const std::string& p) {
    size_t k = p.find_last_of('/');
    return k == std::string::npos ? p : p.substr(k + 1);
  }
  bool stat(const std::string& p, bool& isDir, uint32_t& size) {
    return m_.stat(p.c_str(), isDir, size);
  }
  bool isDir(const std::string& p) {
    bool d = false;
    uint32_t s;
    return stat(p, d, s) && d;
  }
  /// when dst is an existing directory the source is copied into it
  std::string target(const std::string& dst, const std::string& srcName) {
    return isDir(dst) ? join(dst, srcName) : dst;
  }
  bool need(const std::string& cmd, const std::vector<std::string>& a, size_t n) {
    if (a.size() == n) return true;
    err_ << cmd << ": expected " << n << " argument(s)\n";
    return false;
  }
  template <class F>
  int forEach(const std::string& cmd, const std::vector<std::string>& a,
              size_t min, F f) {
    if (a.size() < min) { err_ << cmd << ": missing argument\n"; return 2; }
    int rc = 0;
    for (auto& p : a) if (!f(p)) rc = 1;
    return rc;
  }
  bool fail(const std::string& cmd, const std::string& path, const char* what) {
    err_ << cmd << ": " << path << ": " << what << "\n";
    return false;
  }

  // ---- commands ----------------------------------------------------------
  int cmdCd(const std::vector<std::string>& a) {
    std::string p = a.empty() ? "/" : resolve(a[0]);
    if (!isDir(p)) { fail("cd", p, "not a directory"); return 1; }
    cwd_ = p;
    return 0;
  }

  int cmdMounts() {
    for (auto& name : m_.names()) {
      ServerInfo si;
      RemoteFS* fs = m_.get(name.c_str());
      if (fs && fs->client().info(si) == Status::Ok) {
        out_ << "/" << name << "  server=" << si.name << " protocol=" << (int)si.version
             << " max-open-files=" << (int)si.maxHandles
             << " access=" << (si.readOnly ? "read-only" : "read-write") << "\n";
      } else {
        out_ << "/" << name << "  (not responding)\n";
      }
    }
    return 0;
  }

  void printEntry(const std::string& name, bool dir, uint32_t size, bool longFmt) {
    if (longFmt) out_ << (dir ? 'd' : '-') << " " << std::setw(10) << size << " " << name << "\n";
    else out_ << name << (dir ? "/" : "") << "\n";
  }

  int cmdLs(bool l, bool r, std::vector<std::string> paths) {
    if (paths.empty()) paths.push_back(".");
    int rc = 0;
    for (size_t i = 0; i < paths.size(); i++) {
      if (!lsPath(resolve(paths[i]), l, r, paths.size() > 1 || r)) rc = 1;
      if (i + 1 < paths.size()) out_ << "\n";
    }
    return rc;
  }
  bool lsPath(const std::string& p, bool l, bool r, bool header) {
    bool dir;
    uint32_t size;
    if (!stat(p, dir, size)) return fail("ls", p, "not found");
    if (!dir) { printEntry(base(p), false, size, l); return true; }
    std::vector<DirEntry> es;
    if (!m_.list(p.c_str(), es)) return fail("ls", p, "cannot read directory");
    std::sort(es.begin(), es.end(), [](const DirEntry& x, const DirEntry& y) { return x.name < y.name; });
    if (header) out_ << p << ":\n";
    for (auto& e : es) printEntry(e.name, e.isDir, e.size, l);
    if (r)
      for (auto& e : es)
        if (e.isDir) { out_ << "\n"; lsPath(join(p, e.name), l, r, true); }
    return true;
  }

  bool cmdStat(const std::string& arg) {
    std::string p = resolve(arg);
    bool dir;
    uint32_t size;
    if (!stat(p, dir, size)) return fail("stat", p, "not found");
    out_ << p << ": " << (dir ? "directory" : "file") << ", " << size << " bytes\n";
    return true;
  }

  bool cmdCat(const std::string& arg) {
    std::string p = resolve(arg);
    RemoteFile f = m_.open(p.c_str());
    if (!f || f.isDirectory()) return fail("cat", p, "cannot read file");
    uint8_t buf[512];
    for (size_t n; (n = f.read(buf, sizeof(buf))) > 0;) out_.write((const char*)buf, n);
    if (f.position() < f.size()) return fail("cat", p, "read failed");
    return true;
  }

  int cmdWrite(const std::string& cmd, const std::string& arg, const std::string& text) {
    std::string p = resolve(arg);
    // touch must not truncate an existing file, so it appends nothing
    RemoteFile f = m_.open(p.c_str(), cmd == "write" ? "w" : "a");
    if (!f) { fail(cmd, p, "cannot open file for writing"); return 1; }
    if (f.write((const uint8_t*)text.data(), text.size()) != text.size()) {
      fail(cmd, p, "write failed");
      return 1;
    }
    return 0;
  }

  bool cmdMkdir(const std::string& arg, bool parents) {
    std::string p = resolve(arg);
    if (!parents) return m_.mkdir(p.c_str()) || fail("mkdir", p, "cannot create directory");
    // create all missing components below the mount
    std::string cur;
    size_t i = 1;
    while (i <= p.size()) {
      size_t j = p.find('/', i);
      if (j == std::string::npos) j = p.size();
      cur = p.substr(0, j);
      i = j + 1;
      if (std::count(cur.begin(), cur.end(), '/') < 2) continue;  // the mount itself
      if (!isDir(cur) && !m_.mkdir(cur.c_str())) return fail("mkdir", cur, "cannot create directory");
    }
    return true;
  }

  bool cmdRmdir(const std::string& arg) {
    std::string p = resolve(arg);
    return m_.rmdir(p.c_str()) || fail("rmdir", p, "cannot remove (not empty?)");
  }

  bool removeTree(const std::string& p) {
    std::vector<DirEntry> es;
    if (!m_.list(p.c_str(), es)) return false;
    for (auto& e : es) {
      std::string c = join(p, e.name);
      if (!(e.isDir ? removeTree(c) : m_.remove(c.c_str()))) return false;
    }
    return m_.rmdir(p.c_str());
  }
  bool cmdRm(const std::string& arg, bool recursive) {
    std::string p = resolve(arg);
    bool dir;
    uint32_t size;
    if (!stat(p, dir, size)) return fail("rm", p, "not found");
    if (dir) {
      if (!recursive) return fail("rm", p, "is a directory (use -r)");
      return removeTree(p) || fail("rm", p, "cannot remove directory");
    }
    return m_.remove(p.c_str()) || fail("rm", p, "cannot remove file");
  }

  int cmdCp(const std::string& src, const std::string& dst, bool recursive) {
    std::string s = resolve(src), d = resolve(dst);
    if (isDir(s) && !recursive) { fail("cp", s, "is a directory (use -r)"); return 1; }
    if (!m_.exists(s.c_str())) { fail("cp", s, "not found"); return 1; }
    if (!m_.copy(s.c_str(), target(d, base(s)).c_str())) { fail("cp", s, "copy failed"); return 1; }
    return 0;
  }

  int cmdMv(const std::string& src, const std::string& dst) {
    std::string s = resolve(src), d = resolve(dst);
    if (!m_.exists(s.c_str())) { fail("mv", s, "not found"); return 1; }
    d = target(d, base(s));
    if (m_.rename(s.c_str(), d.c_str())) return 0;
    // different servers: copy, then delete the source
    if (!m_.copy(s.c_str(), d.c_str())) { fail("mv", s, "copy failed"); return 1; }
    bool dir = isDir(s);
    if (!(dir ? removeTree(s) : m_.remove(s.c_str()))) { fail("mv", s, "copied, but cannot remove source"); return 1; }
    return 0;
  }

  // ---- transfers between the remote and the local file system -------------
  bool putFile(const std::filesystem::path& local, const std::string& remote) {
    std::ifstream in(local, std::ios::binary);
    if (!in) return fail("put", local.string(), "cannot read local file");
    RemoteFile out = m_.open(remote.c_str(), "w");
    if (!out) return fail("put", remote, "cannot open for writing");
    std::vector<char> buf(4096);
    while (in) {
      in.read(buf.data(), buf.size());
      size_t n = (size_t)in.gcount();
      if (n && out.write((const uint8_t*)buf.data(), n) != n) return fail("put", remote, "write failed");
    }
    return true;
  }
  bool putTree(const std::filesystem::path& local, const std::string& remote) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(local)) return putFile(local, remote);
    if (!isDir(remote) && !m_.mkdir(remote.c_str())) return fail("put", remote, "cannot create directory");
    for (auto& e : fs::directory_iterator(local))
      if (!putTree(e.path(), join(remote, e.path().filename().string()))) return false;
    return true;
  }
  int cmdPut(const std::string& local, const std::string& dst, bool recursive) {
    namespace fs = std::filesystem;
    fs::path l = local;
    if (!fs::exists(l)) { fail("put", local, "no such local file"); return 1; }
    if (fs::is_directory(l) && !recursive) { fail("put", local, "is a directory (use -r)"); return 1; }
    std::string d = resolve(dst);
    return putTree(l, target(d, l.filename().string())) ? 0 : 1;
  }

  bool getFile(const std::string& remote, const std::filesystem::path& local) {
    RemoteFile in = m_.open(remote.c_str());
    if (!in || in.isDirectory()) return fail("get", remote, "cannot read file");
    std::ofstream out(local, std::ios::binary);
    if (!out) return fail("get", local.string(), "cannot write local file");
    uint8_t buf[512];
    for (size_t n; (n = in.read(buf, sizeof(buf))) > 0;) out.write((const char*)buf, n);
    // read() returns 0 both at the end and on a failed request
    if (in.position() < in.size()) return fail("get", remote, "read failed");
    return (bool)out;
  }
  bool getTree(const std::string& remote, const std::filesystem::path& local) {
    namespace fs = std::filesystem;
    if (!isDir(remote)) return getFile(remote, local);
    std::error_code ec;
    fs::create_directories(local, ec);
    std::vector<DirEntry> es;
    if (!m_.list(remote.c_str(), es)) return fail("get", remote, "cannot read directory");
    for (auto& e : es)
      if (!getTree(join(remote, e.name), local / e.name)) return false;
    return true;
  }
  int cmdGet(const std::string& src, const std::string& local, bool recursive) {
    namespace fs = std::filesystem;
    std::string s = resolve(src);
    if (!m_.exists(s.c_str())) { fail("get", s, "not found"); return 1; }
    if (isDir(s) && !recursive) { fail("get", s, "is a directory (use -r)"); return 1; }
    fs::path l = local;
    if (fs::is_directory(l)) l /= base(s);
    return getTree(s, l) ? 0 : 1;
  }

  Mounts& m_;
  std::ostream& out_;
  std::ostream& err_;
  std::string cwd_ = "/";
};

}  // namespace tinyblefs
