#pragma once

#include "../../common/disruptor/BboRingBuffer.h"
#include "../../common/disruptor/SharedMemoryManager.h"
#include "../../common/bbo_data.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <functional>

// Forward declarations
namespace gateway {
class TCPServer;
class MQTT;
class KafkaProducer;
}

namespace distribution {

// Configuration structure loaded from JSON
struct Config {
    // Shared memory source (from Project 36)
    struct {
        std::string shm_name = "gateway";
        int poll_timeout_us = 100;  // Microseconds between polls
    } source;

    // TCP distribution
    struct {
        bool enable = true;
        int port = 9999;
    } tcp;

    // MQTT distribution
    struct {
        bool enable = true;
        std::string broker_url = "mqtt://192.168.0.2:1883";
        std::string client_id = "distribution_gateway";
        std::string username = "trading";
        std::string password = "trading123";
        std::string topic = "bbo_messages";
    } mqtt;

    // Kafka distribution
    struct {
        bool enable = false;
        std::string broker_url = "192.168.0.203:9092";
        std::string client_id = "distribution_gateway";
        std::string topic = "bbo_messages";
    } kafka;

    // Performance options
    struct {
        bool enable_rt = false;      // Real-time scheduling
        bool quiet_mode = false;     // Suppress console output
        int rt_priority = 70;        // RT priority (1-99)
        int cpu_core = 3;            // CPU core to pin to
    } performance;

    // Logging
    std::string log_level = "info";
};

// Statistics (cache-line aligned)
struct alignas(64) Stats {
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> messages_distributed{0};
    std::atomic<uint64_t> tcp_broadcasts{0};
    std::atomic<uint64_t> mqtt_publishes{0};
    std::atomic<uint64_t> kafka_publishes{0};
    std::atomic<uint64_t> distribution_errors{0};
};

// Distribution Gateway
// Reads BBO data from shared memory (Project 36) and distributes via TCP/MQTT/Kafka
class DistributionGateway {
public:
    explicit DistributionGateway(const Config& config);
    ~DistributionGateway();

    // Non-copyable
    DistributionGateway(const DistributionGateway&) = delete;
    DistributionGateway& operator=(const DistributionGateway&) = delete;

    // Lifecycle
    bool initialize();
    void start();
    void stop();
    void wait();

    // Status
    bool is_running() const { return running_.load(std::memory_order_relaxed); }
    const Stats& get_stats() const { return stats_; }
    void print_stats() const;
    void reset_stats();

private:
    Config config_;
    Stats stats_;

    // Shared memory source
    disruptor::BboRingBuffer* ring_buffer_ = nullptr;

    // Distribution targets
    std::unique_ptr<gateway::TCPServer> tcp_server_;
    std::unique_ptr<gateway::MQTT> mqtt_;
    std::unique_ptr<gateway::KafkaProducer> kafka_;

    // Threading
    std::thread distribution_thread_;
    std::atomic<bool> running_{false};

    // Internal methods
    bool connect_shared_memory();
    bool init_tcp();
    bool init_mqtt();
    bool init_kafka();

    void distribution_loop();
    void distribute_bbo(const gateway::BBOData& bbo);
    std::string format_json(const gateway::BBOData& bbo) const;

    void apply_rt_config();
};

// Load configuration from JSON file
Config load_config(const std::string& path);

}  // namespace distribution
