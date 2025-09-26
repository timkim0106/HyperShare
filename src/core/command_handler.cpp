#include "hypershare/core/command_handler.hpp"
#include "hypershare/core/logger.hpp"
#include "hypershare/core/config.hpp"
#include "hypershare/storage/chunk_manager.hpp"
#include "hypershare/storage/file_metadata.hpp"
#include "hypershare/storage/file_index.hpp"
#include "hypershare/storage/storage_config.hpp"
#include "hypershare/network/connection_manager.hpp"
#include "hypershare/network/file_announcer.hpp"
#include "hypershare/core/ipc_server.hpp"
#include "hypershare/core/ipc_client.hpp"
#include "hypershare/transfer/performance_monitor.hpp"
#include "hypershare/transfer/transfer_manager.hpp"
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
        
        // Initialize the database
        if (!file_index.initialize()) {
            return CommandResult::error("Failed to initialize file database");
        }
        
        // Create file metadata
        hypershare::storage::FileMetadata metadata;
        
        // Set basic file information before chunking
        metadata.filename = file_path.filename().string();
        metadata.file_path = std::filesystem::absolute(file_path).string();
        metadata.file_id = metadata.filename + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        
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
        
        // Create connection manager for this connection
        auto connection_manager = std::make_shared<hypershare::network::ConnectionManager>();
        
        // Initialize storage for file announcements
        auto storage_config = std::make_unique<hypershare::storage::StorageConfig>("./hypershare_data");
        storage_config->create_directories();
        auto file_index = std::make_shared<hypershare::storage::FileIndex>(storage_config->database_path);
        file_index->initialize();
        
        // Set up file announcer to receive remote file announcements
        connection_manager->initialize_file_announcer(file_index);
        
        // Start connection manager (this will create our local networking stack)
        int local_tcp_port = tcp_port + 1; // Use a different port to avoid conflict
        if (!connection_manager->start(local_tcp_port, 0)) { // 0 = disable UDP discovery for client
            return CommandResult::error("Failed to initialize networking");
        }
        
        // Connect to the remote peer
        if (!connection_manager->connect_to_peer(peer_ip, tcp_port)) {
            connection_manager->stop();
            return CommandResult::error("Failed to connect to peer at " + peer_ip + ":" + std::to_string(tcp_port));
        }
        
        std::cout << "✓ Connection established!\n";
        std::cout << "Waiting for file announcements from peer...\n";
        
        // Wait for file announcements
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        // Get remote files from the file announcer
        auto file_announcer = connection_manager->get_file_announcer();
        if (file_announcer) {
            auto remote_files = file_announcer->get_remote_files();
            
            std::cout << "Available files from peer " << peer_ip << ":\n";
            
            if (remote_files.empty()) {
                std::cout << "  No files shared by this peer.\n";
            } else {
                for (size_t i = 0; i < remote_files.size(); ++i) {
                    const auto& file = remote_files[i];
                    std::cout << "  [" << i + 1 << "] " << file.filename 
                              << " (" << file.file_size << " bytes)\n";
                    std::cout << "      File ID: " << file.file_id << "\n";
                    std::cout << "      Hash: " << file.file_hash << "\n";
                    if (!file.tags.empty()) {
                        std::cout << "      Tags: ";
                        for (size_t j = 0; j < file.tags.size(); ++j) {
                            if (j > 0) std::cout << ", ";
                            std::cout << file.tags[j];
                        }
                        std::cout << "\n";
                    }
                }
                
                std::cout << "\nTo download a file, use:\n";
                std::cout << "  hypershare download <file_id>\n";
            }
        }
        
        // Clean up
        connection_manager->stop();
        
        return CommandResult::ok("Connected and discovered files successfully");
        
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
        // Try to get status from running daemon first
        hypershare::core::IPCClient ipc_client;
        
        hypershare::core::IPCRequest request;
        request.command = "status";
        
        auto response = ipc_client.send_request(request);
        
        if (response && response->success) {
            // Daemon is running, get live data
            std::cout << "HyperShare Status (Live from Daemon):\n";
            std::cout << "Connected peers: " << response->data.at("peer_count") << "\n";
            
            // Get transfer statistics
            hypershare::core::IPCRequest transfer_request;
            transfer_request.command = "transfers";
            auto transfer_response = ipc_client.send_request(transfer_request);
            
            if (transfer_response && transfer_response->success) {
                int active_transfers = std::stoi(transfer_response->data.at("session_count"));
                std::cout << "Active transfers: " << active_transfers << "\n";
                
                if (active_transfers > 0) {
                    auto global_speed = std::stoull(transfer_response->data.at("global_speed"));
                    auto total_transferred = std::stoull(transfer_response->data.at("total_bytes_transferred"));
                    
                    std::cout << "Global transfer speed: ";
                    if (global_speed > 1024 * 1024) {
                        std::cout << (global_speed / (1024 * 1024)) << " MB/s\n";
                    } else if (global_speed > 1024) {
                        std::cout << (global_speed / 1024) << " KB/s\n";
                    } else {
                        std::cout << global_speed << " B/s\n";
                    }
                    
                    std::cout << "Total transferred: ";
                    if (total_transferred > 1024 * 1024 * 1024) {
                        std::cout << (total_transferred / (1024 * 1024 * 1024)) << " GB\n";
                    } else if (total_transferred > 1024 * 1024) {
                        std::cout << (total_transferred / (1024 * 1024)) << " MB\n";
                    } else if (total_transferred > 1024) {
                        std::cout << (total_transferred / 1024) << " KB\n";
                    } else {
                        std::cout << total_transferred << " bytes\n";
                    }
                }
            } else {
                std::cout << "Active transfers: 0\n";
            }
            
            std::cout << "Files shared: " << response->data.at("file_count") << "\n";
            std::cout << "Total shared size: " << response->data.at("total_size") << " bytes\n";
            std::cout << "Storage location: " << storage_config_->download_directory << "\n";
            std::cout << "Database: " << storage_config_->database_path << "\n";
            
            // Get file details from daemon
            hypershare::core::IPCRequest files_request;
            files_request.command = "files";
            auto files_response = ipc_client.send_request(files_request);
            
            if (files_response && files_response->success && !files_response->data.at("files").empty()) {
                std::cout << "\nShared files:\n";
                
                std::string files_info = files_response->data.at("files");
                std::istringstream files_stream(files_info);
                std::string file_entry;
                
                while (std::getline(files_stream, file_entry, ';')) {
                    if (file_entry.empty()) continue;
                    
                    // Parse file_id:filename:size:hash
                    std::istringstream entry_stream(file_entry);
                    std::string file_id, filename, size_str, hash;
                    
                    if (std::getline(entry_stream, file_id, ':') &&
                        std::getline(entry_stream, filename, ':') &&
                        std::getline(entry_stream, size_str, ':') &&
                        std::getline(entry_stream, hash, ':')) {
                        
                        std::cout << "  - " << filename << " (" << size_str << " bytes)\n";
                        std::cout << "    File ID: " << file_id << "\n";
                        std::cout << "    File Hash: " << hash << "\n";
                    }
                }
            }
            
            std::cout << "\nNetwork: Daemon running and accepting connections\n";
            
        } else {
            // Daemon not running, fall back to local data
            hypershare::storage::FileIndex file_index(storage_config_->database_path);
            file_index.initialize();
            auto shared_files = file_index.list_files();
            auto file_count = file_index.get_file_count();
            auto total_size = file_index.get_total_size();
            
            std::cout << "HyperShare Status (Daemon Not Running):\n";
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
                    std::cout << "    File ID: " << file.file_id << "\n";
                    std::cout << "    File Hash: " << file.file_hash << "\n";
                }
            }
            
            std::cout << "\nNetwork: Ready for connections (use 'hypershare start' to start daemon)\n";
        }
        
        return CommandResult::ok();
        
    } catch (const std::exception& e) {
        return CommandResult::error("Failed to get status: " + std::string(e.what()));
    }
}

// PeersCommandHandler Implementation
PeersCommandHandler::PeersCommandHandler() = default;

CommandResult PeersCommandHandler::execute(const std::vector<std::string>& args) {
    try {
        // Try to get peers from running daemon
        hypershare::core::IPCClient ipc_client;
        
        hypershare::core::IPCRequest request;
        request.command = "peers";
        
        auto response = ipc_client.send_request(request);
        
        if (response && response->success) {
            // Daemon is running, get live peer data
            std::cout << "Connected Peers (Live from Daemon):\n";
            
            int peer_count = std::stoi(response->data.at("peer_count"));
            if (peer_count == 0) {
                std::cout << "No peers currently connected\n";
            } else {
                std::cout << "Total connected peers: " << peer_count << "\n\n";
                
                std::string peers_info = response->data.at("peers");
                if (!peers_info.empty()) {
                    std::istringstream peers_stream(peers_info);
                    std::string peer_entry;
                    int index = 1;
                    
                    while (std::getline(peers_stream, peer_entry, ';')) {
                        if (peer_entry.empty()) continue;
                        
                        // Parse peer_id:peer_name:endpoint
                        std::istringstream entry_stream(peer_entry);
                        std::string peer_id, peer_name, endpoint;
                        
                        if (std::getline(entry_stream, peer_id, ':') &&
                            std::getline(entry_stream, peer_name, ':') &&
                            std::getline(entry_stream, endpoint, ':')) {
                            
                            std::cout << "  [" << index++ << "] Peer ID: " << peer_id << "\n";
                            std::cout << "      Name: " << peer_name << "\n";
                            std::cout << "      Address: " << endpoint << "\n";
                            std::cout << "      Status: Connected\n\n";
                        }
                    }
                }
            }
            
        } else {
            // Daemon not running
            std::cout << "Discovered Peers:\n";
            std::cout << "No peers currently discovered\n";
            std::cout << "(Peer discovery requires daemon to be running: 'hypershare start')\n";
        }
        
        return CommandResult::ok();
        
    } catch (const std::exception& e) {
        return CommandResult::error("Failed to get peers: " + std::string(e.what()));
    }
}

// DownloadCommandHandler Implementation
DownloadCommandHandler::DownloadCommandHandler()
    : storage_config_(std::make_unique<hypershare::storage::StorageConfig>("./hypershare_data")) {
}

CommandResult DownloadCommandHandler::execute(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return CommandResult::error("Usage: " + get_usage());
    }
    
    std::string file_id = args[1];
    std::string output_path = args.size() > 2 ? args[2] : "./downloads/";
    
    LOG_INFO("Downloading file: {}", file_id);
    
    try {
        // Initialize storage components
        hypershare::storage::FileIndex file_index(storage_config_->database_path);
        if (!file_index.initialize()) {
            return CommandResult::error("Failed to initialize file database");
        }
        
        // Try to find file by file_hash first
        auto metadata_opt = file_index.get_file(file_id);
        
        // If not found by hash, search through all files for matching file_id
        if (!metadata_opt) {
            auto all_files = file_index.list_files();
            for (const auto& file : all_files) {
                if (file.file_id == file_id) {
                    metadata_opt = file;
                    break;
                }
            }
        }
        
        if (!metadata_opt) {
            return CommandResult::error("File not found: " + file_id + "\nUse 'hypershare status' to see available files");
        }
        
        auto metadata = *metadata_opt;
        
        std::cout << "Found file: " << metadata.filename << "\n";
        std::cout << "Size: " << metadata.file_size << " bytes\n";
        std::cout << "Chunks: " << metadata.chunk_count << "\n";
        
        // For now, simulate the download process
        // In a real implementation, this would:
        // 1. Connect to peers that have this file
        // 2. Use TransferManager to coordinate chunk downloads
        // 3. Reassemble chunks into the complete file
        
        std::cout << "Downloading from peers...\n";
        
        // Create output directory if needed
        std::filesystem::path output_dir(output_path);
        if (!std::filesystem::exists(output_dir)) {
            std::filesystem::create_directories(output_dir);
        }
        
        // For demonstration, check if we have the file locally (self-download)
        std::filesystem::path local_file_path = metadata.file_path;
        std::filesystem::path output_file_path = output_dir / metadata.filename;
        
        if (std::filesystem::exists(local_file_path)) {
            // Simulate download by copying the file
            std::filesystem::copy_file(local_file_path, output_file_path, 
                                     std::filesystem::copy_options::overwrite_existing);
            
            std::cout << "✓ Download completed!\n";
            std::cout << "File saved to: " << output_file_path << "\n";
            std::cout << "\nNote: This was a local copy simulation.\n";
            std::cout << "Real peer-to-peer downloads will be implemented in the next phase.\n";
            
            return CommandResult::ok("File downloaded successfully");
        } else {
            return CommandResult::error("File not available locally. Peer-to-peer downloads not yet implemented.");
        }
        
    } catch (const std::exception& e) {
        return CommandResult::error("Download failed: " + std::string(e.what()));
    }
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
    
    auto connection_manager = std::make_shared<hypershare::network::ConnectionManager>();
    
    // Initialize storage for file announcements
    auto storage_config = std::make_unique<hypershare::storage::StorageConfig>("./hypershare_data");
    storage_config->create_directories();
    auto file_index = std::make_shared<hypershare::storage::FileIndex>(storage_config->database_path);
    file_index->initialize();
    
    // Set up file announcer
    connection_manager->initialize_file_announcer(file_index);
    
    // Set up performance monitor
    auto performance_monitor = std::make_shared<hypershare::transfer::PerformanceMonitor>();
    
    // Set up IPC server
    auto ipc_server = std::make_unique<hypershare::core::IPCServer>();
    ipc_server->set_connection_manager(connection_manager);
    ipc_server->set_file_index(file_index);
    ipc_server->set_performance_monitor(performance_monitor);
    
    if (!ipc_server->start()) {
        LOG_ERROR("Failed to start IPC server");
        return CommandResult::error("Failed to start IPC server");
    }
    
    if (!connection_manager->start(tcp_port, udp_port)) {
        LOG_ERROR("Failed to start connection manager");
        return CommandResult::error("Failed to start network services");
    }
    
    std::cout << "Network services started successfully\n";
    std::cout << "Peer discovery active on multicast group\n";
    std::cout << "IPC server running - CLI commands can now connect\n";
    
    // Keep the daemon running and show status
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        auto peer_count = connection_manager->get_connection_count();
        if (peer_count > 0) {
            LOG_INFO("Connected to {} peers", peer_count);
            std::cout << "Connected peers: " << peer_count << "\n";
        }
    }
    
    return CommandResult::ok();
}

// TransfersCommandHandler Implementation
TransfersCommandHandler::TransfersCommandHandler() = default;

CommandResult TransfersCommandHandler::execute(const std::vector<std::string>& args) {
    try {
        // Try to get transfer statistics from running daemon
        hypershare::core::IPCClient ipc_client;
        
        hypershare::core::IPCRequest request;
        request.command = "transfers";
        
        auto response = ipc_client.send_request(request);
        
        if (response && response->success) {
            // Daemon is running, show detailed transfer data
            int active_transfers = std::stoi(response->data.at("session_count"));
            
            std::cout << "Transfer Statistics (Live from Daemon):\n\n";
            
            if (active_transfers == 0) {
                std::cout << "No active transfers.\n";
            } else {
                // Show global statistics
                auto global_speed = std::stoull(response->data.at("global_speed"));
                auto total_transferred = std::stoull(response->data.at("total_bytes_transferred"));
                
                std::cout << "Global Statistics:\n";
                std::cout << "  Active sessions: " << active_transfers << "\n";
                std::cout << "  Combined speed: ";
                if (global_speed > 1024 * 1024) {
                    std::cout << (global_speed / (1024 * 1024)) << " MB/s\n";
                } else if (global_speed > 1024) {
                    std::cout << (global_speed / 1024) << " KB/s\n";
                } else {
                    std::cout << global_speed << " B/s\n";
                }
                
                std::cout << "  Total transferred: ";
                if (total_transferred > 1024 * 1024 * 1024) {
                    std::cout << (total_transferred / (1024 * 1024 * 1024)) << " GB\n";
                } else if (total_transferred > 1024 * 1024) {
                    std::cout << (total_transferred / (1024 * 1024)) << " MB\n";
                } else if (total_transferred > 1024) {
                    std::cout << (total_transferred / 1024) << " KB\n";
                } else {
                    std::cout << total_transferred << " bytes\n";
                }
                
                // Show individual transfer sessions
                std::string transfers_info = response->data.at("transfers");
                if (!transfers_info.empty()) {
                    std::cout << "\nActive Transfer Sessions:\n";
                    
                    std::istringstream transfers_stream(transfers_info);
                    std::string transfer_entry;
                    int index = 1;
                    
                    while (std::getline(transfers_stream, transfer_entry, ';')) {
                        if (transfer_entry.empty()) continue;
                        
                        // Parse session_id:total_bytes:bytes_transferred:percentage:current_speed:avg_speed:eta_ms
                        std::istringstream entry_stream(transfer_entry);
                        std::string session_id, total_bytes_str, transferred_str, percentage_str;
                        std::string current_speed_str, avg_speed_str, eta_str;
                        
                        if (std::getline(entry_stream, session_id, ':') &&
                            std::getline(entry_stream, total_bytes_str, ':') &&
                            std::getline(entry_stream, transferred_str, ':') &&
                            std::getline(entry_stream, percentage_str, ':') &&
                            std::getline(entry_stream, current_speed_str, ':') &&
                            std::getline(entry_stream, avg_speed_str, ':') &&
                            std::getline(entry_stream, eta_str, ':')) {
                            
                            auto total_bytes = std::stoull(total_bytes_str);
                            auto transferred = std::stoull(transferred_str);
                            auto percentage = std::stod(percentage_str);
                            auto current_speed = std::stoull(current_speed_str);
                            auto avg_speed = std::stoull(avg_speed_str);
                            auto eta_ms = std::stoull(eta_str);
                            
                            std::cout << "  [" << index++ << "] Session: " << session_id << "\n";
                            std::cout << "      Progress: " << transferred << "/" << total_bytes 
                                      << " bytes (" << std::fixed << std::setprecision(1) 
                                      << percentage << "%)\n";
                            
                            std::cout << "      Current speed: ";
                            if (current_speed > 1024 * 1024) {
                                std::cout << (current_speed / (1024 * 1024)) << " MB/s";
                            } else if (current_speed > 1024) {
                                std::cout << (current_speed / 1024) << " KB/s";
                            } else {
                                std::cout << current_speed << " B/s";
                            }
                            
                            std::cout << "  Average: ";
                            if (avg_speed > 1024 * 1024) {
                                std::cout << (avg_speed / (1024 * 1024)) << " MB/s\n";
                            } else if (avg_speed > 1024) {
                                std::cout << (avg_speed / 1024) << " KB/s\n";
                            } else {
                                std::cout << avg_speed << " B/s\n";
                            }
                            
                            if (eta_ms > 0) {
                                std::cout << "      ETA: ";
                                if (eta_ms > 60000) {
                                    std::cout << (eta_ms / 60000) << "m " << ((eta_ms % 60000) / 1000) << "s\n";
                                } else if (eta_ms > 1000) {
                                    std::cout << (eta_ms / 1000) << "s\n";
                                } else {
                                    std::cout << eta_ms << "ms\n";
                                }
                            } else {
                                std::cout << "      ETA: Calculating...\n";
                            }
                            std::cout << "\n";
                        }
                    }
                }
            }
            
        } else {
            // Daemon not running
            std::cout << "Transfer Statistics:\n";
            std::cout << "No transfer data available (daemon not running)\n";
            std::cout << "Start the daemon with 'hypershare start' to enable transfer monitoring.\n";
        }
        
        return CommandResult::ok();
        
    } catch (const std::exception& e) {
        return CommandResult::error("Failed to get transfer statistics: " + std::string(e.what()));
    }
}

}