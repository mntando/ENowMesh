#include "ENowMesh.h"

ENowMesh mesh;

void setup() {
  Serial.begin(115200);
  
  // Configure BEFORE init
  mesh.channel = 6;              // Use WiFi channel 6
  mesh.maxHops = 8;              // Allow 8 hops (larger mesh)
  mesh.ackTimeout = 3000;        // Wait 3 seconds for ACK
  mesh.maxRetries = 5;           // Retry 5 times
  mesh.dupDetectWindowMs = 15000; // Remember duplicates for 15 sec
  
  mesh.setRole(ENowMesh::ROLE_REPEATER);
  mesh.initWiFi();
  mesh.setChannel();
  mesh.initEspNow();
  mesh.registerCallbacks();
}

void loop() {
	// Mesh maintenance
	mesh.sendHelloBeacon();
	mesh.checkPendingMessages();
	mesh.prunePeers();
  
	delay(100);
}