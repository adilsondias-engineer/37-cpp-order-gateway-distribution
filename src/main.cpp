/**
 * Project 37: Order Gateway Distribution
 *
 * Reads BBO data from shared memory (populated by Project 36)
 * and distributes via TCP, MQTT, and Kafka.
 *
 * Data Flow:
 *   Project 36 (Ultra Low Latency RX) --> Shared Memory --> Project 37 --> TCP/MQTT/Kafka
 */

#include "distribution_gateway.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <csignal>
#include <cstdio>
#include <string>

// Global gateway pointer for signal handler
static distribution::DistributionGateway* g_gateway = nullptr;

void signal_handler(int sig) {
    std::printf("\nReceived signal %d, stopping...\n", sig);
    if (g_gateway) {
        g_gateway->stop();
    }
}

void setup_logging(const std::string& level) {
    auto console = spdlog::stdout_color_mt("distribution_gateway");
    spdlog::set_default_logger(console);

    if (level == "trace") {
        spdlog::set_level(spdlog::level::trace);
    } else if (level == "debug") {
        spdlog::set_level(spdlog::level::debug);
    } else if (level == "info") {
        spdlog::set_level(spdlog::level::info);
    } else if (level == "warn") {
        spdlog::set_level(spdlog::level::warn);
    } else if (level == "error") {
        spdlog::set_level(spdlog::level::err);
    } else {
        spdlog::set_level(spdlog::level::info);
    }

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
}

void print_usage(const char* prog) {
    std::printf(
        "Project 37: Order Gateway Distribution\n"
        "\n"
        "Reads BBO data from shared memory and distributes via TCP/MQTT/Kafka.\n"
        "\n"
        "Usage: %s [config.json]\n"
        "\n"
        "Arguments:\n"
        "  config.json    Path to configuration file (default: config.json)\n"
        "\n"
        "Example:\n"
        "  %s                    # Use default config.json\n"
        "  %s /path/to/cfg.json  # Use custom config\n"
        "\n",
        prog, prog, prog);
}

int main(int argc, char* argv[]) {
    // Parse command line
    std::string config_path = "config.json";

    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        config_path = arg;
    }

    // Load configuration
    distribution::Config config = distribution::load_config(config_path);

    // Setup logging
    setup_logging(config.log_level);

    spdlog::info("=== Project 37: Order Gateway Distribution ===");
    spdlog::info("Configuration:");
    spdlog::info("  Shared memory:  {}", config.source.shm_name);
    spdlog::info("  TCP:            {} (port {})",
                 config.tcp.enable ? "enabled" : "disabled",
                 config.tcp.port);
    spdlog::info("  MQTT:           {} ({})",
                 config.mqtt.enable ? "enabled" : "disabled",
                 config.mqtt.broker_url);
    spdlog::info("  Kafka:          {} ({})",
                 config.kafka.enable ? "enabled" : "disabled",
                 config.kafka.broker_url);
    spdlog::info("  RT scheduling:  {}",
                 config.performance.enable_rt ? "enabled" : "disabled");
    spdlog::info("  Quiet mode:     {}",
                 config.performance.quiet_mode ? "enabled" : "disabled");

    // Create gateway
    distribution::DistributionGateway gateway(config);
    g_gateway = &gateway;

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Initialize
    if (!gateway.initialize()) {
        spdlog::error("Failed to initialize gateway");
        return 1;
    }

    // Start distribution
    gateway.start();

    spdlog::info("Gateway running. Press Ctrl+C to stop.");

    // Wait for shutdown
    gateway.wait();

    // Print final stats
    gateway.print_stats();

    g_gateway = nullptr;
    return 0;
}
