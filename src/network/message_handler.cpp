#include "hypershare/network/message_handler.hpp"
#include "hypershare/core/logger.hpp"
#include <queue>
#include <mutex>

namespace hypershare::network {

void MessageHandler::handle_message(std::shared_ptr<Connection> connection, const MessageHeader& header, std::vector<std::uint8_t> payload) {
    auto it = handlers_.find(header.type);
    if (it != handlers_.end()) {
        try {
            it->second(connection, payload);
        } catch (const std::exception& e) {
            LOG_ERROR("Error handling message type {} from {}: {}", 
                      static_cast<int>(header.type), 
                      connection->get_remote_endpoint(), 
                      e.what());
        }
    } else {
        LOG_WARN("No handler registered for message type {} from {}", 
                 static_cast<int>(header.type), 
                 connection->get_remote_endpoint());
    }
}

std::pair<MessageHeader, std::vector<std::uint8_t>> MessageSerializer::deserialize_message(std::span<const std::uint8_t> data) {
    if (data.size() < MESSAGE_HEADER_SIZE) {
        throw std::runtime_error("Insufficient data for message header");
    }
    
    auto header = MessageHeader::deserialize(data.subspan(0, MESSAGE_HEADER_SIZE));
    
    if (!header.is_valid()) {
        throw std::runtime_error("Invalid message header");
    }
    
    if (data.size() < MESSAGE_HEADER_SIZE + header.payload_size) {
        throw std::runtime_error("Insufficient data for message payload");
    }
    
    std::vector<std::uint8_t> payload;
    if (header.payload_size > 0) {
        auto payload_span = data.subspan(MESSAGE_HEADER_SIZE, header.payload_size);
        payload.assign(payload_span.begin(), payload_span.end());
        
        if (!header.verify_checksum(payload)) {
            throw std::runtime_error("Message checksum verification failed");
        }
    }
    
    return {header, std::move(payload)};
}

MessageQueue::MessageQueue(std::size_t max_size) : max_size_(max_size) {}

void MessageQueue::push(const MessageHeader& header, std::vector<std::uint8_t> payload) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (normal_queue_.size() >= max_size_) {
        LOG_WARN("Message queue full, dropping oldest message");
        normal_queue_.pop();
    }
    
    QueuedMessage msg{
        header,
        std::move(payload),
        std::chrono::steady_clock::now(),
        0
    };
    
    normal_queue_.push(std::move(msg));
}

std::optional<MessageQueue::QueuedMessage> MessageQueue::pop() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (!priority_queue_.empty()) {
        auto msg = std::move(priority_queue_.front());
        priority_queue_.pop();
        return msg;
    }
    
    if (!normal_queue_.empty()) {
        auto msg = std::move(normal_queue_.front());
        normal_queue_.pop();
        return msg;
    }
    
    return std::nullopt;
}

void MessageQueue::push_priority(const MessageHeader& header, std::vector<std::uint8_t> payload) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (priority_queue_.size() >= max_size_ / 4) { // Reserve 25% for priority messages
        LOG_WARN("Priority message queue full, dropping oldest priority message");
        priority_queue_.pop();
    }
    
    QueuedMessage msg{
        header,
        std::move(payload),
        std::chrono::steady_clock::now(),
        0
    };
    
    priority_queue_.push(std::move(msg));
}

std::size_t MessageQueue::size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return normal_queue_.size() + priority_queue_.size();
}

bool MessageQueue::empty() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return normal_queue_.empty() && priority_queue_.empty();
}

void MessageQueue::clear() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    std::queue<QueuedMessage> empty_normal;
    std::queue<QueuedMessage> empty_priority;
    normal_queue_.swap(empty_normal);
    priority_queue_.swap(empty_priority);
}

void MessageQueue::cleanup_old_messages(std::chrono::milliseconds max_age) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    auto now = std::chrono::steady_clock::now();
    
    // Helper lambda to clean a queue
    auto clean_queue = [&](std::queue<QueuedMessage>& queue) {
        std::queue<QueuedMessage> temp_queue;
        
        while (!queue.empty()) {
            auto& msg = queue.front();
            if (now - msg.timestamp <= max_age) {
                temp_queue.push(std::move(msg));
            } else {
                LOG_DEBUG("Removing expired message type {} (age: {}ms)", 
                          static_cast<int>(msg.header.type),
                          std::chrono::duration_cast<std::chrono::milliseconds>(now - msg.timestamp).count());
            }
            queue.pop();
        }
        
        queue = std::move(temp_queue);
    };
    
    clean_queue(normal_queue_);
    clean_queue(priority_queue_);
}

}