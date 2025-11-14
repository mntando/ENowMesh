#include "ENowMesh.h"

ENowMesh mesh;

void setup() {
  Serial.begin(115200);
  
  mesh.setRole(ENowMesh::ROLE_REPEATER);
  mesh.initWiFi();
  mesh.setChannel();
  mesh.initEspNow();
  mesh.registerCallbacks();
  
  Serial.println("Repeater ready");
}

void loop() {
  mesh.prunePeers();
  mesh.checkPendingMessages();
  
  // Send status update
  char msg[64];
  snprintf(msg, sizeof(msg), "Repeater uptime: %lu", millis());
  mesh.sendData(msg);
  
  delay(10000);
}