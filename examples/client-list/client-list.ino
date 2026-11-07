// Finds all TinyBLEFS servers in range, mounts each of them as "/<name>" (the
// name is provided by the server) and lists / reads / writes files on them.
//
// Runs on every supported board: TinyBLEFS/adapters/BLETransport.h selects the BLE
// adapter of the platform (ESP32: NimBLE, Pico W: core BLE library, other
// boards: ArduinoBLE).
//
// Limits of the number of simultaneous servers:
// - ESP32: NimBLE configuration (CONFIG_BT_NIMBLE_MAX_CONNECTIONS, default 3)
// - Pico W: the core's BLE library has one client, so only ONE server
// - Pico W: select Tools -> IP/Bluetooth Stack -> "IPv4 + Bluetooth"
#include <memory>
#include <vector>

#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/BLETransport.h"

// One transport + client per remote server. They must stay alive as long as
// the server is mounted, so they are heap allocated and never moved.
struct Remote {
  BLEClientTransport transport;
  FileClient client{transport, 5000};
};
std::vector<std::unique_ptr<Remote>> remotes;
Mounts mounts;

void listDir(const std::string& path) {
  RemoteFile dir = mounts.open(path.c_str());
  for (RemoteFile e = dir.openNextFile(); e; e = dir.openNextFile()) {
    Serial.print(e.isDirectory() ? "  d " : "  - ");
    Serial.print(e.name());
    Serial.print(" ");
    Serial.println(e.size());
  }
}

void setup() {
  Serial.begin(115200);
  for (uint32_t t = millis(); !Serial && millis() - t < 3000;) delay(10);
  Serial.print("TinyBLEFS client using ");
  Serial.println(TINYBLEFS_TRANSPORT_NAME);

  // only servers whose BLE device name starts with "TinyFS" ("" = all)
  auto devices = BLEClientTransport::scan(5000, "TinyFS");
  Serial.print("found ");
  Serial.print(devices.size());
  Serial.println(" server(s)");

  for (auto& device : devices) {
    auto remote = std::make_unique<Remote>();
    // ArduinoBLE: 244 needs an ATT MTU of 247 on both sides (default: 20)
    // remote->transport.setPacketSize(244);
    if (!remote->transport.connect(device)) {
      Serial.println("connect failed");
      continue;
    }
    std::string name = mounts.mount(remote->client);
    if (name.empty()) {
      Serial.println("server not responding");
      continue;
    }
    Serial.print("mounted /");
    Serial.println(name.c_str());
    remotes.push_back(std::move(remote));
  }

  // work with every mounted server
  for (const std::string& name : mounts.names()) {
    std::string root = "/" + name;
    Serial.print(root.c_str());
    Serial.println(":");
    listDir(root);

    std::string file = root + "/hello.txt";
    if (RemoteFile f = mounts.open(file.c_str())) {
      while (f.available()) Serial.write(f.read());
      f.close();
    }

    std::string out = root + "/from-client.txt";
    if (RemoteFile w = mounts.open(out.c_str(), "w")) {
      w.println("written over BLE");
      w.close();
    }
  }
}

void loop() {}
