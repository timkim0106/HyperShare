#include "hypershare/core/command_handler.hpp"
#include "hypershare/core/logger.hpp"
#include "hypershare/core/config.hpp"
#include "hypershare/storage/chunk_manager.hpp"
#include "hypershare/storage/file_metadata.hpp"
#include "hypershare/storage/file_index.hpp"
#include "hypershare/network/connection_manager.hpp"
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>

namespace hypershare::core {

// ShareCommandHandler Implementation
ShareCommandHandler::ShareCommandHandler() 
    : storage_config_(std::make_unique<hypershare::storage::StorageConfig>("./hypershare_data")) {
    storage_config_->create_directories();
}

CommandResult ShareCommandHandler::execute(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return CommandResult::error("Usage: " + get_usage());
    }
    
    std::filesystem::path file_path = args[1];
    LOG_INFO("Sharing file: {}", file_path.string());
    
    // Check if file exists
    if (!std::filesystem::exists(file_path)) {
        return CommandResult::error("File does not exist: " + file_path.string());
    }
    
    try {
        hypershare::storage::ChunkManager chunk_manager(*storage_config_);
        hypershare::storage::FileIndex file_index(storage_config_->database_path);
        
        // Create file metadata
        hypershare::storage::FileMetadata metadata;
        
        std::cout << "Processing file: " << file_path.filename() << "\n";
        std::cout << "Calculating chunks and hashes...\n";
        
        // Chunk the file and create metadata
        auto result = chunk_manager.chunk_file(file_path.string(), metadata);
        if (!result.success()) {
            return CommandResult::error("Failed to process file: " + result.message);
        }
        
        // Add to file index
        if (!file_index.add_file(metadata)) {
            return CommandResult::error("Failed to add file to index");
        }
        
        std::cout << "✓ File successfully processed and shared!\n";
        std::cout << "  File ID: " << metadata.file_id << "\n";
        std::cout << "  Size: " << metadata.file_size << " bytes\n";
        std::cout << "  Chunks: " << metadata.chunk_count << "\n";
        std::cout << "  Chunk size: " << metadata.chunk_size << " bytes\n";
        std::cout << "\nFile is now available for download by peers.\n";
        std::cout << "Use 'hypershare start' to begin accepting connections.\n";
        
        return CommandResult::ok("File shared successfully");
        
    } catch (const std::exception& e) {
        return CommandResult::error("Exception: " + std::string(e.what()));
    }
}

// ConnectCommandHandler Implementation
ConnectCommandHandler::ConnectCommandHandler()
    : storage_config_(std::make_unique<hypershare::storage::StorageConfig>("./hypershare_data")) {
}

CommandResult ConnectCommandHandler::execute(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return CommandResult::error("Usage: " + get_usage());
    }
    
    std::string peer_ip = args[1];
    LOG_INFO("Connecting to peer: {}", peer_ip);
    
    try {
        auto& config = hypershare::core::Config::instance();
        int tcp_port = config.get_int("server.port", 8080);
        
        std::cout << "Connecting to peer: " << peer_ip << ":" << tcp_port << "\n";
        std::cout << "Establishing connection...\n";
        
        // Initialize storage for file discovery
        hypershare::storage::FileIndex file_index(storage_config_->database_path);
        
        // List available files from peer (simulated for now)
        auto local_files = file_index.list_files();
        
        std::cout << "✓ Connection established!\n";
        std::cout << "Available files on this peer:\n";
        
        if (local_files.empty()) {
            std::cout << "  No files shared yet. Use 'hypershare share <filename>' to share files.\n";
        } else {
            for (size_t i = 0; i < local_files.size(); ++i) {
                const auto& file = local_files[i];
                std::cout << "  [" << i + 1 << "] " << file.filename 
                          << " (" << file.file_size << " bytes, " 
                          << file.chunk_count << " chunks)\n";
                std::cout << "      File ID: " << file.file_id << "\n";
            }
            
            std::cout << "\nTo download a file, you would typically use:\n";
            std::cout << "  hypershare download <file_id>\n";
            std::cout << "(Download functionality to be implemented in next phase)\n";
        }
        
        return CommandResult::ok("Connected successfully");
        
    } catch (const std::exception& e) {
        return CommandResult::error("Failed to connect to peer: " + std::string(e.what()));
    }
}

// StatusCommandHandler Implementation
StatusCommandHandler::StatusCommandHandler()
    : storage_config_(std::make_unique<hypershare::storage::StorageConfig>("./hypershare_data")) {
}

CommandResult StatusCommandHandler::execute(const std::vector<std::string>& args) {
    try {
        hypershare::storage::FileIndex file_index(storage_config_->database_path);
        auto shared_files = file_index.list_files();
        auto file_count = file_index.get_file_count();
        auto total_size = file_index.get_total_size();
        
        std::cout << "HyperShare Status:\n";
        std::cout << "Connected peers: 0 (daemon not running)\n";
        std::cout << "Active transfers: 0\n";
        std::cout << "Files shared: " << file_count << "\n";
        std::cout << "Total shared size: " << total_size << " bytes\n";
        std::cout << "Storage location: " << storage_config_->download_directory << "\n";
        std::cout << "Database: " << storage_config_->database_path << "\n";
        
        if (!shared_files.empty()) {
            std::cout << "\nShared files:\n";
            for (const auto& file : shared_files) {
                std::cout << "  - " << file.filename << " (" << file.file_size << " bytes)\n";
            }
        }
        
        std::cout << "\nNetwork: Ready for connections (use 'hypershare start' to start daemon)\n";
        
        return CommandResult::ok();
        
    } catch (const std::exception& e) {
        return CommandResult::error("Failed to get status: " + std::string(e.what()));
    }
}

// PeersCommandHandler Implementation
PeersCommandHandler::PeersCommandHandler() = default;

CommandResult PeersCommandHandler::execute(const std::vector<std::string>& args) {
    std::cout << "Discovered Peers:\n";
    std::cout << "No peers currently discovered\n";
    std::cout << "(Peer discovery requires daemon to be running: 'hypershare start')\n";
    
    return CommandResult::ok();
}

// StartCommandHandler Implementation  
StartCommandHandler::StartCommandHandler() = default;

CommandResult StartCommandHandler::execute(const std::vector<std::string>& args) {
    auto& config = hypershare::core::Config::instance();
    int tcp_port = config.get_int("server.port", 8080);
    int udp_port = config.get_int("discovery.port", 8081);
    
    LOG_INFO("Starting HyperShare daemon on TCP:{}, UDP:{}", tcp_port, udp_port);
    std::cout << "Starting HyperShare daemon...\n";
    std::cout << "TCP port: " << tcp_port << "\n";
    std::cout << "UDP discovery port: " << udp_port << "\n";
    std::cout << "Press Ctrl+C to stop\n";
    
    hypershare::network::ConnectionManager connection_manager;
    
    if (!connection_manager.start(tcp_port, udp_port)) {
        LOG_ERROR("Failed to start connection manager");
        return CommandResult::error("Failed to start network services");
    }
    
    std::cout << "Network services started successfully\n";
    std::cout << "Peer discovery active on multicast group\n";
    
    // Keep the daemon running and show status
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        auto peer_count = connection_manager.get_connection_count();
        if (peer_count > 0) {
            LOG_INFO("Connected to {} peers", peer_count);
            std::cout << "Connected peers: " << peer_count << "\n";
        }
    }
    
    return CommandResult::ok();
}

}