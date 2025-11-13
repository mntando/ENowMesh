#include "ENowMesh.h"

// ----- Static storage -----
ENowMesh::PeerInfo ENowMesh::peersStatic[ENowMesh::PEER_TABLE_SIZE] = {};
uint8_t ENowMesh::myMacStatic[6] = {};
ENowMesh* ENowMesh::instance = nullptr;

ENowMesh::SeenPacket ENowMesh::seenPacketsStatic[64] = {};
uint16_t ENowMesh::seenPacketsIndex = 0;
portMUX_TYPE ENowMesh::seenPacketsMux = portMUX_INITIALIZER_UNLOCKED;

uint16_t ENowMesh::sequenceCounter = 0;

// ----- Constructor -----
ENowMesh::ENowMesh() {
    instance = this;
    randomSeed(micros());  // Seed once per boot using microseconds
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
        case ROLE_MASTER:   return "MASTER";
        case ROLE_REPEATER: return "REPEATER";
        case ROLE_LEAF:     return "LEAF";
        default:            return "UNKNOWN";
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

// ----- Set WiFi Channel -----
void ENowMesh::setChannel() {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

// ----- Helpers -----
String ENowMesh::macToStr(const uint8_t *mac) {
    char buf[18];
    sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
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
esp_err_t ENowMesh::sendData(const char *msg, const uint8_t *dest_mac) {
    if (!msg) return ESP_ERR_INVALID_ARG;

    // --- Validate message length before allocating ---
    size_t mlen = strlen(msg);
    if (mlen == 0) {
        Serial.println("sendData: empty message, ignoring.");
        return ESP_ERR_INVALID_ARG;
    } if (mlen > maxPayload) {
        Serial.printf("sendData: message too long (%u > maxPayload %u)\n",
                      (unsigned)mlen, (unsigned)maxPayload);
        return ESP_ERR_INVALID_SIZE;
    } if (mlen > 255) {
        Serial.printf("sendData: payload too large for uint8_t field (%u > 255)\n",
                      (unsigned)mlen);
        return ESP_ERR_INVALID_SIZE;
    }

    // --- Build header ---
    packet_hdr_t hdr = {};
    memcpy(hdr.src_mac, myMacStatic, 6);

    if (dest_mac)
        memcpy(hdr.dest_mac, dest_mac, 6);
    else
        memset(hdr.dest_mac, 0xFF, 6); // broadcast

    hdr.seq = sequenceCounter++;
    hdr.hop_count = 0;
    hdr.payload_len = static_cast<uint8_t>(mlen);

    // --- Allocate and build full packet ---
    size_t total = sizeof(packet_hdr_t) + hdr.payload_len;
    uint8_t *buf = (uint8_t*)malloc(total);
    if (!buf) return ESP_ERR_NO_MEM;

    memcpy(buf, &hdr, sizeof(packet_hdr_t));
    memcpy(buf + sizeof(packet_hdr_t), msg, hdr.payload_len);

    // --- Send ---
    esp_err_t result;
    if (dest_mac) {
        result = sendToMac(dest_mac, buf, total);   // unicast
        Serial.printf("[MESH SEND] To %s | len=%u | msg='%s' | result=%d\n",
                      macToStr(dest_mac).c_str(),
                      (unsigned)hdr.payload_len, msg, (int)result);
    } else {
        forwardToPeersExcept(nullptr, buf, total);  // broadcast
        result = ESP_OK;
        Serial.printf("[MESH BROADCAST] len=%u | msg='%s'\n",
                      (unsigned)hdr.payload_len, msg);
    }

    free(buf);
    return result;
}


// =======================================
// ===== STATIC CALLBACK IMPLEMENTATION ===
// =======================================

void ENowMesh::OnDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
    const uint8_t *mac_addr = info ? info->des_addr : NULL;
    if (mac_addr) {
        Serial.printf("Sent callback to %02X:%02X:%02X:%02X:%02X:%02X - status: %s\n", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5], status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAILED");
    } else {
        Serial.printf("Send callback (unknown peer) - status: %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAILED");
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
        Serial.printf("DUPLICATE packet detected (src=%s seq=%u) - dropping\n", 
                      m->macToStr(hdr.src_mac).c_str(), (unsigned)hdr.seq);
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

    // === PACKET FOR THIS NODE ===
    if (memcmp(hdr.dest_mac, myMacStatic, 6) == 0) {
        Serial.printf("[%s] Packet for me (seq=%u) from immediate=%s original_src=%s hop_count=%u payload_len=%u\n", m->getRoleName(), (unsigned)hdr.seq, m->macToStr(mac_addr).c_str(), m->macToStr(hdr.src_mac).c_str(), (unsigned)hdr.hop_count, (unsigned)hdr.payload_len);

        if (hdr.payload_len > 0) {
            const uint8_t *pl = incomingData + sizeof(packet_hdr_t);

            // Check for ACK packet
            if (m->isAckPacket(pl, hdr.payload_len)) {
                Serial.printf("[ACK RECEIVED] from %s for seq=%u (hop_count=%u)\n", 
                              m->macToStr(hdr.src_mac).c_str(), (unsigned)hdr.seq, (unsigned)hdr.hop_count);
                return;  // ACK consumed - don't reply or forward
            }

            // Print payload
            char *tmp = (char*)malloc(hdr.payload_len + 1);
            if (tmp) {
                memcpy(tmp, pl, hdr.payload_len);
                tmp[hdr.payload_len] = '\0';
                Serial.printf("Payload: %s\n", tmp);
                free(tmp);
            }
        }

        // Send ACK back to original sender
        m->sendData("ACK", hdr.src_mac);
        Serial.printf("ACK sent to %s via sendData().\n", m->macToStr(hdr.src_mac).c_str());
        
        return;  // Packet consumed - don't forward it
    }

    // === FORWARDING LOGIC ===
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
        Serial.printf("Flooded broadcast packet (src %s) hop->%u\n", 
                      m->macToStr(hdr.src_mac).c_str(), fwd_hdr->hop_count);
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
            Serial.printf("Direct send to %s failed (%d), falling back to flood.\n", 
                          m->macToStr(hdr.dest_mac).c_str(), r);
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
    
    // Check if we've seen this packet recently (within last 5 seconds)
    for (size_t i = 0; i < 64; ++i) {
        if (!seenPacketsStatic[i].valid) continue;
        
        // Remove old entries (older than 5 seconds)
        if (now - seenPacketsStatic[i].timestamp > 5000) {
            seenPacketsStatic[i].valid = false;
            continue;
        }
        
        // Check for duplicate
        if (memcmp(seenPacketsStatic[i].src_mac, src_mac, 6) == 0 &&
            seenPacketsStatic[i].seq == seq) {
            return true;  // Duplicate found!
        }
    }
    
    // Not a duplicate - record it with critical section protection
    portENTER_CRITICAL(&seenPacketsMux);
    
    uint16_t writeIndex = seenPacketsIndex;
    seenPacketsIndex = (seenPacketsIndex + 1) % 64;
    
    portEXIT_CRITICAL(&seenPacketsMux);
    
    // Write to reserved slot (outside critical section for speed)
    seenPacketsStatic[writeIndex].valid = true;
    memcpy(seenPacketsStatic[writeIndex].src_mac, src_mac, 6);
    seenPacketsStatic[writeIndex].seq = seq;
    seenPacketsStatic[writeIndex].timestamp = now;
    
    return false;
}

// ----- ACK Detection -----
bool ENowMesh::isAckPacket(const uint8_t *payload, uint8_t payload_len) {
    // Check if payload is exactly "ACK"
    if (payload_len != 3) return false;
    return (memcmp(payload, "ACK", 3) == 0);
}