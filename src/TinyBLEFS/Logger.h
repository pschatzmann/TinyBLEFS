#pragma once
// Logging for TinyBLEFS with log levels and a configurable output.
//
//   TinyBLEFSLogger.begin(Serial, LogLevel::Debug);   // Arduino
//   TinyBLEFSLogger.setLevel(LogLevel::Warning);      // change level
//   TinyBLEFSLogger.setOutput(otherPrint);                    // change output
//
// On Arduino boards nothing is printed until an output is defined. On
// desktop systems the default output is stderr.
//
// Library code logs with TINYBLEFS_LOGE / LOGW / LOGI / LOGD(tag, fmt, ...); the
// arguments are only evaluated when the level is enabled. Applications can
// log with the same macros or with TinyBLEFSLogger.log(...).

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Platform.h"

namespace tinyblefs {

#if defined(ARDUINO)
/// Output of the logger: any Arduino Print (Serial, a file, ...)
using LogPrint = ::Print;
#else
/// Minimal replacement of Arduino's Print for desktop systems
class LogPrint {
 public:
  virtual ~LogPrint() = default;
  virtual size_t write(const uint8_t* data, size_t len) = 0;
  size_t write(uint8_t c) { return write(&c, 1); }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t println(const char* s) { return print(s) + print("\n"); }
};
/// Writes to stderr (default output on desktop systems)
class StderrPrint : public LogPrint {
 public:
  size_t write(const uint8_t* data, size_t len) override {
    return fwrite(data, 1, len, stderr);
  }
};
#endif

enum class LogLevel : uint8_t { None = 0, Error, Warning, Info, Debug };

class LoggerClass {
 public:
  /// Defines the output and the level in one call
  void begin(LogPrint& out, LogLevel level = LogLevel::Warning) {
    out_ = &out;
    level_ = level;
  }
  void setOutput(LogPrint& out) { out_ = &out; }
  /// Disables the output (nothing is logged)
  void end() { out_ = nullptr; }
  void setLevel(LogLevel level) { level_ = level; }
  LogLevel level() const { return level_; }
  /// Prefix every line with the time in ms (default: on)
  void setTimestamps(bool on) { timestamps_ = on; }

  bool isEnabled(LogLevel level) const {
    return out_ != nullptr && level != LogLevel::None && level <= level_;
  }

#if defined(__GNUC__)
  __attribute__((format(printf, 4, 5)))
#endif
  void log(LogLevel level, const char* tag, const char* fmt, ...) {
    if (!isEnabled(level)) return;
    char msg[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char head[48];
    static const char kLetter[] = "-EWID";
    if (timestamps_)
      snprintf(head, sizeof(head), "[%lu] %c %s: ", (unsigned long)nowMs(),
               kLetter[(int)level], tag);
    else
      snprintf(head, sizeof(head), "%c %s: ", kLetter[(int)level], tag);
    out_->print(head);
    out_->print(msg);
    out_->print("\n");
  }

 private:
  LogPrint* out_ =
#if defined(ARDUINO)
      nullptr;
#else
      &defaultOut();
  static StderrPrint& defaultOut() {
    static StderrPrint p;
    return p;
  }
#endif
  LogLevel level_ = LogLevel::Warning;
  bool timestamps_ = true;
};

namespace detail {
inline LoggerClass& loggerInstance() {
  static LoggerClass logger;
  return logger;
}
}  // namespace detail

}  // namespace tinyblefs

/// The logger object of the library (one instance shared by all files)
static tinyblefs::LoggerClass& TinyBLEFSLogger = tinyblefs::detail::loggerInstance();

#define TINYBLEFS_LOG(level, tag, ...)                                  \
  do {                                                               \
    if (::TinyBLEFSLogger.isEnabled(level))                     \
      ::TinyBLEFSLogger.log(level, tag, __VA_ARGS__);           \
  } while (0)
#define TINYBLEFS_LOGE(tag, ...) TINYBLEFS_LOG(::tinyblefs::LogLevel::Error, tag, __VA_ARGS__)
#define TINYBLEFS_LOGW(tag, ...) TINYBLEFS_LOG(::tinyblefs::LogLevel::Warning, tag, __VA_ARGS__)
#define TINYBLEFS_LOGI(tag, ...) TINYBLEFS_LOG(::tinyblefs::LogLevel::Info, tag, __VA_ARGS__)
#define TINYBLEFS_LOGD(tag, ...) TINYBLEFS_LOG(::tinyblefs::LogLevel::Debug, tag, __VA_ARGS__)
