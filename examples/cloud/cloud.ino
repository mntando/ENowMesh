#include "ENowMesh.h"
#include <WiFi.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

ENowMesh mesh;
WebsocketsServer wsServer;

// ====== WIFI (PC HOTSPOT) ======
const char* WIFI_SSID = "YOUR_PC_HOTSPOT_NAME";
const char* WIFI_PASS = "YOUR_PC_HOTSPOT_PASSWORD";

// Target leaf node MAC address
uint8_t leafNodeMAC[] = {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF};

const int BUTTON_PIN = 4;
bool lastButtonState = LOW;

// ====== MESH MESSAGE CALLBACK ======
void onMessage(const uint8_t *src_mac, const char *payload, size_t len) {
  Serial.printf(
    "Mesh RX from %02X:%02X:%02X:%02X:%02X:%02X: %s\n",
    src_mac[0], src_mac[1], src_mac[2],
    src_mac[3], src_mac[4], src_mac[5],
    payload
  );

  // Forward mesh data to Electron via WebSocket
  wsServer.broadcast(payload);
}

// ====== WIFI SETUP ======
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to PC hotspot");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi connected, ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // --- ENowMesh setup ---
  mesh.setRole(ENowMesh::ROLE_MASTER);
  mesh.initWiFi();       // required by ENowMesh
  mesh.initEspNow();
  mesh.setChannel();     // all nodes must follow this channel
  mesh.registerCallbacks();
  mesh.setMessageCallback(onMessage);

  Serial.printf(
    "MASTER node ready! MAC: %s\n",
    mesh.macToStr(mesh.getNodeMac()).c_str()
  );

  // --- WiFi (PC hotspot) ---
  setupWiFi();

  // --- WebSocket server ---
  wsServer.listen(81);
  Serial.println("WebSocket server listening on port 81");
}

// ====== LOOP ======
void loop() {
  // Handle WebSocket clients
  wsServer.poll();

  // Button send command to leaf
  bool buttonState = digitalRead(BUTTON_PIN);
  if (buttonState == HIGH && lastButtonState == LOW) {
    mesh.sendData("{\"cmd\":\"LED_ON\"}", leafNodeMAC);
    Serial.println("Sent LED command to leaf node");
    delay(50);
  }
  lastButtonState = buttonState;

  // Mesh maintenance
  mesh.sendHelloBeacon();
  mesh.checkPendingMessages();
  mesh.prunePeers();

  delay(50);
}
