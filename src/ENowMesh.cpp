#include "ENowMesh.h"

// ----- Static storage -----
ENowMesh::PeerInfo ENowMesh::peersStatic[ENowMesh::PEER_TABLE_SIZE] = {};
uint8_t ENowMesh::masterMacStatic[6] = {};
ENowMesh* ENowMesh::instance = nullptr;

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
        case ROLE_MASTER:   return "MASTER";
        case ROLE_REPEATER: return "REPEATER";
        case ROLE_LEAF:     return "LEAF";
        default:             return "UNKNOWN";
    }
}

// ----- Accessors -----
ENowMesh::PeerInfo* ENowMesh::getPeerTable() {
    return peersStatic;
}

uint8_t* ENowMesh::getMasterMac() {
    return masterMacStatic;
}

// ----- WiFi Setup -----
void ENowMesh::initWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    WiFi.macAddress(masterMacStatic);
    Serial.printf("Master MAC: %s\n", macToStr(masterMacStatic).c_str());
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
            memcpy(peersStatic[i].mac, mac, 6);
            peersStatic[i].lastSeen = millis();
            peersStatic[i].valid = true;

            Serial.printf("Added peer %s at slot %u\n", macToStr(mac).c_str(), (unsigned)i);

            esp_now_peer_info_t info = {};
            memcpy(info.peer_addr, mac, 6);
            info.channel = channel;
            info.ifidx = WIFI_IF_STA;
            info.encrypt = 0;

            if (esp_now_add_peer(&info) != ESP_OK)
                Serial.println("Warning: failed to add peer to ESP-NOW (may already exist).");
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

    // --- Build header ---
    packet_hdr_t hdr = {};
    memcpy(hdr.src_mac, masterMacStatic, 6);

    if (dest_mac)
        memcpy(hdr.dest_mac, dest_mac, 6);
    else
        memset(hdr.dest_mac, 0xFF, 6); // broadcast

    hdr.seq = random(0xFFFF);
    hdr.hop_count = 0;
    hdr.payload_len = strlen(msg);

    // --- Build full packet ---
    size_t total = sizeof(packet_hdr_t) + hdr.payload_len;
    uint8_t *buf = (uint8_t*)malloc(total);
    if (!buf) return ESP_ERR_NO_MEM;

    memcpy(buf, &hdr, sizeof(packet_hdr_t));
    memcpy(buf + sizeof(packet_hdr_t), msg, hdr.payload_len);

    // --- Send ---
    esp_err_t result;
    if (dest_mac) {
        result = sendToMac(dest_mac, buf, total);   // unicast
        Serial.printf("[MESH SEND] To %s | len=%u | msg='%s' | result=%d\n", macToStr(dest_mac).c_str(), (unsigned)hdr.payload_len, msg, (int)result);
    } else {
        forwardToPeersExcept(nullptr, buf, total);  // mesh broadcast
        result = ESP_OK;
        Serial.printf("[MESH BROADCAST] len=%u | msg='%s'\n", (unsigned)hdr.payload_len, msg);
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

    if (len < (int)sizeof(packet_hdr_t)) {
        Serial.println("Packet too small. ignoring.");
        m->touchPeer(mac_addr);
        return;
    }

    packet_hdr_t hdr;
    memcpy(&hdr, incomingData, sizeof(packet_hdr_t));

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

    // === PACKET FOR MASTER ===
    if (m->getRole() == ENowMesh::ROLE_MASTER &&
        memcmp(hdr.dest_mac, masterMacStatic, 6) == 0) {
        Serial.printf("[MASTER] Packet for master (seq=%u) from %s original src %s hop_count=%u payload_len=%u\n", (unsigned)hdr.seq, m->macToStr(mac_addr).c_str(), m->macToStr(hdr.src_mac).c_str(), (unsigned)hdr.hop_count, (unsigned)hdr.payload_len);

        if (hdr.payload_len > 0) {
            const uint8_t *pl = incomingData + sizeof(packet_hdr_t);
            char *tmp = (char*)malloc(hdr.payload_len + 1);
            if (tmp) {
                memcpy(tmp, pl, hdr.payload_len);
                tmp[hdr.payload_len] = '\0';
                Serial.printf("Payload: %s\n", tmp);
                free(tmp);
            }
        }

        // Send ACK
        mesh.sendData("ACK", hdr.src_mac);
        Serial.println("ACK sent via sendData().");
    }

    // === FORWARDING ===
    if (hdr.hop_count >= m->maxHops) {
        Serial.println("Max hops reached. Dropping packet.");
        return;
    }

    size_t fwdLen = sizeof(packet_hdr_t) + hdr.payload_len;
    uint8_t *fwdBuf = (uint8_t*)malloc(fwdLen);
    if (!fwdBuf) return;

    memcpy(fwdBuf, incomingData, fwdLen);
    uint8_t *hop_ptr = fwdBuf + offsetof(packet_hdr_t, hop_count);
    (*hop_ptr) = hdr.hop_count + 1;

    // If this node is a LEAF, do not forward the packet.
    if (m->getRole() == ENowMesh::ROLE_LEAF) {
        Serial.println("Role is LEAF — not forwarding packet.");
        free(fwdBuf);
        return;
    }

    // Try direct send to destination if it's a known peer.
    int peerIndex = m->findPeer(hdr.dest_mac);
    if (peerIndex >= 0) {
        esp_err_t r = esp_now_send(ENowMesh::peersStatic[peerIndex].mac, fwdBuf, fwdLen);
        if (r == ESP_OK) {
            Serial.printf("Forwarded directly to %s (src %s dest %s) hop->%u\n", m->macToStr(ENowMesh::peersStatic[peerIndex].mac).c_str(), m->macToStr(hdr.src_mac).c_str(), m->macToStr(hdr.dest_mac).c_str(), hdr.hop_count + 1);
        } else {
            Serial.printf("Direct send to %s failed (%d), falling back to flood.\n", m->macToStr(hdr.dest_mac).c_str(), r);
            m->forwardToPeersExcept(mac_addr, fwdBuf, fwdLen);
            Serial.printf("Flooded packet (src %s dest %s) hop->%u\n", m->macToStr(hdr.src_mac).c_str(), m->macToStr(hdr.dest_mac).c_str(), hdr.hop_count + 1);
        }
    } else {
        // Destination unknown — fall back to flooding to peers (existing behavior).
        m->forwardToPeersExcept(mac_addr, fwdBuf, fwdLen);
        Serial.printf("Flooded packet (src %s dest %s) hop->%u\n", m->macToStr(hdr.src_mac).c_str(), m->macToStr(hdr.dest_mac).c_str(), hdr.hop_count + 1);
    }

    free(fwdBuf);
}
