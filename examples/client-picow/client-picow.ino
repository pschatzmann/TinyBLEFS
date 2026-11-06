// Pico W client: finds one TinyBLEFS server, mounts it and lists / reads files.
// The BLE library of the core supports only one connection, so only one
// server can be mounted at a time.
// Select Tools -> IP/Bluetooth Stack -> "IPv4 + Bluetooth".
#include "TinyBLEFS.h"
#include "TinyBLEFS/adapters/PicoW.h"

PicoWClientTransport transport;
FileClient client(transport, 5000);
Mounts mounts;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  auto servers = PicoWClientTransport::scan(5000, "TinyFS");
  Serial.print("found ");
  Serial.print(servers.size());
  Serial.println(" server(s)");
  if (servers.empty() || !transport.connect(servers[0])) {
    Serial.println("no connection");
    return;
  }
  std::string name = mounts.mount(client);  // name is provided by the server
  Serial.print("mounted as /");
  Serial.println(name.c_str());

  std::string root = "/" + name;
  std::vector<DirEntry> entries;
  mounts.list(root.c_str(), entries);
  for (auto& e : entries) {
    Serial.print(e.isDir ? "  d " : "  - ");
    Serial.print(e.name.c_str());
    Serial.print(" ");
    Serial.println(e.size);
  }

  std::string file = root + "/hello.txt";
  if (RemoteFile f = mounts.open(file.c_str())) {
    while (f.available()) Serial.write(f.read());
  }
}

void loop() {}
