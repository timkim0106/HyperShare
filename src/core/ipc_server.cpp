#include "hypershare/core/ipc_server.hpp"
#include "hypershare/core/logger.hpp"
#include "hypershare/network/connection_manager.hpp"
#include "hypershare/network/file_announcer.hpp"
#include "hypershare/storage/file_index.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sstream>
#include <algorithm>

namespace hypershare::core {

IPCServer::IPCServer(const std::string& socket_path)
    : socket_path_(socket_path)
    , server_fd_(-1)
    , running_(false) {
    
    LOG_INFO("IPC server initialized with socket: {}", socket_path_);
}

IPCServer::~IPCServer() {
    stop();
}

bool IPCServer::start() {
    if (running_) {
        LOG_WARN("IPC server already running");
        return false;
    }
    
    // Remove existing socket file if it exists
    unlink(socket_path_.c_str());
    
    // Create Unix domain socket
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        LOG_ERROR("Failed to create Unix socket: {}", strerror(errno));
        return false;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind Unix socket: {}", strerror(errno));
        close(server_fd_);
        return false;
    }
    
    if (listen(server_fd_, 5) < 0) {
        LOG_ERROR("Failed to listen on Unix socket: {}", strerror(errno));
        close(server_fd_);
        return false;
    }
    
    running_ = true;
    
    // Start accept thread
    accept_thread_ = std::thread([this]() {
        accept_loop();
    });
    
    LOG_INFO("IPC server started on {}", socket_path_);
    return true;
}

void IPCServer::stop() {
    if (!running_) {
        return;
    }
    
    LOG_INFO("Stopping IPC server");
    running_ = false;
    
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    // Clean up socket file
    unlink(socket_path_.c_str());
}

void IPCServer::accept_loop() {
    LOG_INFO("IPC server accept loop started");
    
    while (running_) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) { // Only log error if we're still supposed to be running
                LOG_ERROR("Failed to accept IPC connection: {}", strerror(errno));
            }
            break;
        }
        
        // Handle client in separate thread
        std::thread client_thread([this, client_fd]() {
            handle_client(client_fd);
        });
        client_thread.detach();
    }
    
    LOG_INFO("IPC server accept loop stopped");
}

void IPCServer::handle_client(int client_fd) {
    char buffer[4096];
    
    // Read request
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[bytes_read] = '\0';
    
    try {
        // Parse simple text request (format: "COMMAND param1=value1 param2=value2")
        std::string request_str(buffer);
        request_str.erase(std::remove(request_str.begin(), request_str.end(), '\n'), request_str.end());
        request_str.erase(std::remove(request_str.begin(), request_str.end(), '\r'), request_str.end());
        
        std::istringstream iss(request_str);
        std::string token;
        
        IPCRequest request;
        if (iss >> request.command) {
            // Parse parameters (key=value format)
            while (iss >> token) {
                size_t eq_pos = token.find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = token.substr(0, eq_pos);
                    std::string value = token.substr(eq_pos + 1);
                    request.parameters[key] = value;
                }
            }
        }
        
        // Handle command
        IPCResponse response;
        if (request.command == "status") {
            response = handle_status_command(request);
        } else if (request.command == "peers") {
            response = handle_peers_command(request);
        } else if (request.command == "files") {
            response = handle_files_command(request);
        } else {
            response.success = false;
            response.message = "Unknown command: " + request.command;
        }
        
        // Send simple text response
        std::stringstream response_stream;
        response_stream << (response.success ? "SUCCESS" : "ERROR") << "\n";
        response_stream << response.message << "\n";
        
        for (const auto& [key, value] : response.data) {
            response_stream << key << "=" << value << "\n";
        }
        response_stream << "END\n";
        
        std::string response_str = response_stream.str();
        write(client_fd, response_str.c_str(), response_str.length());
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error handling IPC client: {}", e.what());
    }
    
    close(client_fd);
}

IPCResponse IPCServer::handle_status_command(const IPCRequest& request) {
    IPCResponse response;
    response.success = true;
    response.message = "Status retrieved successfully";
    
    try {
        if (connection_manager_) {
            response.data["peer_count"] = std::to_string(connection_manager_->get_connection_count());
        } else {
            response.data["peer_count"] = "0";
        }
        
        if (file_index_) {
            response.data["file_count"] = std::to_string(file_index_->get_file_count());
            response.data["total_size"] = std::to_string(file_index_->get_total_size());
        } else {
            response.data["file_count"] = "0";
            response.data["total_size"] = "0";
        }
        
        response.data["daemon_running"] = "true";
        
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Failed to get status: " + std::string(e.what());
    }
    
    return response;
}

IPCResponse IPCServer::handle_peers_command(const IPCRequest& request) {
    IPCResponse response;
    response.success = true;
    response.message = "Peers retrieved successfully";
    
    try {
        if (connection_manager_) {
            auto connections = connection_manager_->get_connections();
            response.data["peer_count"] = std::to_string(connections.size());
            
            std::stringstream peers_info;
            for (size_t i = 0; i < connections.size(); ++i) {
                const auto& conn = connections[i];
                if (i > 0) peers_info << ";";
                peers_info << conn.peer_id << ":" << conn.peer_name << ":" 
                          << conn.connection->get_remote_endpoint();
            }
            response.data["peers"] = peers_info.str();
        } else {
            response.data["peer_count"] = "0";
            response.data["peers"] = "";
        }
        
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Failed to get peers: " + std::string(e.what());
    }
    
    return response;
}

IPCResponse IPCServer::handle_files_command(const IPCRequest& request) {
    IPCResponse response;
    response.success = true;
    response.message = "Files retrieved successfully";
    
    try {
        if (file_index_) {
            auto files = file_index_->list_files();
            response.data["file_count"] = std::to_string(files.size());
            
            std::stringstream files_info;
            for (size_t i = 0; i < files.size(); ++i) {
                const auto& file = files[i];
                if (i > 0) files_info << ";";
                files_info << file.file_id << ":" << file.filename << ":" 
                          << file.file_size << ":" << file.file_hash;
            }
            response.data["files"] = files_info.str();
        } else {
            response.data["file_count"] = "0";
            response.data["files"] = "";
        }
        
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Failed to get files: " + std::string(e.what());
    }
    
    return response;
}

}