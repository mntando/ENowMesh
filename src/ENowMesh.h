#ifndef ENOWMESH_H
#define ENOWMESH_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

class ENowMesh {
    public:
        // ----- Public configuration -----
        uint8_t channel = 1;
        uint16_t maxPeers = 128;            // runtime-configurable copy (for API)
        uint8_t maxHops = 6;
        uint16_t maxPayload = 200;
        uint32_t peerTimeout = 60000UL;

        // compile-time peer table size used for static arrays
        static constexpr size_t PEER_TABLE_SIZE = 128;

        // ===== Node Role Definition =====
        enum NodeRole {
            ROLE_MASTER,
            ROLE_REPEATER,
            ROLE_LEAF
        };

        void setRole(NodeRole r);
        NodeRole getRole() const;
        const char* getRoleName() const;

        // Constructor
        ENowMesh();

        // Setup functions
        void initWiFi();
        void initEspNow();
        void registerCallbacks();
        void setChannel();

        // === GLOBAL STRUCTURES ===
        typedef struct __attribute__((packed)) {
            uint8_t src_mac[6];
            uint8_t dest_mac[6];
            uint16_t seq;
            uint8_t hop_count;
            uint8_t payload_len;
        } packet_hdr_t;

        struct PeerInfo {
            uint8_t mac[6];
            uint32_t lastSeen;
            bool valid;
        };

        PeerInfo *getPeerTable();            // gives access to global peers[]
        uint8_t* getNodeMac();             // access node MAC
        String macToStr(const uint8_t *mac); // helper

        // Peer and routing functions (unchanged)
        int findPeer(const uint8_t *mac);
        void touchPeer(const uint8_t *mac);
        void prunePeers();
        esp_err_t sendToMac(const uint8_t *mac, const uint8_t *data, size_t len);
        void forwardToPeersExcept(const uint8_t *exclude_mac, const uint8_t *data, size_t len);
        
        // Communication
        esp_err_t sendData(const char *msg, const uint8_t *dest_mac = nullptr);

        // === STATIC CALLBACK ENTRYPOINTS ===
        static void OnDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status);
        static void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len);

        // ACK detection
        static constexpr const char* ACK_MESSAGE = "ACK";
        
    private:
        // internal global replacements
        static PeerInfo peersStatic[PEER_TABLE_SIZE];
        static uint8_t myMacStatic[6];
        static ENowMesh *instance;  // allow callbacks to access class
        NodeRole role = ROLE_MASTER;  // default to MASTER if not set
        static uint16_t sequenceCounter;  // Per-node sequence number

        // Duplicate detection structures
        struct SeenPacket {
            uint8_t src_mac[6];
            uint16_t seq;
            uint32_t timestamp;
            bool valid;
        };
        
        static SeenPacket seenPacketsStatic[64];  // circular buffer for seen packets
        static uint16_t seenPacketsIndex;          // current write position
        static portMUX_TYPE seenPacketsMux;  // Spinlock for interrupt safety
        
        bool isDuplicate(const uint8_t *src_mac, uint16_t seq);     // check for duplicate packets helper funtion

        // ACK detection
        bool isAckPacket(const uint8_t *payload, uint8_t payload_len);
};

extern ENowMesh mesh;  // global instance

#endif
