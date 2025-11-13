#include "ENowMesh.h"

ENowMesh mesh;
uint8_t masterMAC[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

void setup() {
  Serial.begin(115200);
  mesh.setRole(ENowMesh::ROLE_LEAF);
  mesh.initWiFi();
  mesh.setChannel();
  mesh.initEspNow();
  mesh.registerCallbacks();
}

void loop() {
  mesh.prunePeers();
  mesh.checkPendingMessages();
  
  // Send ONLY to master (not broadcast)
  char msg[64];
  snprintf(msg, sizeof(msg), "Alert: %lu", millis());
  mesh.sendData(msg, masterMAC);  // ← Unicast with ACK
  
  delay(5000);
}
