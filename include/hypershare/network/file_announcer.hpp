#pragma once

#include "hypershare/network/connection_manager.hpp"
#include "hypershare/storage/file_index.hpp"
#include "hypershare/storage/storage_config.hpp"
#include <memory>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <string>

namespace hypershare::network {

struct RemoteFileInfo {
    std::string file_id;
    std::string filename;
    std::uint64_t file_size;
    std::string file_hash;
    std::vector<std::string> tags;
    std::uint32_t peer_id;
    std::chrono::steady_clock::time_point last_announced;
};

class FileAnnouncer {
public:
    FileAnnouncer(std::shared_ptr<ConnectionManager> connection_manager,
                  std::shared_ptr<hypershare::storage::FileIndex> file_index);
    ~FileAnnouncer();
    
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    void announce_files();
    void announce_file(const hypershare::storage::FileMetadata& metadata);
    
    std::vector<RemoteFileInfo> get_remote_files() const;
    std::vector<RemoteFileInfo> get_remote_files_from_peer(std::uint32_t peer_id) const;
    std::optional<RemoteFileInfo> find_remote_file(const std::string& file_id) const;
    
    void set_announcement_interval(std::chrono::milliseconds interval) { 
        announcement_interval_ = interval; 
    }
    
    void set_file_timeout(std::chrono::milliseconds timeout) { 
        file_timeout_ = timeout; 
    }
    
    void handle_file_announce(std::shared_ptr<Connection> connection, const FileAnnounceMessage& msg);

private:
    void announcement_loop();
    void cleanup_expired_files();
    
    std::shared_ptr<ConnectionManager> connection_manager_;
    std::shared_ptr<hypershare::storage::FileIndex> file_index_;
    
    std::unordered_map<std::string, RemoteFileInfo> remote_files_;
    mutable std::mutex files_mutex_;
    
    std::thread announcement_thread_;
    std::chrono::milliseconds announcement_interval_;
    std::chrono::milliseconds file_timeout_;
    std::chrono::steady_clock::time_point last_announcement_;
    std::chrono::steady_clock::time_point last_cleanup_;
    
    bool running_;
};

}