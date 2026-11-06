// ArduinoBLE client (UNO R4 WiFi, Nano 33 BLE, ...): mounts all TinyBLEFS servers
// in range and lists / reads / writes files on them.
#include <memory>
#include <vector>

#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/ArduinoBLE.h"

struct Remote {
  ArduinoBLEClientTransport transport;
  FileClient client{transport, 5000};
};
std::vector<std::unique_ptr<Remote>> remotes;
Mounts mounts;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  // only servers whose BLE device name starts with "TinyFS" ("" = all)
  auto devices = ArduinoBLEClientTransport::scan(5000, "TinyFS");
  Serial.print("found ");
  Serial.print(devices.size());
  Serial.println(" server(s)");

  for (auto& device : devices) {
    auto remote = std::make_unique<Remote>();
    // 244 needs an ATT MTU of 247 on both sides; the default is the safe 20
    // remote->transport.setPacketSize(244);
    if (!remote->transport.connect(device)) continue;
    std::string name = mounts.mount(remote->client);
    if (name.empty()) continue;
    Serial.print("mounted /");
    Serial.println(name.c_str());
    remotes.push_back(std::move(remote));
  }

  for (const std::string& name : mounts.names()) {
    std::string root = "/" + name;
    std::vector<DirEntry> entries;
    mounts.list(root.c_str(), entries);
    for (auto& e : entries) {
      Serial.print(e.isDir ? "d " : "- ");
      Serial.print(e.name.c_str());
      Serial.print(" ");
      Serial.println(e.size);
    }
    std::string file = root + "/hello.txt";
    if (RemoteFile f = mounts.open(file.c_str())) {
      while (f.available()) Serial.write(f.read());
    }
  }
}

void loop() {}
