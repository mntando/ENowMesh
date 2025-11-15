#include "ENowMesh.h"

ENowMesh mesh;

// Callback when message arrives
void onMessage(const uint8_t *src_mac, const char *payload, size_t len) {
	Serial.printf("Message from %02X:%02X:%02X:%02X:%02X:%02X: %s\n", src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5], payload);

	// NOTE: The callback runs in the ESP-NOW receive context, so keep it fast and don't call blocking functions (like long delays or Serial reads).
	// If you need to do slow operations, consider queuing the message for processing in loop().
	// Look at the "queue_messages" example for a full implementation.

}

void setup() {
	Serial.begin(115200);
	delay(1000);
	
	// Initialize mesh
	mesh.initWiFi();
	mesh.initEspNow();
	mesh.setChannel();
	mesh.registerCallbacks();
	
	// Register message callback
	mesh.setMessageCallback(onMessage);
	
	Serial.println("Mesh node ready!");
}

void loop() {
	// Send broadcast message every 10 seconds
	static unsigned long lastSend = 0;
	if (millis() - lastSend > 10000) {
		mesh.sendData("Hello mesh!");
		lastSend = millis();
	}
	
	// Mesh maintenance
	mesh.sendHelloBeacon();
	mesh.checkPendingMessages();
	mesh.prunePeers();
	
	delay(100);
}