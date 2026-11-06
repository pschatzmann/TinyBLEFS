# Logging

The global object `TinyBLEFSLogger` (class `tinyblefs::LoggerClass`, `Logger.h`) logs all errors and warnings of the library, with levels and a configurable output.

```cpp
TinyBLEFSLogger.begin(Serial, LogLevel::Debug);   // output + level
TinyBLEFSLogger.setLevel(LogLevel::Warning);      // change level
TinyBLEFSLogger.setOutput(myPrint);                       // any Arduino Print
TinyBLEFSLogger.setTimestamps(false);                     // default: [ms] prefix
TinyBLEFSLogger.end();                                    // no output
```

| Level | Used for |
|---|---|
| `Error` | Something is broken: BLE start failed, file system or I/O errors, link lost |
| `Warning` | Operation failed: denied, timeout, cannot open, dropped packets, unknown mount, bad path |
| `Info` | Connections, mounts, advertising |
| `Debug` | Every request and answer, normal "not found" results |
| `None` | Silent |

On Arduino boards **nothing is printed until an output is defined** (default level `Warning`). On Linux the default output is `stderr`.

Output format: `[12345] W server: open failed: denied`

## Own messages

```cpp
TINYBLEFS_LOGI("app", "value %d", x);      // also LOGE / LOGW / LOGD
TinyBLEFSLogger.log(LogLevel::Info, "app", "value %d", x);
```

Macro arguments are only evaluated if the level is enabled. Lines are limited to about 160 characters.

On desktop systems `tinyblefs::LogPrint` is a small stand-in for Arduino's `Print`; derive from it to redirect the output.
