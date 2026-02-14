# Project 37: Order Gateway Distribution - BBO Multi-Protocol Gateway

This project is part of a complete end-to-end trading system:
- **Main Repository:** [fpga-trading-systems](https://github.com/adilsondias-engineer/fpga-trading-systems)
- **Project Number:** 37 of 38
- **Category:** Software (C++20)
- **Dependencies:** Project 36 - Ultra Low Latency RX (DPDK Kernel Bypass)

---

**Platform:** Linux
**Technology:** C++20, Boost.Asio, MQTT (libmosquitto), Kafka (librdkafka), LMAX Disruptor
**Status:** Implemented

---

## Overview

BBO (Best Bid/Offer) distribution gateway that reads from shared memory and distributes via TCP, MQTT, and Kafka. This project completes the ultra-low-latency data pipeline:

**Data Flow:**
```
[Project 36] ──Shared Memory──→ [Project 37] ──→ TCP/MQTT/Kafka ──→ Clients
(DPDK RX)      (Disruptor)      (Distribution)
```

**Architecture Separation:**
- **Project 36**: Ultra-low-latency critical path (DPDK → BBO Parse → Shared Memory)
- **Project 37**: Distribution path (Shared Memory → TCP/MQTT/Kafka)

This separation allows Project 36 to achieve minimal latency variance by offloading distribution to a separate process.

**Trading Relevance:** Distributes market data to multiple consumers (trading terminals, IoT displays, mobile apps, analytics) without impacting the critical path latency.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│              PROJECT 37 (DISTRIBUTION GATEWAY)                  │
│                                                                 │
│  ┌───────────────┐                                              │
│  │ Shared Memory │   ┌─────────────────────────────────────┐   │
│  │ (Disruptor)   │──→│ Distribution Thread                 │   │
│  │               │   │                                     │   │
│  │ /dev/shm/     │   │  ┌─────────┐ ┌─────────┐ ┌───────┐ │   │
│  │ bbo_ring_*    │   │  │   TCP   │ │  MQTT   │ │ Kafka │ │   │
│  └───────────────┘   │  │ Server  │ │ Publish │ │ Prod  │ │   │
│                      │  └────┬────┘ └────┬────┘ └───┬───┘ │   │
│                      │       │           │          │     │   │
│                      └───────┼───────────┼──────────┼─────┘   │
│                              │           │          │         │
└──────────────────────────────┼───────────┼──────────┼─────────┘
                               │           │          │
                               ▼           ▼          ▼
                         ┌─────────┐ ┌─────────┐ ┌─────────┐
                         │  Java   │ │  ESP32  │ │ Future  │
                         │ Desktop │ │  IoT    │ │Analytics│
                         └─────────┘ └─────────┘ └─────────┘
```

---

## Features

### Input: Shared Memory (Disruptor)
- Connects to existing shared memory created by Project 36
- Lock-free polling with configurable timeout
- Automatic reconnection on shared memory failure

### Output: Multi-Protocol Distribution

| Protocol | Use Case | Clients |
|----------|----------|---------|
| **TCP** | Java Desktop trading terminal | Low-latency local apps |
| **MQTT** | ESP32 IoT display, Mobile apps | Lightweight remote clients |
| **Kafka** | Analytics, Data persistence | Backend services |

### Performance
- Single distribution thread
- Optional real-time scheduling (SCHED_FIFO)
- CPU core pinning
- Quiet mode for benchmarking

---

## Building

### Prerequisites

- CMake 3.16+
- GCC 11+ or Clang 14+
- Boost 1.74+ (system, thread)
- libmosquitto 2.0+
- librdkafka 2.6+
- nlohmann-json 3.11+
- spdlog

### Dependencies (Ubuntu/Debian)

```bash
sudo apt-get install -y \
    libboost-system-dev libboost-thread-dev \
    libmosquitto-dev librdkafka-dev \
    nlohmann-json3-dev libspdlog-dev
```

### Build Commands

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

---

## Usage

### Basic Usage

```bash
# Use default config.json
./distribution_gateway

# Use custom config file
./distribution_gateway /path/to/config.json
```

### Configuration

```json
{
    "log_level": "info",
    "source": {
        "shm_name": "gateway",
        "poll_timeout_us": 100
    },
    "tcp": {
        "enable": true,
        "port": 9999
    },
    "mqtt": {
        "enable": true,
        "broker_url": "mqtt://192.168.0.2:1883",
        "client_id": "distribution_gateway",
        "username": "trading",
        "password": "trading123",
        "topic": "bbo_messages"
    },
    "kafka": {
        "enable": false,
        "broker_url": "192.168.0.203:9092",
        "client_id": "distribution_gateway",
        "topic": "bbo_messages"
    },
    "performance": {
        "enable_rt": false,
        "quiet_mode": false,
        "rt_priority": 70,
        "cpu_core": 3
    }
}
```

### Configuration Options

| Option | Description | Default |
|--------|-------------|---------|
| `source.shm_name` | Shared memory name (must match Project 36) | gateway |
| `source.poll_timeout_us` | Poll timeout in microseconds | 100 |
| `tcp.enable` | Enable TCP server | true |
| `tcp.port` | TCP server port | 9999 |
| `mqtt.enable` | Enable MQTT publisher | true |
| `mqtt.broker_url` | MQTT broker URL | mqtt://192.168.0.2:1883 |
| `mqtt.topic` | MQTT topic | bbo_messages |
| `kafka.enable` | Enable Kafka producer | false |
| `kafka.broker_url` | Kafka broker URL | 192.168.0.203:9092 |
| `performance.enable_rt` | Enable RT scheduling | false |
| `performance.quiet_mode` | Suppress console output | false |

---

## Integration

### Running with Project 36

```bash
# Terminal 1: Start Project 36 (DPDK receiver)
cd 36-ultra-low-latency-rx/build
sudo ./network_handler -l 14 -a 0000:01:00.0 -- -s gateway

# Terminal 2: Start Project 37 (distribution)
cd 37-order-gateway-distribution/build
./distribution_gateway
```

### JSON Output Format

```json
{
    "type": "bbo",
    "symbol": "AAPL",
    "timestamp": 1699824000123456789,
    "bid": {
        "price": 290.1708,
        "shares": 30
    },
    "ask": {
        "price": 290.2208,
        "shares": 30
    },
    "spread": {
        "price": 0.05,
        "percent": 0.017
    },
    "fpga": {
        "latency_us": 0.312,
        "latency_a_us": 0.288,
        "latency_b_us": 0.024
    }
}
```

---

## Code Structure

```
37-order-gateway-distribution/
├── CMakeLists.txt              # Build configuration
├── config.json                 # Runtime configuration
├── README.md                   # This file
├── include/
│   └── distribution_gateway.h  # Gateway header
└── src/
    ├── main.cpp                # Entry point
    └── distribution_gateway.cpp # Gateway implementation
```

### Reused from Project 14

| File | Purpose |
|------|---------|
| `tcp_server.cpp` | TCP broadcast server |
| `mqtt.cpp` | MQTT publisher |
| `kafka_producer.cpp` | Kafka producer |

### Shared Components

| File | Purpose |
|------|---------|
| `common/disruptor/BboRingBuffer.h` | Shared memory ring buffer |
| `common/disruptor/SharedMemoryManager.h` | POSIX shared memory |
| `common/bbo_data.h` | BBO data structure |

---

## Troubleshooting

### "Failed to connect to shared memory"
- Ensure Project 36 is running and has created the shared memory
- Check shared memory name matches in both configs
- Verify: `ls -la /dev/shm/bbo_ring_*`

### "MQTT connection failed"
- Check MQTT broker is running: `systemctl status mosquitto`
- Verify broker URL and credentials
- Test: `mosquitto_sub -h 192.168.0.2 -t bbo_messages -u trading -P trading123`

### No messages distributed
- Check Project 36 is receiving and publishing BBO data
- Verify shared memory name matches
- Check `poll_timeout_us` is not too high

---

## Related Projects

- **[36-ultra-low-latency-rx/](../36-ultra-low-latency-rx/)** - DPDK receiver (data source)
- **[14-order-gateway-cpp/](../14-order-gateway-cpp/)** - Full-featured gateway (reference)
- **[10-esp32-ticker/](../10-esp32-ticker/)** - ESP32 MQTT client
- **[12-java-desktop-trading-terminal/](../12-java-desktop-trading-terminal/)** - Java TCP client

---

## Status

| Item | Status |
|------|--------|
| Distribution Gateway | Implemented |
| TCP Server | Reused from Project 14 |
| MQTT Publisher | Reused from Project 14 |
| Kafka Producer | Reused from Project 14 |
| Shared Memory Consumer | Implemented |
| Hardware Testing | Pending |

---

**Created:** January 2026
**Last Updated:** February 14, 2026
**Build Time:** ~10 seconds
**Hardware Status:** Pending testing with Project 36 + Project 38
