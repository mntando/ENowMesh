# ENowMesh Library

A flexible ESP-NOW mesh networking library for ESP32 devices that enables reliable multi-hop communication with configurable node roles, duplicate detection, ACK/retry mechanisms, and automatic peer discovery.

## Features

- **Multi-hop Mesh Networking**: Packets automatically forward through the network to reach distant nodes
- **Three Node Roles**:
  - **MASTER**: Initiates messages, full routing capability
  - **REPEATER**: Forwards all messages, ideal for intermediate nodes
  - **LEAF**: End devices that don't forward packets (power-saving)
- **Automatic Peer Discovery**: HELLO beacons for network topology awareness
- **Duplicate Detection**: Tracks recently seen packets to prevent loops
- **ACK/Retry Mechanism**: Unicast messages can request acknowledgments with automatic retries
- **Message Type Flags**: DATA, HELLO, ACK, NO_FORWARD, NO_ACK
- **Configurable Parameters**: Customize mesh behavior at runtime without recompiling most settings
- **Thread-Safe Operations**: Critical sections protect shared state

## Installation

1. Download or clone this library to your Arduino libraries folder:
   ```
   ~/Documents/Arduino/libraries/ENowMesh/
   ```

2. Restart the Arduino IDE

3. The library will appear under **Sketch > Include Library > ENowMesh**

## Quick Start

### Basic Setup (Repeater Node)

```cpp
#include "ENowMesh.h"

ENowMesh mesh;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Configure as REPEATER (forwards all messages)
  mesh.setRole(ENowMesh::ROLE_REPEATER);
  
  // Initialize mesh
  mesh.initWiFi();
  mesh.setChannel();
  mesh.initEspNow();
  mesh.registerCallbacks();
  
  Serial.println("Mesh node ready!");
}

void loop() {
  mesh.prunePeers();           // Clean up inactive peers
  mesh.sendHelloBeacon();      // Advertise presence
  mesh.checkPendingMessages(); // Handle ACK retries
  
  delay(100);
}
```

### Sending Messages

**Broadcast (to all nodes):**
```cpp
mesh.sendData("Hello everyone!");
```

**Unicast (to specific node):**
```cpp
uint8_t targetMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
mesh.sendData("Hello specific node", targetMac);
```

**With custom message type:**
```cpp
// Send broadcast without expecting ACK
mesh.sendData("Status update", nullptr, ENowMesh::MSG_TYPE_NO_ACK);
```

## Configuration

All configuration is done via public member variables. Set these **before** calling `initWiFi()`:

### Network Topology
```cpp
mesh.channel = 1;           // WiFi channel (1-13), must match all nodes
mesh.maxHops = 6;           // Maximum forwarding hops
mesh.maxPeers = 128;        // Maximum active peers to track
```

### Payload & Timing
```cpp
mesh.maxPayload = 200;      // Max payload size (excluding 17-byte header)
mesh.peerTimeout = 60000;   // Remove peer if silent for 60 seconds
mesh.ackTimeout = 2000;     // Wait 2 seconds for ACK before retry
mesh.maxRetries = 3;        // Retry failed unicasts up to 3 times
```

### Duplicate Detection
```cpp
mesh.dupDetectBufferSize = 64;   // Remember 64 recent packets
mesh.dupDetectWindowMs = 10000;  // Consider packets as duplicates for 10 seconds
```

### HELLO Beacons
```cpp
mesh.helloInterval = 15000;  // Send HELLO every 15 seconds
// For LEAF nodes (power saving):
// mesh.helloInterval = 60000;  // Send HELLO every 60 seconds
```

### Pending Messages
```cpp
mesh.maxPendingMessages = 16;  // Track up to 16 messages awaiting ACK
```

## API Reference

### Core Setup
```cpp
void initWiFi();           // Initialize WiFi in STA mode
void initEspNow();         // Initialize ESP-NOW protocol
void registerCallbacks();  // Register send/receive callbacks
void setChannel();         // Set WiFi channel for mesh
```

### Loop Operations
```cpp
void prunePeers();              // Remove inactive peers
void sendHelloBeacon();         // Send periodic HELLO beacon
void checkPendingMessages();    // Handle ACK timeouts and retries
```

### Communication
```cpp
// Send message (broadcast if dest_mac=nullptr)
esp_err_t sendData(const char *msg, const uint8_t *dest_mac = nullptr, 
                   uint8_t msg_type = MSG_TYPE_DATA);

// Low-level send (advanced users)
esp_err_t sendToMac(const uint8_t *mac, const uint8_t *data, size_t len);
void forwardToPeersExcept(const uint8_t *exclude_mac, const uint8_t *data, size_t len);
```

### Node Role Management
```cpp
void setRole(NodeRole role);      // Set node role (MASTER/REPEATER/LEAF)
NodeRole getRole() const;         // Get current role
const char* getRoleName() const;  // Get role as string ("MASTER", "REPEATER", "LEAF")
```

### Peer Management
```cpp
PeerInfo* getPeerTable();         // Access the peer table
uint8_t* getNodeMac();            // Get this node's MAC address
int findPeer(const uint8_t *mac); // Find peer by MAC
void touchPeer(const uint8_t *mac); // Register/update peer
```

### Utilities
```cpp
String macToStr(const uint8_t *mac);  // Convert MAC address to "XX:XX:XX:XX:XX:XX"
```

## Packet Structure

Each packet consists of a 17-byte header + payload:

```
struct packet_hdr_t {
  uint8_t src_mac[6];      // Original sender MAC
  uint8_t dest_mac[6];     // Destination MAC (0xFF... for broadcast)
  uint16_t seq;            // Sequence number (duplicate detection)
  uint8_t hop_count;       // Current hop count
  uint8_t msg_type;        // Message type flags
  uint8_t payload_len;     // Payload length
};
// Total: 17 bytes header + up to 233 bytes payload = 250 bytes max (ESP-NOW limit)
```

## Message Types

Message types are flags that can be combined:

| Flag | Value | Purpose |
|------|-------|---------|
| `MSG_TYPE_DATA` | 0x01 | Regular data message |
| `MSG_TYPE_HELLO` | 0x02 | Hello beacon (peer discovery) |
| `MSG_TYPE_ACK` | 0x04 | Acknowledgment |
| `MSG_TYPE_NO_FORWARD` | 0x08 | Don't forward this packet |
| `MSG_TYPE_NO_ACK` | 0x10 | Don't send ACK for this |

**Example:**
```cpp
// Send a message that won't be forwarded and doesn't need ACK
mesh.sendData("Control message", nullptr, 
              ENowMesh::MSG_TYPE_NO_FORWARD | ENowMesh::MSG_TYPE_NO_ACK);
```

## Node Roles Explained

### MASTER
- Initiates messages
- Forwards all packets
- Never enters sleep mode
- Full routing capability
- Use for: Central hub, always-on gateway

### REPEATER
- Forwards all received messages
- Full routing capability
- Can send and receive
- Use for: Intermediate routing nodes, WiFi extenders

### LEAF
- **Does NOT forward** packets (power-saving)
- Can send and receive messages
- Useful for battery-powered devices
- Other nodes won't use it for routing

## Recommended Configuration Examples

### Small Mesh (3-5 nodes)
```cpp
mesh.maxHops = 3;
mesh.helloInterval = 15000;
mesh.dupDetectBufferSize = 32;
mesh.dupDetectWindowMs = 5000;
```

### Medium Mesh (5-15 nodes)
```cpp
mesh.maxHops = 6;
mesh.helloInterval = 20000;
mesh.dupDetectBufferSize = 64;
mesh.dupDetectWindowMs = 10000;
```

### Large Mesh (15+ nodes)
```cpp
mesh.maxHops = 10;
mesh.helloInterval = 30000;
mesh.dupDetectBufferSize = 128;
mesh.dupDetectWindowMs = 15000;
```

### Low-Power Configuration (Battery Nodes)
```cpp
mesh.setRole(ENowMesh::ROLE_LEAF);    // Don't forward packets
mesh.helloInterval = 60000;            // Send HELLO every minute
mesh.maxPayload = 100;                 // Smaller messages
mesh.peerTimeout = 300000;             // 5 minute peer timeout
```

## Examples

The library includes several examples in the `examples/` folder:

- **basic/**: Simple unicast and broadcast
- **custom_configuration/**: Advanced parameter tuning
- **master/**: Master node behavior
 - **multi_censor/**: Multiple sensor nodes reporting
- **node/**: Simple repeater node
- **unicast/**: Point-to-point communication
 - **queued_messages/**: Reliable unicast with ACK/retry (pending queue)

## Queued Messages (Reliable Unicast with Retries)

This example demonstrates how to send unicast messages that the library will track in a pending queue until an ACK is received or retries are exhausted. The mesh automatically tracks pending unicast messages when you call `sendData()` with a `dest_mac` and the `MSG_TYPE_NO_ACK` flag is not set. Call `checkPendingMessages()` regularly in `loop()` so the library can retry timed-out messages and clear completed entries.

Sender example:

```cpp
#include "ENowMesh.h"

ENowMesh mesh;
// Replace with the destination node's MAC address
uint8_t destMac[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };

void setup() {
  Serial.begin(115200);

  // Tune reliability parameters BEFORE initWiFi()
  mesh.ackTimeout = 2000;       // 2s wait for ACK before retry
  mesh.maxRetries = 3;          // retry up to 3 times
  mesh.maxPendingMessages = 8;  // how many messages to track simultaneously

  mesh.setRole(ENowMesh::ROLE_REPEATER);
  mesh.initWiFi();
  mesh.setChannel();
  mesh.initEspNow();
  mesh.registerCallbacks();
}

void loop() {
  // Send a reliable unicast; library will enqueue it for ACK tracking
  esp_err_t r = mesh.sendData("Important command", destMac);
  if (r != ESP_OK) Serial.printf("sendData failed: %d\n", (int)r);

  // Must call regularly so pending messages are retried / timed out
  mesh.checkPendingMessages();

  // Keep peer table and other housekeeping running
  mesh.prunePeers();
  mesh.sendHelloBeacon();

  delay(1000);
}
```

Receiver notes:
- The library will automatically generate and send ACK packets for unicast messages (unless `MSG_TYPE_NO_ACK` is set).
- ACK handling and clearing of pending entries is done internally; you can monitor Serial logs (OnDataSent / OnDataRecv) to observe ACKs and retries.

Tuning hints:
- Increase `ackTimeout` for deeper/wider meshes (formula: `(maxHops * 500ms) + 500ms`).
- Increase `maxRetries` for unstable links or critical messages.
- Ensure `mesh.maxPendingMessages` is large enough for your expected concurrent unicast burst.

## Compile-Time Constants

These constants define static array sizes and cannot be changed at runtime:

```cpp
static constexpr size_t PEER_TABLE_SIZE = 128;         // ~2KB RAM
static constexpr size_t DUP_DETECT_BUFFER_SIZE = 128;  // ~1.4KB RAM
static constexpr size_t MAX_PENDING_MESSAGES = 32;     // ~6.9KB RAM
```

To modify these limits, edit `ENowMesh.h` and recompile.

## Troubleshooting

### Nodes Not Discovering Each Other
- Verify all nodes are on the **same WiFi channel**: `mesh.channel`
- Check that `sendHelloBeacon()` is called regularly in `loop()`
- Look for "HELLO RECEIVED" messages in Serial output

### Messages Not Being Forwarded
- Verify node roles: use `mesh.getRoleName()` in Serial output
- LEAF nodes don't forward—use REPEATER or MASTER for intermediate nodes
- Check `maxHops` isn't too low
- Verify `MSG_TYPE_NO_FORWARD` flag isn't set unintentionally

### ACK Timeouts/Retries
- Check `ackTimeout` is long enough for mesh depth: `(maxHops × 500ms) + 500ms`
- Monitor Serial for "Send FAILED" messages
- Increase `maxRetries` if nodes are unstable
- Check that destination node is reachable

### High Duplicate Detection False Positives
- Increase `dupDetectWindowMs` if mesh is large
- Formula: `(maxHops × 1000ms) + 3000ms`

## Serial Debug Output

Enable Serial for detailed mesh activity:

```
[HELLO BEACON] Sent to all peers: HELLO:REPEATER
[MESH SEND] To AA:BB:CC:DD:EE:FF | type=DATA | len=13 | msg='Hello world' | result=0
[RECV] type=DATA | from=AA:BB:CC:DD:EE:FF | seq=12345 | hop=1
[MESH BROADCAST] type=DATA | len=20 | msg='Broadcast message'
Flooded broadcast packet (src AA:BB:CC:DD:EE:FF) hop->2
```

## Performance Notes

- ESP-NOW range: ~250 meters line-of-sight
- Latency per hop: ~50-200ms
- Maximum theoretical throughput: ~30-50 messages/second (depends on payload size)
- Mesh stability improves with 50-100ms loop delay
- Consider mesh density: too many nodes = more collisions

## License

[Add your license information here]

## Support

For issues, feature requests, or questions, please refer to the examples or contact the library maintainer.

## Changelog

### v1.0.0
- Initial release
- Multi-hop mesh networking
- Three node roles (MASTER, REPEATER, LEAF)
- ACK/retry mechanism
- Duplicate detection
- HELLO beacon discovery
