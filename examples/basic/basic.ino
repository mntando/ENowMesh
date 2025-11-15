#include "ENowMesh.h"
ENowMesh mesh;

// Optional: Specific node MAC for unicast example (replace with actual MAC)
// uint8_t targetNodeMAC[] = {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF};

// ========================================
// RECEIVING MESSAGES
// ========================================
// Callback when message arrives
void onMessage(const uint8_t *src_mac, const char *payload, size_t len) {
    Serial.printf("[RECEIVED] from %02X:%02X:%02X:%02X:%02X:%02X: %s\n", 
                  src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5], payload);
    
    // NOTE: The callback runs in the ESP-NOW receive context, so keep it fast and don't call blocking functions (like long delays or Serial reads).
    // If you need to do slow operations, consider queuing the message for processing in loop().
    // Look at the "queue_messages" example for a full implementation.
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    mesh.setRole(ENowMesh::ROLE_REPEATER);
    
    // Initialize mesh
    mesh.initWiFi();
    mesh.initEspNow();
    mesh.setChannel();
    mesh.registerCallbacks();
    
    // Register message callback
    mesh.setMessageCallback(onMessage);
    
    Serial.println("Mesh node ready!");
    Serial.printf("My MAC: %s\n", mesh.macToStr(mesh.getNodeMac()).c_str());
}

void loop() {
    static unsigned long lastSend = 0;

	// ========================================
    // SENDING MESSAGES
    // ========================================    
    if (millis() - lastSend > 10000) {

        // Example 1: Broadcast to all nodes
        mesh.sendData("Hello everyone!");
        Serial.println("[SENT] Broadcast to all nodes");
        
        // Example 2: Send to MASTER node (automatic routing)
        mesh.sendToMaster("Data for master");
        Serial.println("[SENT] Message routed to MASTER");
        
        // Example 3: Send to specific node MAC (unicast)
        // Uncomment and set targetNodeMAC[] above to use:
        // mesh.sendData("Hello specific node", targetNodeMAC);
        // Serial.printf("[SENT] Unicast to %s\n", mesh.macToStr(targetNodeMAC).c_str());
        
        // Example 4: Send to all REPEATER nodes
        // mesh.sendToRepeaters("Message for repeaters");
        // Serial.println("[SENT] Message to all REPEATERs");
        
        lastSend = millis();
    }
    
    // Mesh maintenance
    mesh.sendHelloBeacon();
    mesh.checkPendingMessages();
    mesh.prunePeers();
    
    delay(100);
}