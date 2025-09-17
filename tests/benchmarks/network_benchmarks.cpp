#include <benchmark/benchmark.h>
#include "hypershare/network/protocol.hpp"
#include "hypershare/network/message_handler.hpp"
#include "hypershare/network/tcp_server.hpp"
#include "hypershare/network/tcp_client.hpp"
#include <random>
#include <thread>

using namespace hypershare::network;

// Benchmark message serialization performance
static void BM_MessageSerialization(benchmark::State& state) {
    HandshakeMessage msg{12345, 8080, "BenchmarkPeer", 0x12345678};
    
    for (auto _ : state) {
        auto serialized = msg.serialize();
        benchmark::DoNotOptimize(serialized);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MessageSerialization);

static void BM_MessageDeserialization(benchmark::State& state) {
    HandshakeMessage msg{12345, 8080, "BenchmarkPeer", 0x12345678};
    auto serialized = msg.serialize();
    
    for (auto _ : state) {
        auto deserialized = HandshakeMessage::deserialize(serialized);
        benchmark::DoNotOptimize(deserialized);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MessageDeserialization);

static void BM_MessageHeaderSerialization(benchmark::State& state) {
    MessageHeader header(MessageType::HEARTBEAT, 100);
    
    for (auto _ : state) {
        auto serialized = header.serialize();
        benchmark::DoNotOptimize(serialized);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MessageHeaderSerialization);

static void BM_MessageHeaderDeserialization(benchmark::State& state) {
    MessageHeader header(MessageType::HEARTBEAT, 100);
    auto serialized = header.serialize();
    
    for (auto _ : state) {
        auto deserialized = MessageHeader::deserialize(serialized);
        benchmark::DoNotOptimize(deserialized);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MessageHeaderDeserialization);

static void BM_ChecksumCalculation(benchmark::State& state) {
    std::vector<std::uint8_t> data(state.range(0));
    std::random_device rd;
    std::mt19937 gen(rd());
    std::generate(data.begin(), data.end(), gen);
    
    MessageHeader header(MessageType::CHUNK_DATA, static_cast<std::uint32_t>(data.size()));
    
    for (auto _ : state) {
        header.calculate_checksum(data);
        benchmark::DoNotOptimize(header.checksum);
    }
    
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_ChecksumCalculation)->Range(1024, 1024*1024);

static void BM_ChecksumVerification(benchmark::State& state) {
    std::vector<std::uint8_t> data(state.range(0));
    std::random_device rd;
    std::mt19937 gen(rd());
    std::generate(data.begin(), data.end(), gen);
    
    MessageHeader header(MessageType::CHUNK_DATA, static_cast<std::uint32_t>(data.size()));
    header.calculate_checksum(data);
    
    for (auto _ : state) {
        bool valid = header.verify_checksum(data);
        benchmark::DoNotOptimize(valid);
    }
    
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_ChecksumVerification)->Range(1024, 1024*1024);

static void BM_ChunkDataSerialization(benchmark::State& state) {
    std::vector<std::uint8_t> data(state.range(0));
    std::random_device rd;
    std::mt19937 gen(rd());
    std::generate(data.begin(), data.end(), gen);
    
    ChunkDataMessage msg{"file123", 42, data, "chunk_hash"};
    
    for (auto _ : state) {
        auto serialized = msg.serialize();
        benchmark::DoNotOptimize(serialized);
    }
    
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_ChunkDataSerialization)->Range(1024, 1024*1024);

static void BM_ChunkDataDeserialization(benchmark::State& state) {
    std::vector<std::uint8_t> data(state.range(0));
    std::random_device rd;
    std::mt19937 gen(rd());
    std::generate(data.begin(), data.end(), gen);
    
    ChunkDataMessage msg{"file123", 42, data, "chunk_hash"};
    auto serialized = msg.serialize();
    
    for (auto _ : state) {
        auto deserialized = ChunkDataMessage::deserialize(serialized);
        benchmark::DoNotOptimize(deserialized);
    }
    
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_ChunkDataDeserialization)->Range(1024, 1024*1024);

static void BM_MessageQueueOperations(benchmark::State& state) {
    MessageQueue queue(10000);
    MessageHeader header(MessageType::HEARTBEAT, 10);
    std::vector<std::uint8_t> payload(10, 0x42);
    
    for (auto _ : state) {
        queue.push(header, payload);
        auto msg = queue.pop();
        benchmark::DoNotOptimize(msg);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MessageQueueOperations);

static void BM_MessageQueuePriorityOperations(benchmark::State& state) {
    MessageQueue queue(10000);
    MessageHeader normal_header(MessageType::FILE_ANNOUNCE, 10);
    MessageHeader priority_header(MessageType::DISCONNECT, 5);
    std::vector<std::uint8_t> normal_payload(10, 0x42);
    std::vector<std::uint8_t> priority_payload(5, 0x24);
    
    for (auto _ : state) {
        queue.push(normal_header, normal_payload);
        queue.push_priority(priority_header, priority_payload);
        auto msg1 = queue.pop(); // Should be priority
        auto msg2 = queue.pop(); // Should be normal
        benchmark::DoNotOptimize(msg1);
        benchmark::DoNotOptimize(msg2);
    }
    
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_MessageQueuePriorityOperations);

static void BM_MessageHandlerRegistration(benchmark::State& state) {
    MessageHandler handler;
    
    for (auto _ : state) {
        handler.register_handler<HeartbeatMessage>(MessageType::HEARTBEAT,
            [](std::shared_ptr<Connection> conn, const HeartbeatMessage& msg) {
                // Empty handler
            });
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MessageHandlerRegistration);

static void BM_MessageHandlerProcessing(benchmark::State& state) {
    MessageHandler handler;
    std::atomic<int> counter{0};
    
    handler.register_handler<HeartbeatMessage>(MessageType::HEARTBEAT,
        [&counter](std::shared_ptr<Connection> conn, const HeartbeatMessage& msg) {
            counter++;
        });
    
    HeartbeatMessage msg{123456789ULL, 1, 0};
    auto payload = msg.serialize();
    MessageHeader header(MessageType::HEARTBEAT, static_cast<std::uint32_t>(payload.size()));
    
    for (auto _ : state) {
        handler.handle_message(nullptr, header, payload);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MessageHandlerProcessing);

// Benchmark for complete message serialization workflow
static void BM_CompleteMessageWorkflow(benchmark::State& state) {
    MessageHandler handler;
    std::atomic<int> processed{0};
    
    handler.register_handler<HandshakeMessage>(MessageType::HANDSHAKE,
        [&processed](std::shared_ptr<Connection> conn, const HandshakeMessage& msg) {
            processed++;
        });
    
    for (auto _ : state) {
        // Create message
        HandshakeMessage msg{state.iterations() % 100000, 8080, "Peer", 0};
        
        // Serialize
        auto serialized = MessageSerializer::serialize_message(MessageType::HANDSHAKE, msg);
        
        // Deserialize
        auto [header, payload] = MessageSerializer::deserialize_message(serialized);
        
        // Handle
        handler.handle_message(nullptr, header, payload);
        
        benchmark::DoNotOptimize(serialized);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CompleteMessageWorkflow);

// Network throughput simulation
static void BM_NetworkThroughputSimulation(benchmark::State& state) {
    const size_t chunk_size = state.range(0);
    std::vector<std::uint8_t> data(chunk_size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::generate(data.begin(), data.end(), gen);
    
    for (auto _ : state) {
        // Simulate network message processing for file transfer
        ChunkDataMessage chunk{"file123", state.iterations() % 1000, data, "hash"};
        
        // Serialize (simulates sending)
        auto serialized = chunk.serialize();
        
        // Add header
        MessageHeader header(MessageType::CHUNK_DATA, static_cast<std::uint32_t>(serialized.size()));
        header.calculate_checksum(serialized);
        
        // Verify (simulates receiving)
        bool valid = header.verify_checksum(serialized);
        
        // Deserialize (simulates processing)
        auto deserialized = ChunkDataMessage::deserialize(serialized);
        
        benchmark::DoNotOptimize(valid);
        benchmark::DoNotOptimize(deserialized);
    }
    
    state.SetBytesProcessed(state.iterations() * chunk_size);
}
BENCHMARK(BM_NetworkThroughputSimulation)->Range(1024, 1024*1024);

BENCHMARK_MAIN();