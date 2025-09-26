#pragma once

#include "ipc_server.hpp"
#include <optional>

namespace hypershare::core {

class IPCClient {
public:
    IPCClient(const std::string& socket_path = "/tmp/hypershare.sock");
    ~IPCClient();
    
    std::optional<IPCResponse> send_request(const IPCRequest& request);
    bool is_daemon_running();
    
private:
    std::string socket_path_;
};

}