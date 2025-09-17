#include "hypershare/core/logger.hpp"
#include <spdlog/pattern_formatter.h>

namespace hypershare::core {

std::shared_ptr<spdlog::logger> Logger::logger_;

void Logger::initialize(const std::string& log_file, LogLevel level) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(static_cast<spdlog::level::level_enum>(level));
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_file, 1048576 * 5, 3);
    file_sink->set_level(spdlog::level::debug);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
    
    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    logger_ = std::make_shared<spdlog::logger>("hypershare", sinks.begin(), sinks.end());
    logger_->set_level(static_cast<spdlog::level::level_enum>(level));
    logger_->flush_on(spdlog::level::warn);
    
    spdlog::set_default_logger(logger_);
    spdlog::set_level(static_cast<spdlog::level::level_enum>(level));
    
    LOG_INFO("Logger initialized with level: {}", static_cast<int>(level));
}

void Logger::shutdown() {
    if (logger_) {
        LOG_INFO("Shutting down logger");
        logger_->flush();
        spdlog::shutdown();
        logger_.reset();
    }
}

}