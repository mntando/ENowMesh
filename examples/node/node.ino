#include "ENowMesh.h"

ENowMesh mesh;

// Master node MAC address (replace with your master's MAC)
uint8_t masterMAC[] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC};

const int LED_PIN = 2;  // Built-in LED
const int SENSOR_PIN = 34;  // Analog sensor input

// Callback when message arrives from mesh
void onMessage(const uint8_t *src_mac, const char *payload, size_t len) {
  Serial.printf("Command from %02X:%02X:%02X:%02X:%02X:%02X: %s\n",
                src_mac[0], src_mac[1], src_mac[2], src_mac[3], 
                src_mac[4], src_mac[5], payload);
  
  // Act on commands
  if (strcmp(payload, "CMD:LED_ON") == 0) {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED turned ON");
  }
  else if (strcmp(payload, "CMD:LED_OFF") == 0) {
    digitalWrite(LED_PIN, LOW);
    Serial.println("LED turned OFF");
  }
  else if (strcmp(payload, "CMD:STATUS") == 0) {
    // Respond immediately with status
    sendStatus();
  }
}

void sendStatus() {
  int sensorValue = analogRead(SENSOR_PIN);
  char msg[64];
  snprintf(msg, sizeof(msg), "STATUS:sensor=%d,led=%d", 
           sensorValue, digitalRead(LED_PIN));
  
  mesh.sendData(msg, masterMAC);
  Serial.printf("Sent status to master: %s\n", msg);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(SENSOR_PIN, INPUT);
  
  // Configure as LEAF node (doesn't forward packets)
  mesh.setRole(ENowMesh::ROLE_LEAF);
  
  // Initialize mesh
  mesh.initWiFi();
  mesh.initEspNow();
  mesh.setChannel();
  mesh.registerCallbacks();
  mesh.setMessageCallback(onMessage);
  
  Serial.printf("LEAF node ready! MAC: %s\n", 
                mesh.macToStr(mesh.getNodeMac()).c_str());
}

void loop() {
  // Periodically send status to master (every 30 seconds)
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 30000) {
    sendStatus();
    lastStatus = millis();
  }
  
  // Mesh maintenance
  mesh.sendHelloBeacon();
  mesh.checkPendingMessages();
  mesh.prunePeers();
  
  delay(100);
}