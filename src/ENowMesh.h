#ifndef ENOWMESH_H
#define ENOWMESH_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

class ENowMesh {
    public:
        // ========================================
        // MESSAGE TYPE FLAGS
        // ========================================
        static constexpr uint8_t MSG_TYPE_DATA       = 0x01;  // Regular data message
        static constexpr uint8_t MSG_TYPE_HELLO      = 0x02;  // Hello beacon
        static constexpr uint8_t MSG_TYPE_ACK        = 0x04;  // Acknowledgment
        static constexpr uint8_t MSG_TYPE_NO_FORWARD = 0x08;  // Don't forward this packet
        static constexpr uint8_t MSG_TYPE_NO_ACK     = 0x10;  // Don't send ACK for this
        static constexpr uint8_t MSG_TYPE_TO_MASTER  = 0x20;  // Route to MASTER node
        static constexpr uint8_t MSG_TYPE_TO_REPEATER = 0x40; // Route to REPEATER node

        // ========================================
        // CONFIGURABLE MESH PARAMETERS
        // ========================================
        // Call these in setup() BEFORE initWiFi() to customize behavior
        
        // --- Network Topology ---
        uint8_t channel = 1;  // WiFi channel (1-13). All nodes must use same channel.
        
        uint8_t maxHops = 6;  
        // Maximum forwarding hops before packet is dropped
        // Recommended: Small mesh (3-5 nodes): 3-4 hops, Medium mesh (5-15 nodes): 5-6 hops, Large mesh (15+ nodes): 7-10 hops, each hop adds ~50-200ms latency
        
        uint16_t maxPeers = 128;  
        // Maximum active peers to track (compile-time fixed, see PEER_TABLE_SIZE)
        // Recommended: Set to expected node count + 20% buffer
        
        // --- Payload Configuration ---
        uint16_t maxPayload = 200;  
        // Maximum payload size in bytes (excluding header)
        // Recommended: Short messages: 100 bytes, General use: 200 bytes, Maximum: 234 bytes (250 - 17 byte header), ESP-NOW limit is 250 bytes total; header uses ~17 bytes
        
        // --- Timing Parameters ---
        uint32_t peerTimeout = 60000UL;  // 60 seconds
        // How long before inactive peer is removed (milliseconds)
        // Recommended: Fast-moving nodes: 30000 (30s), Stationary nodes: 60000-120000 (1-2 min), Low-power nodes: 300000 (5 min)
        
        uint32_t ackTimeout = 2000;  // 2 seconds
        // How long to wait for ACK before retry (milliseconds)
        // Recommended: Low latency mesh: 1000-1500ms, General use: 2000-3000ms, High-hop count: 3000-5000ms, Formula: (maxHops × 500ms) + 500ms buffer
        
        uint8_t maxRetries = 3;  
        // Number of retry attempts for failed unicast messages
        // Recommended: Reliable delivery needed: 3-5, Best-effort: 1-2, Critical messages: 5-7
        
        // --- Duplicate Detection ---
        uint8_t dupDetectBufferSize = 64;  
        // Number of recent packets to remember for duplicate detection
        // Recommended: Light traffic (<5 msg/sec): 32, Medium traffic (5-20 msg/sec): 64, Heavy traffic (>20 msg/sec): 128, Formula: (expected_packets_per_sec × dupDetectWindowMs / 1000) × 1.5, Must not exceed DUP_DETECT_BUFFER_SIZE constant
        
        uint32_t dupDetectWindowMs = 10000;  // 10 seconds
        // Time window for duplicate detection (milliseconds)
        // Recommended: Small mesh (3 hops): 5000ms (5s), Medium mesh (6 hops): 10000ms (10s), Large mesh (10 hops): 15000ms (15s), Formula: (maxHops × 1000ms) + 3000ms safety buffer, Packets older than this are forgotten, MUST be longer than worst-case propagation time!
        
        uint8_t maxPendingMessages = 16;  
        // Maximum simultaneous pending messages awaiting ACK
        // Recommended: Low message rate: 8, General use: 16, High throughput: 32, Must not exceed MAX_PENDING_MESSAGES constant

        // --- Hello Beacon Parameters ---
        uint32_t helloInterval = 15000;  // 15 seconds
        // How often to send HELLO beacons (milliseconds)
        // Recommended: MASTER/REPEATER: 15000-30000ms, LEAF: 60000-120000ms (power saving)

        // ========================================
        // COMPILE-TIME CONSTANTS
        // ========================================
        // These define static array sizes and cannot be changed at runtime
        // Modify these if you need different limits, then recompile
        
        static constexpr size_t PEER_TABLE_SIZE = 128;
        // Static peer table size - increases RAM usage (16 bytes per peer)
        // 128 peers = ~2KB RAM
        
        static constexpr size_t DUP_DETECT_BUFFER_SIZE = 128;
        // Maximum duplicate detection buffer (11 bytes per entry)
        // 128 entries = ~1.4KB RAM
        
        static constexpr size_t MAX_PENDING_MESSAGES = 32;
        // Maximum pending message slots (215 bytes per message)
        // 32 messages = ~6.9KB RAM

        // ========================================
        // NODE ROLE DEFINITION
        // ========================================
        enum NodeRole {
            ROLE_MASTER,    // Initiates messages, never sleeps, full routing
            ROLE_REPEATER,  // Forwards all messages, full routing capability
            ROLE_LEAF       // End device, does NOT forward packets (saves power)
        };

        void setRole(NodeRole r);
        NodeRole getRole() const;
        const char* getRoleName() const;

        // ========================================
        // CORE API
        // ========================================
        
        ENowMesh();  // Constructor

        // Setup functions (call in this order)
        void initWiFi();
        void initEspNow();
        void registerCallbacks();
        void setChannel();

        // Loop functions (call regularly)
        void prunePeers();              // Remove inactive peers
        void checkPendingMessages();     // Handle retries and timeouts
        void sendHelloBeacon();         // Send periodic HELLO beacon

        // Communication
        esp_err_t sendData(const char *msg, const uint8_t *dest_mac = nullptr, uint8_t msg_type = MSG_TYPE_DATA);
        // Send message to specific node (unicast) or all nodes (broadcast if dest_mac=nullptr)
        // Returns: ESP_OK on success, error code otherwise

        // Helper to send message to nodes
        esp_err_t sendToMaster(const char *msg, uint8_t msg_type = MSG_TYPE_DATA);                          // Msg will be received by all masters
        esp_err_t sendToRepeaters(const char *msg, uint8_t msg_type = MSG_TYPE_DATA);                       // Msg will be received by all repeaters
        esp_err_t sendDirect(const char *msg, const uint8_t *dest_mac, uint8_t msg_type = MSG_TYPE_DATA);   // Direct send - no mesh forwarding (1-hop only)


        // ========================================
        // PACKET STRUCTURE
        // ========================================
        typedef struct __attribute__((packed)) {
            uint8_t src_mac[6];      // Original sender MAC
            uint8_t dest_mac[6];     // Destination MAC (0xFF... for broadcast)
            uint16_t seq;            // Sequence number for duplicate detection
            uint8_t hop_count;       // Current hop count (incremented at each hop)
            uint8_t msg_type;        // Message type flags (NEW!)
            uint8_t payload_len;     // Payload length in bytes
        } packet_hdr_t;
        // Total header size: 17 bytes

        // ========================================
        // PEER MANAGEMENT
        // ========================================
        struct PeerInfo {
            uint8_t mac[6];
            uint32_t lastSeen;
            bool valid;
        };

        PeerInfo* getPeerTable();        // Access peer table
        uint8_t* getNodeMac();           // Get this node's MAC address
        String macToStr(const uint8_t *mac);  // Helper: MAC to string

        int findPeer(const uint8_t *mac);
        void touchPeer(const uint8_t *mac);

        // ========================================
        // LOW-LEVEL SEND (Advanced Users)
        // ========================================
        esp_err_t sendToMac(const uint8_t *mac, const uint8_t *data, size_t len);
        void forwardToPeersExcept(const uint8_t *exclude_mac, const uint8_t *data, size_t len);

        // ========================================
        // STATIC CALLBACKS (Internal Use)
        // ========================================
        static void OnDataSent(const esp_now_send_info_t *info, esp_now_send_status_t status);
        static void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len);

        // ========================================
        // USER CALLBACK
        // ========================================
        // Called when a message destined for this node is received
        typedef void (*MessageCallback)(const uint8_t *src_mac, const char *payload, size_t len);
        void setMessageCallback(MessageCallback cb);

    private:
        // ========================================
        // INTERNAL STRUCTURES
        // ========================================
        
        // Duplicate detection
        struct SeenPacket {
            uint8_t src_mac[6];
            uint16_t seq;
            uint32_t timestamp;
            bool valid;
        };
        
        // Pending message tracking (for ACK/retry)
        struct PendingMessage {
            uint8_t dest_mac[6];
            uint16_t seq;
            uint32_t sendTime;
            uint8_t retryCount;
            char payload[233];  // Max: 250 - 17 byte header
            uint8_t payloadLen;
            bool waiting;
        };

        // User message callback
        MessageCallback userCallback = nullptr;

        // ========================================
        // STATIC STORAGE
        // ========================================
        static PeerInfo peersStatic[PEER_TABLE_SIZE];
        static uint8_t myMacStatic[6];
        static ENowMesh* instance;
        static uint16_t sequenceCounter;
        
        static SeenPacket seenPacketsStatic[DUP_DETECT_BUFFER_SIZE];
        static uint16_t seenPacketsIndex;
        static portMUX_TYPE seenPacketsMux;
        
        static PendingMessage pendingMessages[MAX_PENDING_MESSAGES];
        static portMUX_TYPE pendingMux;

        // ========================================
        // INTERNAL STATE
        // ========================================
        NodeRole role = ROLE_MASTER;
        uint32_t lastHelloTime = 0;  // Track last HELLO beacon time

        // ========================================
        // HELPER METHODS
        // ========================================
        bool isDuplicate(const uint8_t *src_mac, uint16_t seq);
        const char* msgTypeToStr(uint8_t msg_type);  // Helper for debug logging
};

#endif