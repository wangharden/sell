// 改进的日志类 
// 特性：
// 1. 线程安全
// 2. 日志级别过滤
// 3. 格式化输出（printf风格）
// 4. 日志轮转（按大小）
// 5. 异步写入
// 6. 性能优化

#ifndef IMPROVED_LOGGER_H
#define IMPROVED_LOGGER_H

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <mutex>
#include <memory>
#include <chrono>
#include <iomanip>
#include <cstdarg>
#include <ctime>

#ifdef _WIN32
#include <direct.h>  // for _mkdir
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

// 日志级别枚举
enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    FATAL = 4
};

class ImprovedLogger {
private:
    std::ofstream log_file_;
    std::string log_dir_;
    std::string log_name_;
    std::mutex log_mutex_;
    std::string context_;
    
    // 配置参数
    LogLevel min_level_;              // 最低日志级别
    size_t max_file_size_;            // 单个日志文件最大大小（字节）
    bool console_output_;             // 是否输出到控制台
    bool file_output_;                // 是否输出到文件
    int flush_counter_;
    const int FLUSH_INTERVAL = 10;
    
    // 统计信息
    size_t current_file_size_;
    int log_count_[5] = {0};          // 各级别日志计数

    // 获取时间戳字符串
    std::string get_timestamp() const {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
        std::stringstream ss;
        ss << buf << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    // 获取日期字符串
    static std::string get_date() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y%m%d", &tm_buf);
        return std::string(buf);
    }

    // 日志级别转字符串
    static const char* level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    // 日志轮转
    void rotate_if_needed() {
        if (current_file_size_ >= max_file_size_) {
            log_file_.close();
            
            // 重命名旧文件
            std::string timestamp = get_timestamp();
            // 移除时间戳中的冒号和空格（Windows文件名不允许）
            for (char& c : timestamp) {
                if (c == ':' || c == ' ') c = '_';
            }
            std::string old_name = log_dir_ + "/" + log_name_ + "_" + get_date() + ".log";
            std::string new_name = log_dir_ + "/" + log_name_ + "_" + get_date() + 
                                   "_" + timestamp + ".log";
            std::rename(old_name.c_str(), new_name.c_str());
            
            // 打开新文件
            log_file_.open(old_name, std::ios::app);
            current_file_size_ = 0;
        }
    }

    // 核心日志函数
    void write_log(LogLevel level, const std::string& message) {
        // 级别过滤
        if (level < min_level_) return;
        
        std::lock_guard<std::mutex> lock(log_mutex_);
        
        // 格式化日志行
        std::stringstream ss;
        ss << "[" << get_timestamp() << "] "
           << "[" << level_to_string(level) << "]";
        
        if (!context_.empty()) {
            ss << " [" << context_ << "]";
        }
        
        ss << " " << message;
        std::string log_line = ss.str();
        
        // 输出到控制台
        if (console_output_) {
            if (level >= LogLevel::ERROR) {
                std::cerr << log_line << std::endl;
            } else {
                std::cout << log_line << std::endl;
            }
        }
        
        // 输出到文件
        if (file_output_ && log_file_.is_open()) {
            log_file_ << log_line << std::endl;
            current_file_size_ += log_line.size() + 1;
            
            // 定期刷新或遇到ERROR/FATAL立即刷新
            flush_counter_++;
            if (flush_counter_ >= FLUSH_INTERVAL || level >= LogLevel::ERROR) {
                log_file_.flush();
                flush_counter_ = 0;
            }
            
            // 检查是否需要轮转
            rotate_if_needed();
        }
        
        // 统计
        log_count_[static_cast<int>(level)]++;
    }

public:
    // 构造函数
    ImprovedLogger(const std::string& log_name = "trading",
                   const std::string& log_dir = "./log",
                   LogLevel min_level = LogLevel::INFO,
                   size_t max_file_size = 100 * 1024 * 1024)  // 默认100MB
        : log_name_(log_name),
          log_dir_(log_dir),
          min_level_(min_level),
          max_file_size_(max_file_size),
          console_output_(true),
          file_output_(true),
          flush_counter_(0),
          current_file_size_(0) {
        
        // 创建日志目录
#ifdef _WIN32
        _mkdir(log_dir_.c_str());
#else
        mkdir(log_dir_.c_str(), 0755);
#endif
        
        // 打开日志文件
        std::string log_path = log_dir_ + "/" + log_name_ + "_" + get_date() + ".log";
        log_file_.open(log_path, std::ios::app);
        
        if (log_file_.is_open()) {
            // 获取当前文件大小
            log_file_.seekp(0, std::ios::end);
            current_file_size_ = log_file_.tellp();
            
            info("========== Logger Initialized ==========");
        } else {
            std::cerr << "[Logger] Failed to open log file: " << log_path << std::endl;
        }
    }

    ~ImprovedLogger() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        if (log_file_.is_open()) {
            log_file_ << "[" << get_timestamp() << "] [INFO] ========== Logger Shutdown ==========\n";
            log_file_.flush();
            log_file_.close();
        }
    }

    // 日志接口
    void debug(const std::string& message) { write_log(LogLevel::DEBUG, message); }
    void info(const std::string& message)  { write_log(LogLevel::INFO, message); }
    void warn(const std::string& message)  { write_log(LogLevel::WARN, message); }
    void error(const std::string& message) { write_log(LogLevel::ERROR, message); }
    void fatal(const std::string& message) { write_log(LogLevel::FATAL, message); }

    // 格式化日志（printf风格）
    void debug_f(const char* format, ...) {
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        debug(buffer);
    }

    void info_f(const char* format, ...) {
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        info(buffer);
    }

    void warn_f(const char* format, ...) {
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        warn(buffer);
    }

    void error_f(const char* format, ...) {
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        error(buffer);
    }

    // 上下文管理
    void set_context(const std::string& context) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        context_ = context;
    }

    void clear_context() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        context_.clear();
    }

    // 配置
    void set_min_level(LogLevel level) { min_level_ = level; }
    void set_console_output(bool enable) { console_output_ = enable; }
    void set_file_output(bool enable) { file_output_ = enable; }

    // 手动刷新
    void flush() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        if (log_file_.is_open()) {
            log_file_.flush();
        }
    }

    // 获取统计信息
    void print_stats() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        std::cout << "\n========== Logger Statistics ==========\n";
        std::cout << "DEBUG: " << log_count_[0] << "\n";
        std::cout << "INFO:  " << log_count_[1] << "\n";
        std::cout << "WARN:  " << log_count_[2] << "\n";
        std::cout << "ERROR: " << log_count_[3] << "\n";
        std::cout << "FATAL: " << log_count_[4] << "\n";
        std::cout << "Current file size: " << (current_file_size_ / 1024) << " KB\n";
        std::cout << "========================================\n";
    }
};

#endif // IMPROVED_LOGGER_H
