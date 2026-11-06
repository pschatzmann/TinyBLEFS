// Finds all TinyBLEFS servers in range, mounts each of them as "/<name>" (the
// name is provided by the server) and lists / reads / writes files on them.
//
// Note: the number of simultaneous BLE connections is limited by the NimBLE
// configuration (CONFIG_BT_NIMBLE_MAX_CONNECTIONS, 3 by default on ESP32).
#include <memory>
#include <vector>

#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/NimBLE.h"

// One transport + client per remote server. They must stay alive as long as
// the server is mounted, so they are heap allocated and never moved.
struct Remote {
  NimBLEClientTransport transport;
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

  // only servers whose BLE device name starts with "TinyFS" ("" = all)
  auto addresses = NimBLEClientTransport::scan(5000, "TinyFS");
  Serial.print("found ");
  Serial.print(addresses.size());
  Serial.println(" server(s)");

  for (auto& address : addresses) {
    auto remote = std::make_unique<Remote>();
    if (!remote->transport.connect(address)) {
      Serial.print("connect failed: ");
      Serial.println(address.toString().c_str());
      continue;
    }
    std::string name = mounts.mount(remote->client);
    if (name.empty()) {
      Serial.print("not responding: ");
      Serial.println(address.toString().c_str());
      continue;
    }
    Serial.print("mounted ");
    Serial.print(address.toString().c_str());
    Serial.print(" as /");
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
