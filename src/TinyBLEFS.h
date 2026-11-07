#pragma once
// TinyBLEFS: header-only remote file system over BLE.
//
// Client side: FileClient + RemoteFS / Mounts (Arduino File-like API)
// Server side: FileServer + an FsBackend (fs/ArduinoFS.h or
//              fs/StdFilesystem.h, include the one you need)
// BLE adapters (transports/*.h) implement tinyblefs::Transport;
// transports/BLETransport.h selects the one of the current platform.

#include "TinyBLEFS/Client.h"
#include "TinyBLEFS/FsBackend.h"
#include "TinyBLEFS/Logger.h"
#include "TinyBLEFS/Mounts.h"
#include "TinyBLEFS/Protocol.h"
#include "TinyBLEFS/RemoteFS.h"
#include "TinyBLEFS/Server.h"
#include "TinyBLEFS/Transport.h"

// On Arduino the public classes are made available without the tinyblefs::
// prefix. Only these names are imported (not the whole namespace), so that the
// internal names cannot clash with other libraries (e.g. SdFat has an FsFile).
// Define TINYBLEFS_NO_USING_NAMESPACE before including the library to switch this
// off. Adapters and backends do the same for their own classes.
#if defined(ARDUINO) && !defined(TINYBLEFS_NO_USING_NAMESPACE)
using tinyblefs::DirEntry;
using tinyblefs::FileClient;
using tinyblefs::FileServer;
using tinyblefs::FsBackend;
using tinyblefs::FsEntry;
using tinyblefs::LogLevel;
using tinyblefs::Mounts;
using tinyblefs::RemoteFile;
using tinyblefs::RemoteFS;
#endif
