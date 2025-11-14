#include <Arduino.h>
#include "ENowMesh.h"

ENowMesh mesh;

unsigned long lastMessage = 0;
const unsigned long MESSAGE_INTERVAL = 10000; // every 10 seconds

// choose node type:
#define NODE_TYPE ENowMesh::ROLE_REPEATER   // or ENowMesh::ROLE_LEAF

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP-NOW MESH NODE ===");

  // ---- Role setup ----
  mesh.setRole(NODE_TYPE);
  Serial.printf("Role: %s\n", mesh.getRoleName());

  // ---- Optional: Customize HELLO beacon interval ----
  // For LEAF nodes, consider longer intervals to save power:
  // if (NODE_TYPE == ENowMesh::ROLE_LEAF) {
  //   mesh.helloInterval = 60000;  // LEAF: Send HELLO every 60 seconds
  // }

  // ---- WiFi & ESP-NOW initialization ----
  mesh.initWiFi();
  mesh.setChannel();
  mesh.initEspNow();
  mesh.registerCallbacks();

  Serial.println("Node initialized successfully.");
}

void loop() {
  mesh.prunePeers();           // regularly clean inactive peers
  mesh.sendHelloBeacon();       // send periodic HELLO beacons
  mesh.checkPendingMessages();  // handle ACK retries

  unsigned long now = millis();
  if (now - lastMessage >= MESSAGE_INTERVAL) {
    lastMessage = now;

    // example message from node
    char msg[64];
    snprintf(msg, sizeof(msg), "Hello from NODE (%s) at %lu ms", mesh.getRoleName(), now);

    // broadcast message into mesh (master will receive it)
    mesh.sendData(msg);

    Serial.printf("Sent: %s\n", msg);
  }

  delay(100);
}