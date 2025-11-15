# ENowMesh

**ESP-NOW Mesh Networking Library for ESP32**

A lightweight, self-organizing mesh network library using Espressif's ESP-NOW protocol. Build robust multi-hop networks without WiFi infrastructure - perfect for IoT sensors, home automation, and distributed systems.

## Features

- **Self-Organizing Mesh** - Nodes automatically discover peers and route messages through multiple hops
- **Three Node Roles** - MASTER (hub), REPEATER (router), LEAF (end device)
- **Reliable Delivery** - Automatic ACK/retry mechanism for unicast messages
- **Smart Routing** - Direct unicast when possible, intelligent flooding fallback
- **Role-Based Routing** - Send messages specifically to MASTER or REPEATER nodes
- **Duplicate Detection** - Prevents message loops in the mesh
- **Configurable** - Tune hop limits, timeouts, retries, and more
- **Lightweight** - Minimal memory footprint, runs on ESP32 with ~10KB RAM

## Why ESP-NOW Mesh?

- **No WiFi Router Needed** - Direct device-to-device communication
- **Low Latency** - Typical 50-200ms per hop (vs. WiFi + server roundtrip)
- **Long Range** - Up to 200m+ outdoor (ESP-NOW's range)
- **Low Power** - Sleep-friendly for battery nodes
- **Simple API** - Start meshing in 10 lines of code

## Hardware Requirements

- **ESP32** (any variant: ESP32, ESP32-S2, ESP32-S3, ESP32-C3)
- Minimum 2 nodes to form a mesh
- All nodes must use the **same WiFi channel**

## Installation

### Arduino IDE
1. Download this repository as ZIP
2. Sketch → Include Library → Add .ZIP Library
3. Select the downloaded ZIP file

### PlatformIO
```ini
lib_deps = 
    https://github.com/yourusername/ENowMesh.git
```

## Quick Start

### Basic Node (10 Lines)

```cpp
#include "ENowMesh.h"
ENowMesh mesh;

void setup() {
    Serial.begin(115200);
    mesh.initWiFi();
    mesh.initEspNow();
    mesh.setChannel();
    mesh.registerCallbacks();
}

void loop() {
    mesh.sendData("Hello mesh!");
    mesh.sendHelloBeacon();
    mesh.checkPendingMessages();
    mesh.prunePeers();
    delay(10000);
}
```

### Receiving Messages

```cpp
void onMessage(const uint8_t *src_mac, const char *payload, size_t len) {
    Serial.printf("Received: %s\n", payload);
}

void setup() {
    // ... init code ...
    mesh.setMessageCallback(onMessage);
}
```

## Node Roles

Configure role **before** `initWiFi()`:

```cpp
// MASTER - Hub node, never sleeps, initiates commands
mesh.setRole(ENowMesh::ROLE_MASTER);

// REPEATER - Routes all messages, extends range (default)
mesh.setRole(ENowMesh::ROLE_REPEATER);

// LEAF - End device, does NOT forward (power saving)
mesh.setRole(ENowMesh::ROLE_LEAF);
```

**Role Behavior:**
- **MASTER**: Processes `sendToMaster()` messages, full routing
- **REPEATER**: Forwards all messages, processes `sendToRepeaters()`
- **LEAF**: Never forwards (saves power/bandwidth)

**Runtime Role Changes:**
Node roles can be changed at runtime using `setRole()`:
```cpp
// Receive command to change role
if (strcmp(payload, "CMD:PROMOTE_MASTER") == 0) {
    mesh.setRole(ENowMesh::ROLE_MASTER);
    mesh.sendHelloBeacon();  // Announce new role immediately
}
```
**NOTE:**  
**Safe transitions:** REPEATER ↔ MASTER (both are routers)  
**Avoid:** MASTER/REPEATER → LEAF (breaks mesh routing, may partition network)

## Sending Messages

### 1. Broadcast to Everyone
```cpp
mesh.sendData("Hello everyone!");
```

### 2. Send to Any MASTER Node (Anycast)
```cpp
mesh.sendToMaster("Data for master");
// First MASTER in path processes it, packet dropped after
```

### 3. Send to All REPEATER Nodes (Multicast)
```cpp
mesh.sendToRepeaters("Update all routers");
// Every REPEATER processes AND forwards it
```

### 4. Unicast to Specific MAC
```cpp
uint8_t targetMAC[] = {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF};
mesh.sendData("Private message", targetMAC);
// Automatic ACK/retry, routed through mesh if needed
```

### 5. Custom Message Types
```cpp
// No ACK required (fire-and-forget)
mesh.sendData("Sensor reading", nullptr, ENowMesh::MSG_TYPE_DATA | ENowMesh::MSG_TYPE_NO_ACK);

// Don't forward (local broadcast only)
mesh.sendData("Local only", nullptr, ENowMesh::MSG_TYPE_DATA | ENowMesh::MSG_TYPE_NO_FORWARD);
```

## Configuration

Customize **before** `initWiFi()`:

```cpp
void setup() {
    // Network topology
    mesh.channel = 6;              // WiFi channel (all nodes must match!)
    mesh.maxHops = 8;              // Max forwarding hops (default: 6)
    
    // Payload size
    mesh.maxPayload = 200;         // Max message bytes (default: 200, max: 234)
    
    // Timing
    mesh.peerTimeout = 60000;      // Remove inactive peers after 60s
    mesh.ackTimeout = 2000;        // Wait 2s for ACK before retry
    mesh.maxRetries = 3;           // Retry failed sends 3 times
    mesh.helloInterval = 15000;    // Send HELLO beacon every 15s
    
    // Duplicate detection
    mesh.dupDetectBufferSize = 64; // Remember last 64 packets
    mesh.dupDetectWindowMs = 10000; // Forget packets older than 10s
    
    mesh.initWiFi();
    // ... rest of init ...
}
```

### Configuration Guidelines

| Parameter | Small Mesh (3-5 nodes) | Medium (5-15) | Large (15+) |
|-----------|------------------------|---------------|-------------|
| `maxHops` | 3-4 | 5-6 | 7-10 |
| `ackTimeout` | 1500ms | 2000ms | 3000ms+ |
| `dupDetectBufferSize` | 32 | 64 | 128 |
| `dupDetectWindowMs` | 5000ms | 10000ms | 15000ms |

**Formula for `ackTimeout`:** `(maxHops × 500ms) + 500ms buffer`  
**Formula for `dupDetectWindowMs`:** `(maxHops × 1000ms) + 3000ms safety`

## Message Queue Pattern (Important!)

**Never block the receive callback!** ESP-NOW callbacks run in interrupt context.

### BAD (Blocks ESP-NOW)
```cpp
void onMessage(const uint8_t *src_mac, const char *payload, size_t len) {
    delay(1000);  // BREAKS MESH! Drops packets
    readSensor();  // Blocks other messages
}
```

### GOOD (Queue for Processing)
```cpp
// See examples/queued_messages.ino for full implementation
struct ReceivedMessage {
    uint8_t src_mac[6];
    char payload[200];
    size_t len;
};
ReceivedMessage queue[10];

void onMessage(...) {
    // Just queue it, return fast
    queue[writePos++] = {...};
}

void loop() {
    processMessages();  // Handle slowly here
}
```

**Use queue when handling requires:**
- `delay()` calls
- Sensor reads (DHT, ultrasonic, etc.)
- Display updates (OLED, LCD)
- Multiple `sendData()` replies
- Complex processing

See `examples/queued_messages.ino` for complete pattern.

## API Reference

### Core Methods

```cpp
// Setup (call in order)
void initWiFi();
void initEspNow();
void setChannel();
void registerCallbacks();
void setMessageCallback(MessageCallback cb);

// Loop maintenance (call regularly)
void sendHelloBeacon();       // Send peer discovery beacon
void checkPendingMessages();  // Handle ACK retries
void prunePeers();            // Remove stale peers

// Sending
esp_err_t sendData(const char *msg, const uint8_t *dest_mac = nullptr, uint8_t msg_type = MSG_TYPE_DATA);
esp_err_t sendToMaster(const char *msg, uint8_t msg_type = MSG_TYPE_DATA);
esp_err_t sendToRepeaters(const char *msg, uint8_t msg_type = MSG_TYPE_DATA);

// Peer management
int findPeer(const uint8_t *mac);
PeerInfo* getPeerTable();
uint8_t* getNodeMac();

// Role management
void setRole(NodeRole r);
NodeRole getRole() const;
const char* getRoleName() const;
```

### Message Type Flags

```cpp
MSG_TYPE_DATA        // Regular data message (default)
MSG_TYPE_HELLO       // Hello beacon (auto-handled)
MSG_TYPE_ACK         // Acknowledgment (auto-handled)
MSG_TYPE_NO_FORWARD  // Don't forward this packet
MSG_TYPE_NO_ACK      // Don't send ACK (fire-and-forget)
MSG_TYPE_TO_MASTER   // Route to MASTER nodes
MSG_TYPE_TO_REPEATER // Route to REPEATER nodes
```

Combine with `|`: `MSG_TYPE_DATA | MSG_TYPE_NO_ACK`

## Examples

### Master-Leaf System
```
[MASTER] ←→ [REPEATER] ←→ [REPEATER] ←→ [LEAF]
  (hub)      (router)      (router)    (sensor)
```

**Master** (`examples/master.ino`):
- Sends commands to leaf nodes
- Receives sensor data
- Forwards to cloud/server

**Leaf** (`examples/node.ino`):
- Reads sensors periodically
- Sends data to master with `sendToMaster()`
- Low power sleep possible (doesn't forward)

### Multi-Repeater Broadcast
```cpp
// On MASTER
mesh.sendToRepeaters("CONFIG:interval=30");

// All REPEATERs receive and forward to reach distant repeaters
```

## Packet Structure

```cpp
struct packet_hdr_t {
    uint8_t src_mac[6];      // Original sender
    uint8_t dest_mac[6];     // Destination (0xFF... = broadcast)
    uint16_t seq;            // Sequence number (duplicate detection)
    uint8_t hop_count;       // Current hops (incremented each forward)
    uint8_t msg_type;        // Message type flags
    uint8_t payload_len;     // Payload length (0-234 bytes)
};
// Header: 17 bytes
// Max total packet: 250 bytes (ESP-NOW limit)
// Max payload: 234 bytes
```

## Troubleshooting

### Messages Not Being Received
1. **Check WiFi channel** - All nodes must use same channel
2. **Check range** - ESP-NOW range is ~50-200m (walls reduce it)
3. **Check `maxHops`** - Increase if nodes are far apart
4. **Enable debug** - Watch Serial output for packet flow

### High Packet Loss
1. **Reduce broadcast frequency** - Too many broadcasts flood the mesh
2. **Increase `dupDetectWindowMs`** - Packets arriving late get dropped
3. **Check peer table** - May be full (`PEER_TABLE_SIZE = 128`)
4. **Reduce `maxPayload`** - Smaller packets = more reliable

### ACK Timeouts
1. **Increase `ackTimeout`** - Formula: `(maxHops × 500) + 500ms`
2. **Check route** - Use Serial debug to see hop count
3. **Check LEAF nodes** - LEAFs don't forward, may block routes

### Memory Issues
```cpp
// Reduce static buffers in ENowMesh.h
static constexpr size_t PEER_TABLE_SIZE = 64;  // Was 128
static constexpr size_t DUP_DETECT_BUFFER_SIZE = 64;  // Was 128
static constexpr size_t MAX_PENDING_MESSAGES = 16;  // Was 32
```

### Duplicate Packets
- Normal in mesh networks! Handled automatically.
- If seeing "DUPLICATE" in logs, it's working correctly.
- Increase `dupDetectWindowMs` if seeing false duplicates.

## Performance Characteristics ***(theoretical)***

| Metric | Value |
|--------|-------|
| Latency (per hop) | 50-200ms typical |
| Range (outdoor) | 100-250m |
| Range (indoor) | 30-100m (walls reduce) |
| Max payload | 234 bytes |
| Max packet size | 250 bytes |
| Throughput | ~10-50 packets/sec per node |
| Power (TX) | ~120mA @ 3.3V |
| Power (RX) | ~80mA @ 3.3V |

## Best Practices

1. **Call maintenance functions regularly** in `loop()`:
   ```cpp
   mesh.sendHelloBeacon();
   mesh.checkPendingMessages();
   mesh.prunePeers();
   ```

2. **Keep callbacks fast** - Queue messages for slow processing

3. **Set appropriate `maxHops`** - Balance latency vs. coverage

4. **Use unicast for important data** - Gets ACK/retry

5. **Use broadcast for sensor readings** - No ACK overhead

6. **Test range before deployment** - ESP-NOW range varies by environment

7. **Monitor Serial output** - Shows packet flow and issues

## Potential Improvements

### Routing Tables
Currently uses **flood routing** - packets broadcast to all peers when destination unknown. This works reliably but consumes bandwidth.

**Planned improvement:**
- Maintain routing table mapping destination MACs to next-hop neighbors
- Learn routes from packet headers (source routing)
- Fallback to flooding when route unknown
- **Benefit:** Reduced broadcasts, lower latency, better scalability for large meshes (20+ nodes)

**Tradeoff:** ~1-2KB additional RAM for routing table storage

### Encryption
ESP-NOW supports AES-128 encryption, but ENowMesh currently uses **unencrypted mode** for simplicity and compatibility.

**Planned improvement:**
- Optional encrypted peer pairing
- Pre-shared key (PSK) mode for entire mesh
- Per-message encryption with shared mesh key

**Implementation considerations:**
- Encrypted mode limits peer count (10 encrypted vs 20 unencrypted on ESP32)
- Key distribution mechanism needed for dynamic peers
- Performance impact: ~5-10ms encryption overhead per packet

**Workaround for now:** Implement application-layer encryption in your payload before calling `sendData()`.

### Other Potential Features
- **Mesh health metrics** - Track packet loss, hop counts, peer stability
- **Quality of Service (QoS)** - Priority queues for critical messages
- **Sleep coordination** - Wake-on-demand for low-power LEAF nodes
- **Bridge mode** - Gateway between ESP-NOW mesh and WiFi/MQTT

Contributions welcome! Open an issue to discuss implementation.

## License

MIT License - See LICENSE file

## Contributing

Pull requests welcome! Please include examples demonstrating new features.

## Credits

Built on Espressif's ESP-NOW protocol.