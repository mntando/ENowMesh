#include "ENowMesh.h"
ENowMesh mesh;

void setup() {
  Serial.begin(115200);
  mesh.setRole(ENowMesh::ROLE_MASTER);  // or REPEATER or LEAF
  mesh.initWiFi();
  mesh.setChannel();
  mesh.initEspNow();
  mesh.registerCallbacks();
}

void loop() {
  mesh.prunePeers();
  mesh.checkPendingMessages();
  
  // Your code here
  mesh.sendData("Hello");
  delay(5000);
}