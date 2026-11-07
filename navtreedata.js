/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "TinyBLEFS", "index.html", [
    [ "Tiny BLE FS", "index.html", "index" ],
    [ "Client API", "md_docs_2client-api.html", [
      [ "Connecting", "md_docs_2client-api.html#autotoc_md11", null ],
      [ "Mounts", "md_docs_2client-api.html#autotoc_md12", null ],
      [ "File", "md_docs_2client-api.html#autotoc_md13", null ],
      [ "Errors and locking", "md_docs_2client-api.html#autotoc_md14", null ]
    ] ],
    [ "File systems and platforms", "md_docs_2filesystems.html", [
      [ "Verification status", "md_docs_2filesystems.html#autotoc_md16", null ],
      [ "ESP32", "md_docs_2filesystems.html#autotoc_md18", [
        [ "LittleFS", "md_docs_2filesystems.html#autotoc_md19", null ],
        [ "SPIFFS", "md_docs_2filesystems.html#autotoc_md20", null ],
        [ "FFat (FAT in internal flash)", "md_docs_2filesystems.html#autotoc_md21", null ],
        [ "SD card over SPI (<tt>SD</tt>)", "md_docs_2filesystems.html#autotoc_md22", null ],
        [ "SD card over SD_MMC (SDIO)", "md_docs_2filesystems.html#autotoc_md23", null ]
      ] ],
      [ "Raspberry Pi Pico W / Pico 2 W (arduino-pico)", "md_docs_2filesystems.html#autotoc_md25", [
        [ "LittleFS (internal flash)", "md_docs_2filesystems.html#autotoc_md26", null ],
        [ "FatFS (internal flash)", "md_docs_2filesystems.html#autotoc_md27", null ],
        [ "SD card (<tt>SDFS</tt>)", "md_docs_2filesystems.html#autotoc_md28", null ]
      ] ],
      [ "TinyFATFS (FAT on any storage)", "md_docs_2filesystems.html#autotoc_md30", [
        [ "RAM disk (no hardware)", "md_docs_2filesystems.html#autotoc_md31", null ],
        [ "SD card over SPI", "md_docs_2filesystems.html#autotoc_md32", null ],
        [ "SD card over SD_MMC", "md_docs_2filesystems.html#autotoc_md33", null ],
        [ "Notes", "md_docs_2filesystems.html#autotoc_md34", null ]
      ] ],
      [ "Linux, macOS, Windows: a directory", "md_docs_2filesystems.html#autotoc_md36", null ],
      [ "Your own file system", "md_docs_2filesystems.html#autotoc_md38", null ],
      [ "Limits that apply to all file systems", "md_docs_2filesystems.html#autotoc_md39", null ]
    ] ],
    [ "Getting started", "md_docs_2getting-started.html", [
      [ "Installation", "md_docs_2getting-started.html#autotoc_md41", null ],
      [ "Requirements", "md_docs_2getting-started.html#autotoc_md42", null ],
      [ "How it works", "md_docs_2getting-started.html#autotoc_md43", null ],
      [ "Examples", "md_docs_2getting-started.html#autotoc_md44", null ]
    ] ],
    [ "Logging", "md_docs_2logging.html", [
      [ "Own messages", "md_docs_2logging.html#autotoc_md46", null ]
    ] ],
    [ "Protocol", "md_docs_2protocol.html", [
      [ "GATT", "md_docs_2protocol.html#autotoc_md48", null ],
      [ "Message", "md_docs_2protocol.html#autotoc_md49", null ],
      [ "Operations", "md_docs_2protocol.html#autotoc_md50", null ],
      [ "Status codes", "md_docs_2protocol.html#autotoc_md51", null ],
      [ "Locking", "md_docs_2protocol.html#autotoc_md52", null ],
      [ "Limits", "md_docs_2protocol.html#autotoc_md53", null ]
    ] ],
    [ "Server API", "md_docs_2server-api.html", [
      [ "File system backends", "md_docs_2server-api.html#autotoc_md55", null ],
      [ "Security", "md_docs_2server-api.html#autotoc_md56", null ],
      [ "File locking", "md_docs_2server-api.html#autotoc_md57", null ]
    ] ],
    [ "Several clients", "md_docs_2sessions.html", [
      [ "Sessions", "md_docs_2sessions.html#autotoc_md59", null ],
      [ "Limits", "md_docs_2sessions.html#autotoc_md60", null ],
      [ "Wire format", "md_docs_2sessions.html#autotoc_md61", null ]
    ] ],
    [ "Test results", "md_docs_2test-results.html", [
      [ "PC tests", "md_docs_2test-results.html#autotoc_md63", null ],
      [ "Compile checks", "md_docs_2test-results.html#autotoc_md64", null ],
      [ "Hardware tests", "md_docs_2test-results.html#autotoc_md65", null ],
      [ "Performance matrix", "md_docs_2test-results.html#autotoc_md66", null ],
      [ "Open problems", "md_docs_2test-results.html#autotoc_md67", null ],
      [ "Not tested with the current code", "md_docs_2test-results.html#autotoc_md68", null ]
    ] ],
    [ "Linux tools", "md_docs_2tools.html", [
      [ "Build", "md_docs_2tools.html#autotoc_md70", null ],
      [ "Usage", "md_docs_2tools.html#autotoc_md71", null ],
      [ "Commands", "md_docs_2tools.html#autotoc_md72", null ],
      [ "Example", "md_docs_2tools.html#autotoc_md73", null ],
      [ "Bluetooth settings", "md_docs_2tools.html#autotoc_md74", null ],
      [ "Mounting as a directory: tinyblefs-fuse", "md_docs_2tools.html#autotoc_md75", [
        [ "Requirements", "md_docs_2tools.html#autotoc_md76", null ],
        [ "Usage", "md_docs_2tools.html#autotoc_md77", null ],
        [ "Example", "md_docs_2tools.html#autotoc_md78", null ],
        [ "How it behaves", "md_docs_2tools.html#autotoc_md79", null ],
        [ "Limits", "md_docs_2tools.html#autotoc_md80", null ]
      ] ],
      [ "Server: tinyblefs-server", "md_docs_2tools.html#autotoc_md81", null ]
    ] ],
    [ "Transports", "md_docs_2transports.html", [
      [ "Common transport names", "md_docs_2transports.html#autotoc_md83", null ],
      [ "Adapters", "md_docs_2transports.html#autotoc_md84", null ],
      [ "Writing an adapter", "md_docs_2transports.html#autotoc_md85", null ],
      [ "Performance", "md_docs_2transports.html#autotoc_md86", null ]
    ] ],
    [ "Namespaces", "namespaces.html", [
      [ "Namespace List", "namespaces.html", "namespaces_dup" ],
      [ "Namespace Members", "namespacemembers.html", [
        [ "All", "namespacemembers.html", null ],
        [ "Functions", "namespacemembers_func.html", null ],
        [ "Variables", "namespacemembers_vars.html", null ],
        [ "Typedefs", "namespacemembers_type.html", null ],
        [ "Enumerations", "namespacemembers_enum.html", null ],
        [ "Enumerator", "namespacemembers_eval.html", null ]
      ] ]
    ] ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Hierarchy", "hierarchy.html", "hierarchy" ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", "functions_dup" ],
        [ "Functions", "functions_func.html", "functions_func" ],
        [ "Variables", "functions_vars.html", null ],
        [ "Typedefs", "functions_type.html", null ],
        [ "Related Symbols", "functions_rela.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", null ],
        [ "Variables", "globals_vars.html", null ],
        [ "Macros", "globals_defs.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"ArduinoBLE_8h.html",
"classtinyblefs_1_1FileClient.html#afa3f6f85c8f2906bb7677ca71f875e95",
"classtinyblefs_1_1RemoteFile.html#a067ceb683ca0b0194a3198fbc69e86ca",
"functions_func_f.html"
];

var SYNCONMSG = 'click to disable panel synchronisation';
var SYNCOFFMSG = 'click to enable panel synchronisation';