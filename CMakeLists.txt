cmake_minimum_required(VERSION 3.16)
project(order_gateway_distribution
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "BBO Distribution Gateway (reads from shared memory)"
)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type defaults to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")

# Find required packages
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(Threads REQUIRED)

# Find nlohmann_json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    # Try pkg-config
    find_package(PkgConfig)
    pkg_check_modules(NLOHMANN_JSON nlohmann_json)
endif()

# Find spdlog
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    find_package(PkgConfig)
    pkg_check_modules(SPDLOG spdlog)
endif()

# Source files - this project's sources
set(SOURCES
    src/main.cpp
    src/distribution_gateway.cpp
)

# Reuse distribution components from Project 14
set(PROJECT14_SOURCES
    ../14-order-gateway-cpp/src/tcp_server.cpp
    ../14-order-gateway-cpp/src/mqtt.cpp
    ../14-order-gateway-cpp/src/kafka_producer.cpp
)

# Create executable
add_executable(distribution_gateway ${SOURCES} ${PROJECT14_SOURCES})

# Include directories
target_include_directories(distribution_gateway PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../14-order-gateway-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/disruptor
    ${Boost_INCLUDE_DIRS}
)

# Compiler options
target_compile_options(distribution_gateway PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-DNDEBUG>
    -march=native
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
)

# Linker options
target_link_options(distribution_gateway PRIVATE
    $<$<CONFIG:Release>:-flto>
    -Wl,--as-needed
)

# Link libraries
target_link_libraries(distribution_gateway PRIVATE
    ${Boost_LIBRARIES}
    Threads::Threads
    rt          # POSIX shared memory
    mosquitto   # MQTT client
    rdkafka++   # Kafka client
)

# Link nlohmann_json
if(nlohmann_json_FOUND)
    target_link_libraries(distribution_gateway PRIVATE nlohmann_json::nlohmann_json)
elseif(NLOHMANN_JSON_FOUND)
    target_include_directories(distribution_gateway PRIVATE ${NLOHMANN_JSON_INCLUDE_DIRS})
endif()

# Link spdlog
if(spdlog_FOUND)
    target_link_libraries(distribution_gateway PRIVATE spdlog::spdlog)
elseif(SPDLOG_FOUND)
    target_include_directories(distribution_gateway PRIVATE ${SPDLOG_INCLUDE_DIRS})
    target_link_libraries(distribution_gateway PRIVATE ${SPDLOG_LIBRARIES})
endif()

# Install target
install(TARGETS distribution_gateway
    RUNTIME DESTINATION bin
)

# Print configuration summary
message(STATUS "")
message(STATUS "=== Configuration Summary ===")
message(STATUS "  Project:        ${PROJECT_NAME}")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "")
