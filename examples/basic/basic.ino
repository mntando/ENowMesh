void onMessageReceived(const uint8_t *src_mac, const char *payload, size_t len) {
  Serial.printf("APP: Got message from %s: %s\n", 
                mesh.macToStr(src_mac).c_str(), payload);
  
  // Your application logic here:
  // - Parse the payload
  // - Update variables
  // - Trigger actions
  // - etc.

  // NOTE: The callback runs in the ESP-NOW receive context, so keep it fast and don't call blocking functions (like long delays or Serial reads).
  // If you need to do slow operations, consider queuing the message for processing in loop().
  // Look at the "queue_messages" example for a full implementation.

}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  mesh.setRole(NODE_TYPE);
  mesh.initWiFi();
  mesh.setChannel();
  mesh.initEspNow();
  mesh.registerCallbacks();
  
  // Register your callback
  mesh.setMessageCallback(onMessageReceived);
  
  Serial.println("Ready!");
}