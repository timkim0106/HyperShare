#pragma once

#include "../transfer/performance_monitor.hpp"
#include <string>
#include <vector>
#include <set>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>

struct sqlite3;

namespace hypershare::storage {

struct ResumeInfo {
    std::string file_id;
    std::string session_id;
    std::set<uint64_t> completed_chunks;
    std::chrono::system_clock::time_point last_activity;
    hypershare::transfer::SessionStats stats;
};

class ResumeManager {
public:
    explicit ResumeManager(const std::filesystem::path& database_path);
    ~ResumeManager();
    
    // Database operations
    bool initialize();
    
    // Resume state management
    bool save_resume_state(const ResumeInfo& info);
    std::optional<ResumeInfo> load_resume_state(const std::string& file_id);
    std::optional<ResumeInfo> load_resume_state_by_session(const std::string& session_id);
    
    bool remove_resume_state(const std::string& file_id);
    bool remove_resume_state_by_session(const std::string& session_id);
    
    // Chunk progress tracking
    bool update_chunk_completed(const std::string& file_id, uint64_t chunk_index);
    bool update_chunk_completed_by_session(const std::string& session_id, uint64_t chunk_index);
    
    std::set<uint64_t> get_completed_chunks(const std::string& file_id);
    std::vector<uint64_t> get_missing_chunks(const std::string& file_id, uint64_t total_chunks);
    
    // Activity tracking
    bool update_last_activity(const std::string& file_id);
    bool update_last_activity_by_session(const std::string& session_id);
    
    // Cleanup operations
    void cleanup_old_resume_states(std::chrono::hours max_age = std::chrono::hours(72));
    std::vector<ResumeInfo> list_resumable_transfers();
    
    // Statistics
    size_t get_resume_state_count() const;
    bool is_resumable(const std::string& file_id) const;
    
private:
    std::filesystem::path db_path_;
    sqlite3* db_;
    
    bool create_tables();
    bool prepare_statements();
    void cleanup_statements();
    
    // Serialization helpers
    std::vector<uint8_t> serialize_completed_chunks(const std::set<uint64_t>& chunks);
    std::set<uint64_t> deserialize_completed_chunks(const std::vector<uint8_t>& data);
    
    std::vector<uint8_t> serialize_session_stats(const hypershare::transfer::SessionStats& stats);
    hypershare::transfer::SessionStats deserialize_session_stats(const std::vector<uint8_t>& data);
};

} // namespace hypershare::storage