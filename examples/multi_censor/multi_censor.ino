#include "ENowMesh.h"

ENowMesh mesh;
unsigned long lastTemp = 0;
unsigned long lastHumidity = 0;

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
  
  unsigned long now = millis();
  
  // Send temperature every 10 seconds
  if (now - lastTemp > 10000) {
    lastTemp = now;
    char msg[64];
    snprintf(msg, sizeof(msg), "TEMP:%.1f", readTemp());
    mesh.sendData(msg);
  }
  
  // Send humidity every 30 seconds
  if (now - lastHumidity > 30000) {
    lastHumidity = now;
    char msg[64];
    snprintf(msg, sizeof(msg), "HUM:%.1f", readHumidity());
    mesh.sendData(msg);
  }
  
  delay(100);
}

float readTemp() { return 22.5; }
float readHumidity() { return 65.0; }