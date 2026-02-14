#include "distribution_gateway.h"

// Reuse distribution components from Project 14
#include "../../14-order-gateway-cpp/include/tcp_server.h"
#include "../../14-order-gateway-cpp/include/mqtt.h"
#include "../../14-order-gateway-cpp/include/kafka_producer.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <sched.h>
#include <pthread.h>

namespace distribution
{

    DistributionGateway::DistributionGateway(const Config &config)
        : config_(config)
    {
    }

    DistributionGateway::~DistributionGateway()
    {
        stop();
    }

    bool DistributionGateway::initialize()
    {
        spdlog::info("Initializing Distribution Gateway...");

        if (!connect_shared_memory())
        {
            return false;
        }

        if (config_.tcp.enable && !init_tcp())
        {
            return false;
        }

        if (config_.mqtt.enable && !init_mqtt())
        {
            return false;
        }

        if (config_.kafka.enable && !init_kafka())
        {
            return false;
        }

        spdlog::info("Distribution Gateway initialized successfully");
        return true;
    }

    bool DistributionGateway::connect_shared_memory()
    {
        try
        {
            ring_buffer_ = disruptor::SharedMemoryManager<
                disruptor::BboRingBuffer>::open(config_.source.shm_name);
            spdlog::info("Connected to shared memory '{}'", config_.source.shm_name);
            return true;
        }
        catch (const std::exception &e)
        {
            spdlog::error("Failed to connect to shared memory '{}': {}",
                          config_.source.shm_name, e.what());
            return false;
        }
    }

    bool DistributionGateway::init_tcp()
    {
        try
        {
            tcp_server_ = std::make_unique<gateway::TCPServer>(config_.tcp.port);
            tcp_server_->start();
            spdlog::info("TCP server started on port {}", config_.tcp.port);
            return true;
        }
        catch (const std::exception &e)
        {
            spdlog::error("Failed to start TCP server: {}", e.what());
            return false;
        }
    }

    bool DistributionGateway::init_mqtt()
    {
        try
        {
            mqtt_ = std::make_unique<gateway::MQTT>(
                config_.mqtt.broker_url,
                config_.mqtt.client_id,
                config_.mqtt.username,
                config_.mqtt.password);
            mqtt_->connect();
            spdlog::info("Connected to MQTT broker: {}", config_.mqtt.broker_url);
            return true;
        }
        catch (const std::exception &e)
        {
            spdlog::error("Failed to connect to MQTT: {}", e.what());
            return false;
        }
    }

    bool DistributionGateway::init_kafka()
    {
        try
        {
            kafka_ = std::make_unique<gateway::KafkaProducer>(
                config_.kafka.broker_url,
                config_.kafka.client_id);
            spdlog::info("Kafka producer initialized: {}", config_.kafka.broker_url);
            return true;
        }
        catch (const std::exception &e)
        {
            spdlog::error("Failed to initialize Kafka: {}", e.what());
            return false;
        }
    }

    void DistributionGateway::start()
    {
        if (running_.load())
        {
            spdlog::warn("Distribution Gateway already running");
            return;
        }

        running_.store(true, std::memory_order_relaxed);

        distribution_thread_ = std::thread([this]()
                                           {
        if (config_.performance.enable_rt) {
            apply_rt_config();
        }
        distribution_loop(); });

        spdlog::info("Distribution Gateway started");
    }

    void DistributionGateway::stop()
    {
        if (!running_.load())
        {
            return;
        }

        spdlog::info("Stopping Distribution Gateway...");
        running_.store(false, std::memory_order_relaxed);

        if (distribution_thread_.joinable())
        {
            distribution_thread_.join();
        }

        // Cleanup
        if (mqtt_ && mqtt_->isConnected())
        {
            mqtt_->disconnect();
        }

        if (kafka_)
        {
            kafka_->flush();
        }

        if (ring_buffer_)
        {
            disruptor::SharedMemoryManager<disruptor::BboRingBuffer>::disconnect(ring_buffer_);
            ring_buffer_ = nullptr;
        }

        spdlog::info("Distribution Gateway stopped");
    }

    void DistributionGateway::wait()
    {
        if (distribution_thread_.joinable())
        {
            distribution_thread_.join();
        }
    }

    void DistributionGateway::distribution_loop()
    {
        spdlog::info("Distribution loop started");

        gateway::BBOData bbo;

        while (running_.load(std::memory_order_relaxed))
        {
            // Poll shared memory for new BBO data
            if (ring_buffer_->poll(bbo))
            {
                stats_.messages_received.fetch_add(1, std::memory_order_relaxed);
                distribute_bbo(bbo);
            }
            else
            {
                // No data available, brief pause to avoid busy-spinning
                std::this_thread::sleep_for(
                    std::chrono::microseconds(config_.source.poll_timeout_us));
            }
        }

        spdlog::info("Distribution loop stopped");
    }

    void DistributionGateway::distribute_bbo(const gateway::BBOData &bbo)
    {
        if (!bbo.valid)
        {
            return;
        }

        std::string json = format_json(bbo);

        // TCP broadcast
        if (tcp_server_ && tcp_server_->client_count() > 0)
        {
            try
            {
                tcp_server_->broadcast(json);
                stats_.tcp_broadcasts.fetch_add(1, std::memory_order_relaxed);
            }
            catch (const std::exception &e)
            {
                spdlog::debug("TCP broadcast error: {}", e.what());
                stats_.distribution_errors.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // MQTT publish
        if (mqtt_ && mqtt_->isConnected())
        {
            try
            {
                mqtt_->publish(config_.mqtt.topic, json);
                stats_.mqtt_publishes.fetch_add(1, std::memory_order_relaxed);
            }
            catch (const std::exception &e)
            {
                spdlog::debug("MQTT publish error: {}", e.what());
                stats_.distribution_errors.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Kafka publish
        if (kafka_ && kafka_->isConnected())
        {
            try
            {
                kafka_->publish(config_.kafka.topic, json);
                stats_.kafka_publishes.fetch_add(1, std::memory_order_relaxed);
            }
            catch (const std::exception &e)
            {
                spdlog::debug("Kafka publish error: {}", e.what());
                stats_.distribution_errors.fetch_add(1, std::memory_order_relaxed);
            }
        }

        stats_.messages_distributed.fetch_add(1, std::memory_order_relaxed);

        // Console output (unless quiet mode)
        if (!config_.performance.quiet_mode)
        {
            spdlog::info("[BBO] {} Bid: ${:.4f} ({}) | Ask: ${:.4f} ({}) | Spread: ${:.4f}",
                         bbo.get_symbol(),
                         bbo.bid_price, bbo.bid_shares,
                         bbo.ask_price, bbo.ask_shares,
                         bbo.spread);
        }
    }

    std::string DistributionGateway::format_json(const gateway::BBOData &bbo) const
    {
        // Calculate spread percentage
        double spread_pct = 0.0;
        if (bbo.bid_price > 0.0)
        {
            spread_pct = (bbo.spread / bbo.bid_price) * 100.0;
        }

        nlohmann::json j;
        j["type"] = "bbo";
        j["symbol"] = bbo.get_symbol();
        j["timestamp"] = bbo.timestamp_ns;
        j["bid"]["price"] = bbo.bid_price;
        j["bid"]["shares"] = bbo.bid_shares;
        j["ask"]["price"] = bbo.ask_price;
        j["ask"]["shares"] = bbo.ask_shares;
        j["spread"]["price"] = bbo.spread;
        j["spread"]["percent"] = spread_pct;

        // Include FPGA latency if available
        if (bbo.fpga_latency_us > 0.0)
        {
            j["fpga"]["latency_us"] = bbo.fpga_latency_us;
            j["fpga"]["latency_a_us"] = bbo.fpga_latency_a_us;
            j["fpga"]["latency_b_us"] = bbo.fpga_latency_b_us;
        }

        return j.dump();
    }

    void DistributionGateway::apply_rt_config()
    {

        // Set real-time scheduling
        struct sched_param param;
        param.sched_priority = config_.performance.rt_priority;

        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0)
        {
            spdlog::warn("Failed to set RT scheduling (run as root or with CAP_SYS_NICE)");
        }
        else
        {
            spdlog::info("RT scheduling enabled: SCHED_FIFO priority {}",
                         config_.performance.rt_priority);
        }

        // Pin to CPU core
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(config_.performance.cpu_core, &cpuset);

        if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0)
        {
            spdlog::warn("Failed to pin to CPU core {}", config_.performance.cpu_core);
        }
        else
        {
            spdlog::info("Pinned to CPU core {}", config_.performance.cpu_core);
        }
    }

    void DistributionGateway::print_stats() const
    {
        spdlog::info("=== Distribution Gateway Statistics ===");
        spdlog::info("  Messages received:    {}",
                     stats_.messages_received.load(std::memory_order_relaxed));
        spdlog::info("  Messages distributed: {}",
                     stats_.messages_distributed.load(std::memory_order_relaxed));
        spdlog::info("  TCP broadcasts:       {}",
                     stats_.tcp_broadcasts.load(std::memory_order_relaxed));
        spdlog::info("  MQTT publishes:       {}",
                     stats_.mqtt_publishes.load(std::memory_order_relaxed));
        spdlog::info("  Kafka publishes:      {}",
                     stats_.kafka_publishes.load(std::memory_order_relaxed));
        spdlog::info("  Distribution errors:  {}",
                     stats_.distribution_errors.load(std::memory_order_relaxed));

        if (tcp_server_)
        {
            spdlog::info("  TCP clients:          {}", tcp_server_->client_count());
        }
    }

    void DistributionGateway::reset_stats()
    {
        stats_.messages_received.store(0, std::memory_order_relaxed);
        stats_.messages_distributed.store(0, std::memory_order_relaxed);
        stats_.tcp_broadcasts.store(0, std::memory_order_relaxed);
        stats_.mqtt_publishes.store(0, std::memory_order_relaxed);
        stats_.kafka_publishes.store(0, std::memory_order_relaxed);
        stats_.distribution_errors.store(0, std::memory_order_relaxed);
    }

    // Configuration loading
    Config load_config(const std::string &path)
    {
        Config config;

        std::ifstream file(path);
        if (!file.is_open())
        {
            spdlog::warn("Config file '{}' not found, using defaults", path);
            return config;
        }

        try
        {
            nlohmann::json j = nlohmann::json::parse(file);

            // Log level
            if (j.contains("log_level"))
            {
                config.log_level = j["log_level"].get<std::string>();
            }

            // Source (shared memory)
            if (j.contains("source"))
            {
                auto &src = j["source"];
                if (src.contains("shm_name"))
                    config.source.shm_name = src["shm_name"].get<std::string>();
                if (src.contains("poll_timeout_us"))
                    config.source.poll_timeout_us = src["poll_timeout_us"].get<int>();
            }

            // TCP
            if (j.contains("tcp"))
            {
                auto &tcp = j["tcp"];
                if (tcp.contains("enable"))
                    config.tcp.enable = tcp["enable"].get<bool>();
                if (tcp.contains("port"))
                    config.tcp.port = tcp["port"].get<int>();
            }

            // MQTT
            if (j.contains("mqtt"))
            {
                auto &mqtt = j["mqtt"];
                if (mqtt.contains("enable"))
                    config.mqtt.enable = mqtt["enable"].get<bool>();
                if (mqtt.contains("broker_url"))
                    config.mqtt.broker_url = mqtt["broker_url"].get<std::string>();
                if (mqtt.contains("client_id"))
                    config.mqtt.client_id = mqtt["client_id"].get<std::string>();
                if (mqtt.contains("username"))
                    config.mqtt.username = mqtt["username"].get<std::string>();
                if (mqtt.contains("password"))
                    config.mqtt.password = mqtt["password"].get<std::string>();
                if (mqtt.contains("topic"))
                    config.mqtt.topic = mqtt["topic"].get<std::string>();
            }

            // Kafka
            if (j.contains("kafka"))
            {
                auto &kafka = j["kafka"];
                if (kafka.contains("enable"))
                    config.kafka.enable = kafka["enable"].get<bool>();
                if (kafka.contains("broker_url"))
                    config.kafka.broker_url = kafka["broker_url"].get<std::string>();
                if (kafka.contains("client_id"))
                    config.kafka.client_id = kafka["client_id"].get<std::string>();
                if (kafka.contains("topic"))
                    config.kafka.topic = kafka["topic"].get<std::string>();
            }

            // Performance
            if (j.contains("performance"))
            {
                auto &perf = j["performance"];
                if (perf.contains("enable_rt"))
                    config.performance.enable_rt = perf["enable_rt"].get<bool>();
                if (perf.contains("quiet_mode"))
                    config.performance.quiet_mode = perf["quiet_mode"].get<bool>();
                if (perf.contains("rt_priority"))
                    config.performance.rt_priority = perf["rt_priority"].get<int>();
                if (perf.contains("cpu_core"))
                    config.performance.cpu_core = perf["cpu_core"].get<int>();
            }

            spdlog::info("Loaded configuration from '{}'", path);
        }
        catch (const std::exception &e)
        {
            spdlog::error("Error parsing config file: {}", e.what());
        }

        return config;
    }

} // namespace distribution
