#include "ENowMesh.h"

ENowMesh mesh;

// Target leaf node MAC address (replace with your actual node's MAC)
uint8_t leafNodeMAC[] = {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF};

const int BUTTON_PIN = 4;  // Button on GPIO 4
bool lastButtonState = LOW;

// Callback when message arrives from mesh
void onMessage(const uint8_t *src_mac, const char *payload, size_t len) {
  Serial.printf("Received from %02X:%02X:%02X:%02X:%02X:%02X: %s\n", src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5], payload);
  
  // Process message (e.g., forward to cloud)
  Serial.printf(">> CLOUD: %s\n", payload);
  // TODO: Replace with actual cloud API call (HTTP, MQTT, etc.)
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Configure as MASTER node
  mesh.setRole(ENowMesh::ROLE_MASTER);
  
  // Initialize mesh
  mesh.initWiFi();
  mesh.initEspNow();
  mesh.setChannel();
  mesh.registerCallbacks();
  mesh.setMessageCallback(onMessage);
  
  Serial.printf("MASTER node ready! MAC: %s\n", mesh.macToStr(mesh.getNodeMac()).c_str());
}

void loop() {
  // Check button press
  bool buttonState = digitalRead(BUTTON_PIN);
  if (buttonState == HIGH && lastButtonState == LOW) {
    // Button pressed - send command to leaf node
    mesh.sendData("CMD:LED_ON", leafNodeMAC);
    Serial.println("Sent LED command to leaf node");
    delay(50);  // Debounce
  }
  lastButtonState = buttonState;
  
  // Mesh maintenance
  mesh.sendHelloBeacon();
  mesh.checkPendingMessages();
  mesh.prunePeers();
  
  delay(100);
}