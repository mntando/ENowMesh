#include "ENowMesh.h"

// ----- Static storage -----
ENowMesh::PeerInfo ENowMesh::peersStatic[ENowMesh::PEER_TABLE_SIZE] = {};
uint8_t ENowMesh::myMacStatic[6] = {};
ENowMesh* ENowMesh::instance = nullptr;

ENowMesh::SeenPacket ENowMesh::seenPacketsStatic[ENowMesh::DUP_DETECT_BUFFER_SIZE] = {};
uint16_t ENowMesh::seenPacketsIndex = 0;
portMUX_TYPE ENowMesh::seenPacketsMux = portMUX_INITIALIZER_UNLOCKED;

ENowMesh::PendingMessage ENowMesh::pendingMessages[ENowMesh::MAX_PENDING_MESSAGES] = {};
portMUX_TYPE ENowMesh::pendingMux = portMUX_INITIALIZER_UNLOCKED;

// ----- Constructor -----
ENowMesh::ENowMesh() {
    instance = this;
}

// ----- Role Management -----
void ENowMesh::setRole(NodeRole r) {
    role = r;
}

ENowMesh::NodeRole ENowMesh::getRole() const {
    return role;
}

const char* ENowMesh::getRoleName() const {
    switch (role) {
        case ROLE_MASTER:   return "MASTER";    // 
        case ROLE_REPEATER: return "REPEATER";  // 
        case ROLE_LEAF:     return "LEAF";      // Does not forward
        default:            return "UNKNOWN";   //  
    }
}

// ----- Accessors -----
ENowMesh::PeerInfo* ENowMesh::getPeerTable() {
    return peersStatic;
}

uint8_t* ENowMesh::getNodeMac() {
    return myMacStatic;
}

// ----- WiFi Setup -----
void ENowMesh::initWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    WiFi.macAddress(myMacStatic);
    Serial.printf("Node MAC: %s\n", macToStr(myMacStatic).c_str());
}

// ----- ESP-NOW Init -----
void ENowMesh::initEspNow() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW!");
        while (true) delay(100);
    }
}

// ----- Register Callbacks -----
void ENowMesh::registerCallbacks() {
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
}

// ----- User Callbacks -----
void ENowMesh::setMessageCallback(MessageCallback cb) {
    userCallback = cb;
}

// ----- Set WiFi Channel -----
void ENowMesh::setChannel() {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

// ----- Helpers -----
String ENowMesh::macToStr(const uint8_t *mac) {
    char buf[18];
    sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

const char* ENowMesh::msgTypeToStr(uint8_t msg_type) {
    static char buf[64];
    buf[0] = '\0';
    
    if (msg_type & MSG_TYPE_DATA) strcat(buf, "DATA|");
    if (msg_type & MSG_TYPE_HELLO) strcat(buf, "HELLO|");
    if (msg_type & MSG_TYPE_ACK) strcat(buf, "ACK|");
    if (msg_type & MSG_TYPE_NO_FORWARD) strcat(buf, "NO_FWD|");
    if (msg_type & MSG_TYPE_NO_ACK) strcat(buf, "NO_ACK|");
    
    // Remove trailing '|'
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '|') buf[len-1] = '\0';
    
    return buf;
}

// =======================================
// ===== PEER MANAGEMENT ===
// =======================================

// ----- Peer Search -----
int ENowMesh::findPeer(const uint8_t *mac) {
    for (size_t i = 0; i < ENowMesh::PEER_TABLE_SIZE; ++i)
        if (peersStatic[i].valid && memcmp(peersStatic[i].mac, mac, 6) == 0)
            return (int)i;
    return -1;
}

// ----- Add/Update Peer -----
void ENowMesh::touchPeer(const uint8_t *mac) {
    int idx = findPeer(mac);
    if (idx >= 0) {
        peersStatic[idx].lastSeen = millis();
        return;
    }

    for (size_t i = 0; i < ENowMesh::PEER_TABLE_SIZE; ++i) {
        if (!peersStatic[i].valid) {
            esp_now_peer_info_t info = {};
            memcpy(info.peer_addr, mac, 6);
            info.channel = channel;
            info.ifidx = WIFI_IF_STA;
            info.encrypt = 0;

            esp_err_t result = esp_now_add_peer(&info);
            if (result == ESP_OK || result == ESP_ERR_ESPNOW_EXIST) {
                memcpy(peersStatic[i].mac, mac, 6);
                peersStatic[i].lastSeen = millis();
                peersStatic[i].valid = true;
                Serial.printf("Added peer %s at slot %u\n", macToStr(mac).c_str(), (unsigned)i);
            } else {
                Serial.printf("Failed to add peer %s to ESP-NOW: %d\n", macToStr(mac).c_str(), result);
            }
            return;
        }
    }

    Serial.println("Peer table full! Cannot add new peer.");
}

// ----- Peer Pruning -----
void ENowMesh::prunePeers() {
    uint32_t now = millis();
    for (size_t i = 0; i < ENowMesh::PEER_TABLE_SIZE; ++i) {
        if (peersStatic[i].valid && (now - peersStatic[i].lastSeen > peerTimeout)) {
            Serial.printf("Pruning peer %s slot %u\n", macToStr(peersStatic[i].mac).c_str(), (unsigned)i);
            esp_now_del_peer(peersStatic[i].mac);
            peersStatic[i].valid = false;
        }
    }
}

// =======================================
// ===== HELLO BEACON ====
// =======================================

void ENowMesh::sendHelloBeacon() {
    uint32_t now = millis();
    
    // Check if it's time to send HELLO
    if (now - lastHelloTime < helloInterval) {
        return;  // Not time yet
    }
    
    lastHelloTime = now;
    
    // Build HELLO message with role info
    char helloMsg[32];
    snprintf(helloMsg, sizeof(helloMsg), "HELLO:%s", getRoleName());
    
    size_t mlen = strlen(helloMsg);
    
    // --- Build header ---
    packet_hdr_t hdr = {};
    memcpy(hdr.src_mac, myMacStatic, 6);
    memset(hdr.dest_mac, 0xFF, 6);  // Broadcast
    hdr.seq = random(0xFFFF);
    hdr.hop_count = 0;
    hdr.msg_type = MSG_TYPE_HELLO | MSG_TYPE_NO_FORWARD | MSG_TYPE_NO_ACK;  // HELLO flags
    hdr.payload_len = static_cast<uint8_t>(mlen);
    
    // --- Build packet ---
    size_t total = sizeof(packet_hdr_t) + hdr.payload_len;
    uint8_t *buf = (uint8_t*)malloc(total);
    if (!buf) {
        Serial.println("HELLO: Memory allocation failed");
        return;
    }
    
    memcpy(buf, &hdr, sizeof(packet_hdr_t));
    memcpy(buf + sizeof(packet_hdr_t), helloMsg, hdr.payload_len);
    
    // --- Broadcast to all peers ---
    forwardToPeersExcept(nullptr, buf, total);
    Serial.printf("[HELLO BEACON] Sent to all peers: %s\n", helloMsg);
    
    free(buf);
}

// =======================================
// ===== COMMUNICATION FUNCTIONS ====
// =======================================

// ----- Send Wrapper -----
esp_err_t ENowMesh::sendToMac(const uint8_t *mac, const uint8_t *data, size_t len) {
    if (!mac) return ESP_ERR_INVALID_ARG;
    return esp_now_send(mac, data, len);
}

// ----- Forward Wrapper -----
void ENowMesh::forwardToPeersExcept(const uint8_t *exclude_mac, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < ENowMesh::PEER_TABLE_SIZE; ++i) {
        if (!peersStatic[i].valid) continue;
        if (exclude_mac && memcmp(peersStatic[i].mac, exclude_mac, 6) == 0) continue;
        esp_err_t r = esp_now_send(peersStatic[i].mac, data, len);
        if (r != ESP_OK) {
            Serial.printf("esp_now_send to %s failed: %d\n", macToStr(peersStatic[i].mac).c_str(), r);
        }
    }
}

// ----- Send Data -----
esp_err_t ENowMesh::sendData(const char *msg, const uint8_t *dest_mac, uint8_t msg_type) {
    if (!msg) return ESP_ERR_INVALID_ARG;

    // --- Validate message length before allocating ---
    size_t mlen = strlen(msg);
    if (mlen == 0) {
        Serial.println("sendData: empty message, ignoring.");
        return ESP_ERR_INVALID_ARG;
    } if (mlen > maxPayload) {
        Serial.printf("sendData: message too long (%u > maxPayload %u)\n", (unsigned)mlen, (unsigned)maxPayload);
        return ESP_ERR_INVALID_SIZE;
    } if (mlen > 255) {
        Serial.printf("sendData: payload too large for uint8_t field (%u > 255)\n", (unsigned)mlen);
        return ESP_ERR_INVALID_SIZE;
    }

    // --- Build header ---
    packet_hdr_t hdr = {};
    memcpy(hdr.src_mac, myMacStatic, 6);

    if (dest_mac)
        memcpy(hdr.dest_mac, dest_mac, 6);
    else
        memset(hdr.dest_mac, 0xFF, 6); // broadcast

    hdr.seq = random(0xFFFF);
    hdr.hop_count = 0;
    hdr.msg_type = msg_type;  // Use provided message type

    // Set NO_ACK flag for broadcasts (if not already set)
    if (!dest_mac && !(msg_type & MSG_TYPE_NO_ACK)) {
        hdr.msg_type |= MSG_TYPE_NO_ACK;
    }

    hdr.payload_len = static_cast<uint8_t>(mlen);

    // --- Allocate and build full packet ---
    size_t total = sizeof(packet_hdr_t) + hdr.payload_len;
    
    // Check ESP-NOW hardware limit
    if (total > ESP_NOW_MAX_IE_DATA_LEN) {
        Serial.printf("ERROR: Packet too large (%u bytes > %u max)\n", (unsigned)total, (unsigned)ESP_NOW_MAX_IE_DATA_LEN);
        return ESP_ERR_INVALID_SIZE;
    }
    
    uint8_t *buf = (uint8_t*)malloc(total);
    if (!buf) return ESP_ERR_NO_MEM;

    memcpy(buf, &hdr, sizeof(packet_hdr_t));
    memcpy(buf + sizeof(packet_hdr_t), msg, hdr.payload_len);

    // --- Send ---
    esp_err_t result;
    if (dest_mac) {
        result = sendToMac(dest_mac, buf, total);   // unicast
        Serial.printf("[MESH SEND] To %s | type=%s | len=%u | msg='%s' | result=%d\n", macToStr(dest_mac).c_str(), msgTypeToStr(hdr.msg_type), (unsigned)hdr.payload_len, msg, (int)result);
    } else {
        forwardToPeersExcept(nullptr, buf, total);  // broadcast
        result = ESP_OK;
        Serial.printf("[MESH BROADCAST] type=%s | len=%u | msg='%s'\n", msgTypeToStr(hdr.msg_type), (unsigned)hdr.payload_len, msg);
    }

    free(buf);

    // Track unicast messages that need ACKs (if MSG_TYPE_NO_ACK is not set)
    if (dest_mac && result == ESP_OK && !(hdr.msg_type & MSG_TYPE_NO_ACK)) {
        portENTER_CRITICAL(&pendingMux);
        
        // Find empty slot
        for (size_t i = 0; i < instance->maxPendingMessages; i++) {
            if (!pendingMessages[i].waiting) {
                memcpy(pendingMessages[i].dest_mac, dest_mac, 6);
                pendingMessages[i].seq = hdr.seq;
                pendingMessages[i].sendTime = millis();
                pendingMessages[i].retryCount = 0;
                pendingMessages[i].payloadLen = hdr.payload_len;
                memcpy(pendingMessages[i].payload, msg, hdr.payload_len);
                pendingMessages[i].waiting = true;
                break;
            }
        }
        
        portEXIT_CRITICAL(&pendingMux);
    }

    return result;
}


// =======================================
// ===== STATIC CALLBACK IMPLEMENTATION ===
// =======================================

void ENowMesh::OnDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
    ENowMesh *m = instance;
    if (!m || !info) return;
    
    const uint8_t *mac_addr = info->des_addr;
    if (!mac_addr) return;
    
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.printf("Sent OK to %02X:%02X:%02X:%02X:%02X:%02X\n", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    } else {
        Serial.printf("Send FAILED to %02X:%02X:%02X:%02X:%02X:%02X - removing peer\n", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        
        // Remove failed peer immediately
        int idx = m->findPeer(mac_addr);
        if (idx >= 0) {
            esp_now_del_peer(mac_addr);
            m->peersStatic[idx].valid = false;
        }
    }
}

void ENowMesh::OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    ENowMesh *m = instance;
    if (!m || !info) return;

    const uint8_t *mac_addr = info->src_addr;
    Serial.printf("Received %d bytes from %02X:%02X:%02X:%02X:%02X:%02X\n", len, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    // === BASIC VALIDATION ===
    if (len < (int)sizeof(packet_hdr_t)) {
        Serial.println("Packet too small. ignoring.");
        m->touchPeer(mac_addr);
        return;
    }

    packet_hdr_t hdr;
    memcpy(&hdr, incomingData, sizeof(packet_hdr_t));

    // Drop packets from self
    if (memcmp(hdr.src_mac, myMacStatic, 6) == 0) {
        Serial.println("Packet originated from self. Dropping.");
        return;
    }

    // Duplicate detection (before any processing)
    if (m->isDuplicate(hdr.src_mac, hdr.seq)) {
        Serial.printf("DUPLICATE packet detected (src=%s seq=%u) - dropping\n", m->macToStr(hdr.src_mac).c_str(), (unsigned)hdr.seq);
        m->touchPeer(mac_addr);  // still update peer table
        return;
    }

    if (hdr.payload_len > m->maxPayload) {
        Serial.printf("Payload_len %u exceeds MAX_PAYLOAD %u. ignoring.\n", hdr.payload_len, (unsigned)m->maxPayload);
        m->touchPeer(mac_addr);
        return;
    }

    if ((size_t)len < sizeof(packet_hdr_t) + hdr.payload_len) {
        Serial.println("Payload length mismatch. ignoring.");
        m->touchPeer(mac_addr);
        return;
    }

    m->touchPeer(mac_addr);

    Serial.printf("[RECV] type=%s | from=%s | seq=%u | hop=%u\n", 
                 m->msgTypeToStr(hdr.msg_type), m->macToStr(hdr.src_mac).c_str(), 
                 (unsigned)hdr.seq, (unsigned)hdr.hop_count);

    // === HANDLE HELLO BEACONS ===
    if (hdr.msg_type & MSG_TYPE_HELLO) {
        Serial.printf("[HELLO RECEIVED] from %s (via %s) - peer discovered\n", 
                     m->macToStr(hdr.src_mac).c_str(), m->macToStr(mac_addr).c_str());
        
        // Peer already added via touchPeer() above
        // HELLO packets are not forwarded (MSG_TYPE_NO_FORWARD flag prevents it)
        // HELLO packets don't need ACK (MSG_TYPE_NO_ACK flag prevents it)
        return;  // HELLO consumed
    }

    // === PACKET FOR THIS NODE ===
    if (memcmp(hdr.dest_mac, myMacStatic, 6) == 0) {
        Serial.printf("[%s] Packet for me (seq=%u) from immediate=%s original_src=%s hop_count=%u payload_len=%u\n", 
                     m->getRoleName(), (unsigned)hdr.seq, m->macToStr(mac_addr).c_str(), 
                     m->macToStr(hdr.src_mac).c_str(), (unsigned)hdr.hop_count, (unsigned)hdr.payload_len);

        if (hdr.payload_len > 0) {
            const uint8_t *pl = incomingData + sizeof(packet_hdr_t);

            // Check for ACK packet using MSG_TYPE_ACK flag
            if (hdr.msg_type & MSG_TYPE_ACK) {
                // Extract acknowledged sequence number from payload
                char tmp[16];
                size_t copyLen = (hdr.payload_len < 15) ? hdr.payload_len : 15;
                memcpy(tmp, pl, copyLen);
                tmp[copyLen] = '\0';
                uint16_t ack_seq = (uint16_t)atoi(tmp);
                
                Serial.printf("[ACK RECEIVED] from %s acknowledging seq=%u\n", m->macToStr(hdr.src_mac).c_str(), (unsigned)ack_seq);
                
                // Clear from pending messages
                portENTER_CRITICAL(&pendingMux);
                for (size_t i = 0; i < instance->maxPendingMessages; i++) {
                    if (pendingMessages[i].waiting && pendingMessages[i].seq == ack_seq && memcmp(pendingMessages[i].dest_mac, hdr.src_mac, 6) == 0) {
                        pendingMessages[i].waiting = false;
                        Serial.printf("[MSG CONFIRMED] seq=%u delivered successfully\n", ack_seq);
                        break;
                    }
                }
                portEXIT_CRITICAL(&pendingMux);
                
                return;  // ACK consumed
            }

            // Print payload
            char *tmp = (char*)malloc(hdr.payload_len + 1);
            if (tmp) {
                memcpy(tmp, pl, hdr.payload_len);
                tmp[hdr.payload_len] = '\0';
                Serial.printf("Payload: %s\n", tmp);

                // Call user callback if registered
                if (m->userCallback) {
                    m->userCallback(hdr.src_mac, tmp, hdr.payload_len);
                }
                
                free(tmp);
            }
        }

        // Send ACK back to original sender (only if MSG_TYPE_NO_ACK is not set)
        if (!(hdr.msg_type & MSG_TYPE_NO_ACK)) {
            char ackPayload[8];
            snprintf(ackPayload, sizeof(ackPayload), "%u", hdr.seq);
            
            // Use sendData with MSG_TYPE_ACK flag (ACKs don't need their own ACKs)
            m->sendData(ackPayload, hdr.src_mac, MSG_TYPE_ACK | MSG_TYPE_NO_ACK);
            Serial.printf("ACK sent to %s for seq=%u\n", m->macToStr(hdr.src_mac).c_str(), (unsigned)hdr.seq);
        }
        
        return;  // Packet consumed - don't forward it
    }

    // === FORWARDING LOGIC ===
    
    // Check if packet should not be forwarded
    if (hdr.msg_type & MSG_TYPE_NO_FORWARD) {
        Serial.println("Packet has NO_FORWARD flag - not forwarding.");
        return;
    }

    // Check hop limit
    if (hdr.hop_count >= m->maxHops) {
        Serial.println("Max hops reached. Dropping packet.");
        return;
    }

    // Hop count management with proper struct casting
    size_t fwdLen = sizeof(packet_hdr_t) + hdr.payload_len;
    uint8_t *fwdBuf = (uint8_t*)malloc(fwdLen);
    
    // Memory safety check
    if (!fwdBuf) {
        Serial.println("Memory allocation failed!");
        return;
    }

    memcpy(fwdBuf, incomingData, fwdLen);
    packet_hdr_t *fwd_hdr = (packet_hdr_t*)fwdBuf;
    fwd_hdr->hop_count = hdr.hop_count + 1;

    // If this node is a LEAF, do not forward the packet.
    if (m->getRole() == ENowMesh::ROLE_LEAF) {
        Serial.println("Role is LEAF – not forwarding packet.");
        free(fwdBuf);
        return;
    }

    // Check if broadcast
    bool isBroadcast = true;
    for (int i = 0; i < 6; i++) {
        if (hdr.dest_mac[i] != 0xFF) {
            isBroadcast = false;
            break;
        }
    }

    if (isBroadcast) {
        // Always flood broadcasts
        m->forwardToPeersExcept(mac_addr, fwdBuf, fwdLen);
        Serial.printf("Flooded broadcast packet (src %s) hop->%u\n", m->macToStr(hdr.src_mac).c_str(), fwd_hdr->hop_count);
    } else {
        // Try direct send to destination if it's a known peer
        int peerIndex = m->findPeer(hdr.dest_mac);
        if (peerIndex >= 0) {
            esp_err_t r = esp_now_send(ENowMesh::peersStatic[peerIndex].mac, fwdBuf, fwdLen);
            if (r == ESP_OK) {
                Serial.printf("Forwarded directly to %s (src %s dest %s) hop->%u\n", 
                             m->macToStr(ENowMesh::peersStatic[peerIndex].mac).c_str(), 
                             m->macToStr(hdr.src_mac).c_str(), m->macToStr(hdr.dest_mac).c_str(), 
                             fwd_hdr->hop_count);
                free(fwdBuf);
                return;  // Success - don't also flood
            }
            // Only flood if direct send failed
            Serial.printf("Direct send to %s failed (%d), falling back to flood.\n", m->macToStr(hdr.dest_mac).c_str(), r);
        }
        
        // Destination unknown or direct send failed – flood to peers
        m->forwardToPeersExcept(mac_addr, fwdBuf, fwdLen);
        Serial.printf("Flooded packet (src %s dest %s) hop->%u\n", 
                     m->macToStr(hdr.src_mac).c_str(), m->macToStr(hdr.dest_mac).c_str(), 
                     fwd_hdr->hop_count);
    }

    free(fwdBuf);
}

// ----- Duplicate Detection -----
bool ENowMesh::isDuplicate(const uint8_t *src_mac, uint16_t seq) {
    uint32_t now = millis();
    
    // Check if we've seen this packet recently
    for (size_t i = 0; i < instance->dupDetectBufferSize; ++i) {
        if (!seenPacketsStatic[i].valid) continue;
        
        // Remove old entries
        if (now - seenPacketsStatic[i].timestamp > instance->dupDetectWindowMs) {
            seenPacketsStatic[i].valid = false;
            continue;
        }
        
        // Check for duplicate
        if (memcmp(seenPacketsStatic[i].src_mac, src_mac, 6) == 0 && seenPacketsStatic[i].seq == seq) {
            return true;  // Duplicate found!
        }
    }
    
    // Not a duplicate - record it with critical section protection
    portENTER_CRITICAL(&seenPacketsMux);
    
    uint16_t writeIndex = seenPacketsIndex;
    seenPacketsIndex = (seenPacketsIndex + 1) % instance->dupDetectBufferSize;
    
    portEXIT_CRITICAL(&seenPacketsMux);
    
    // Write to reserved slot (outside critical section for speed)
    seenPacketsStatic[writeIndex].valid = true;
    memcpy(seenPacketsStatic[writeIndex].src_mac, src_mac, 6);
    seenPacketsStatic[writeIndex].seq = seq;
    seenPacketsStatic[writeIndex].timestamp = now;
    
    return false;
}

// ----- Check Pending Messages for ACKs and Retries -----
void ENowMesh::checkPendingMessages() {
    uint32_t now = millis();
    
    portENTER_CRITICAL(&pendingMux);
    
    for (size_t i = 0; i < instance->maxPendingMessages; i++) {
        if (!pendingMessages[i].waiting) continue;
        
        if (now - pendingMessages[i].sendTime > instance->ackTimeout) {
            if (pendingMessages[i].retryCount < instance->maxRetries) {
                // Retry
                pendingMessages[i].retryCount++;
                pendingMessages[i].sendTime = now;
                
                portEXIT_CRITICAL(&pendingMux);
                
                Serial.printf("[RETRY] seq=%u to %s (attempt %u/%u)\n", 
                             pendingMessages[i].seq, macToStr(pendingMessages[i].dest_mac).c_str(), 
                             pendingMessages[i].retryCount, instance->maxRetries);
                
                sendData((char*)pendingMessages[i].payload, pendingMessages[i].dest_mac);
                
                portENTER_CRITICAL(&pendingMux);
            } else {
                // Failed permanently
                Serial.printf("[MSG FAILED] seq=%u to %s after %u retries\n", 
                             pendingMessages[i].seq, macToStr(pendingMessages[i].dest_mac).c_str(), 
                             instance->maxRetries);
                pendingMessages[i].waiting = false;
            }
        }
    }
    
    portEXIT_CRITICAL(&pendingMux);
}