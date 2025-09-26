#include "hypershare/core/ipc_client.hpp"
#include "hypershare/core/logger.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sstream>

namespace hypershare::core {

IPCClient::IPCClient(const std::string& socket_path)
    : socket_path_(socket_path) {
}

IPCClient::~IPCClient() {
}

std::optional<IPCResponse> IPCClient::send_request(const IPCRequest& request) {
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        LOG_ERROR("Failed to create Unix socket: {}", strerror(errno));
        return std::nullopt;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
    
    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_DEBUG("Failed to connect to daemon socket: {}", strerror(errno));
        close(sock_fd);
        return std::nullopt;
    }
    
    // Send request
    std::stringstream request_stream;
    request_stream << request.command;
    
    for (const auto& [key, value] : request.parameters) {
        request_stream << " " << key << "=" << value;
    }
    request_stream << "\n";
    
    std::string request_str = request_stream.str();
    if (write(sock_fd, request_str.c_str(), request_str.length()) < 0) {
        LOG_ERROR("Failed to write to daemon socket: {}", strerror(errno));
        close(sock_fd);
        return std::nullopt;
    }
    
    // Read response
    char buffer[4096];
    ssize_t bytes_read = read(sock_fd, buffer, sizeof(buffer) - 1);
    close(sock_fd);
    
    if (bytes_read <= 0) {
        LOG_ERROR("Failed to read from daemon socket");
        return std::nullopt;
    }
    
    buffer[bytes_read] = '\0';
    
    // Parse response
    std::istringstream response_stream(buffer);
    std::string line;
    
    IPCResponse response;
    
    // First line: SUCCESS or ERROR
    if (std::getline(response_stream, line)) {
        response.success = (line == "SUCCESS");
    }
    
    // Second line: message
    if (std::getline(response_stream, line)) {
        response.message = line;
    }
    
    // Remaining lines: key=value pairs until END
    while (std::getline(response_stream, line)) {
        if (line == "END") {
            break;
        }
        
        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);
            response.data[key] = value;
        }
    }
    
    return response;
}

bool IPCClient::is_daemon_running() {
    IPCRequest request;
    request.command = "status";
    
    auto response = send_request(request);
    return response.has_value() && response->success;
}

}