#pragma once

#include <string>
#include <memory>
#include <thread>
#include <unordered_map>
#include <functional>

namespace hypershare::network {
    class ConnectionManager;
    class FileAnnouncer;
}

namespace hypershare::storage {
    class FileIndex;
}

namespace hypershare::transfer {
    class PerformanceMonitor;
}

namespace hypershare::core {

struct IPCRequest {
    std::string command;
    std::unordered_map<std::string, std::string> parameters;
};

struct IPCResponse {
    bool success;
    std::string message;
    std::unordered_map<std::string, std::string> data;
};

class IPCServer {
public:
    IPCServer(const std::string& socket_path = "/tmp/hypershare.sock");
    ~IPCServer();
    
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    void set_connection_manager(std::shared_ptr<hypershare::network::ConnectionManager> cm) {
        connection_manager_ = cm;
    }
    
    void set_file_index(std::shared_ptr<hypershare::storage::FileIndex> fi) {
        file_index_ = fi;
    }
    
    void set_performance_monitor(std::shared_ptr<hypershare::transfer::PerformanceMonitor> pm) {
        performance_monitor_ = pm;
    }

private:
    void accept_loop();
    void handle_client(int client_fd);
    
    IPCResponse handle_status_command(const IPCRequest& request);
    IPCResponse handle_peers_command(const IPCRequest& request);
    IPCResponse handle_files_command(const IPCRequest& request);
    IPCResponse handle_transfers_command(const IPCRequest& request);
    
    std::string socket_path_;
    int server_fd_;
    bool running_;
    std::thread accept_thread_;
    
    std::shared_ptr<hypershare::network::ConnectionManager> connection_manager_;
    std::shared_ptr<hypershare::storage::FileIndex> file_index_;
    std::shared_ptr<hypershare::transfer::PerformanceMonitor> performance_monitor_;
};

}