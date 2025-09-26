#include "hypershare/network/file_announcer.hpp"
#include "hypershare/core/logger.hpp"

namespace hypershare::network {

FileAnnouncer::FileAnnouncer(std::shared_ptr<ConnectionManager> connection_manager,
                           std::shared_ptr<hypershare::storage::FileIndex> file_index)
    : connection_manager_(connection_manager)
    , file_index_(file_index)
    , announcement_interval_(std::chrono::minutes(5))
    , file_timeout_(std::chrono::minutes(10))
    , last_announcement_(std::chrono::steady_clock::now())
    , last_cleanup_(std::chrono::steady_clock::now())
    , running_(false) {
    
    LOG_INFO("File announcer initialized");
}

FileAnnouncer::~FileAnnouncer() {
    stop();
}

bool FileAnnouncer::start() {
    if (running_) {
        LOG_WARN("File announcer already running");
        return false;
    }
    
    running_ = true;
    
    announcement_thread_ = std::thread([this]() {
        announcement_loop();
    });
    
    LOG_INFO("File announcer started");
    return true;
}

void FileAnnouncer::stop() {
    if (!running_) {
        return;
    }
    
    LOG_INFO("Stopping file announcer");
    running_ = false;
    
    if (announcement_thread_.joinable()) {
        announcement_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(files_mutex_);
    remote_files_.clear();
}

void FileAnnouncer::announce_files() {
    if (!running_ || !file_index_) {
        return;
    }
    
    try {
        auto local_files = file_index_->list_files();
        
        LOG_DEBUG("Announcing {} files to peers", local_files.size());
        
        for (const auto& file : local_files) {
            announce_file(file);
        }
        
        last_announcement_ = std::chrono::steady_clock::now();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to announce files: {}", e.what());
    }
}

void FileAnnouncer::announce_file(const hypershare::storage::FileMetadata& metadata) {
    if (!running_ || !connection_manager_) {
        return;
    }
    
    FileAnnounceMessage announce_msg{
        metadata.file_id,
        metadata.filename,
        metadata.file_size,
        metadata.file_hash,
        metadata.tags
    };
    
    connection_manager_->broadcast_message(MessageType::FILE_ANNOUNCE, announce_msg);
    
    LOG_DEBUG("Announced file: {} ({})", metadata.filename, metadata.file_id);
}

std::vector<RemoteFileInfo> FileAnnouncer::get_remote_files() const {
    std::lock_guard<std::mutex> lock(files_mutex_);
    std::vector<RemoteFileInfo> files;
    files.reserve(remote_files_.size());
    
    for (const auto& [id, info] : remote_files_) {
        files.push_back(info);
    }
    
    return files;
}

std::vector<RemoteFileInfo> FileAnnouncer::get_remote_files_from_peer(std::uint32_t peer_id) const {
    std::lock_guard<std::mutex> lock(files_mutex_);
    std::vector<RemoteFileInfo> files;
    
    for (const auto& [id, info] : remote_files_) {
        if (info.peer_id == peer_id) {
            files.push_back(info);
        }
    }
    
    return files;
}

std::optional<RemoteFileInfo> FileAnnouncer::find_remote_file(const std::string& file_id) const {
    std::lock_guard<std::mutex> lock(files_mutex_);
    auto it = remote_files_.find(file_id);
    if (it != remote_files_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void FileAnnouncer::handle_file_announce(std::shared_ptr<Connection> connection, const FileAnnounceMessage& msg) {
    auto peer_id = connection->get_peer_id();
    if (peer_id == 0) {
        LOG_WARN("Received file announce from peer with unknown ID");
        return;
    }
    
    std::lock_guard<std::mutex> lock(files_mutex_);
    
    RemoteFileInfo info{
        msg.file_id,
        msg.filename,
        msg.file_size,
        msg.file_hash,
        msg.tags,
        peer_id,
        std::chrono::steady_clock::now()
    };
    
    auto key = msg.file_id + "_" + std::to_string(peer_id);
    bool is_new_file = (remote_files_.find(key) == remote_files_.end());
    
    remote_files_[key] = info;
    
    if (is_new_file) {
        LOG_INFO("Discovered file from peer {}: {} ({})", peer_id, msg.filename, msg.file_id);
    } else {
        LOG_DEBUG("Updated file info from peer {}: {} ({})", peer_id, msg.filename, msg.file_id);
    }
}

void FileAnnouncer::announcement_loop() {
    LOG_INFO("File announcement loop started");
    
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        
        if (now - last_announcement_ >= announcement_interval_) {
            announce_files();
        }
        
        if (now - last_cleanup_ >= std::chrono::seconds(60)) {
            cleanup_expired_files();
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    
    LOG_INFO("File announcement loop stopped");
}

void FileAnnouncer::cleanup_expired_files() {
    std::lock_guard<std::mutex> lock(files_mutex_);
    auto now = std::chrono::steady_clock::now();
    
    auto it = remote_files_.begin();
    while (it != remote_files_.end()) {
        if (now - it->second.last_announced > file_timeout_) {
            LOG_DEBUG("Expired file from peer {}: {} ({})", 
                     it->second.peer_id, it->second.filename, it->second.file_id);
            it = remote_files_.erase(it);
        } else {
            ++it;
        }
    }
    
    last_cleanup_ = now;
}

}