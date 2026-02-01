#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

/// @brief 简易 JSON 配置读取器（不依赖第三方库）
class ConfigReader {
private:
    std::string content_;
    
    /// @brief 从 JSON 字符串中提取字段值
    std::string extract_value(const std::string& key) const {
        size_t pos = content_.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        
        pos = content_.find(":", pos);
        if (pos == std::string::npos) return "";
        
        pos = content_.find("\"", pos);
        if (pos == std::string::npos) return "";
        
        size_t end = content_.find("\"", pos + 1);
        if (end == std::string::npos) return "";
        
        return content_.substr(pos + 1, end - pos - 1);
    }
    
    int extract_int(const std::string& key) const {
        size_t pos = content_.find("\"" + key + "\"");
        if (pos == std::string::npos) return 0;
        
        pos = content_.find(":", pos);
        if (pos == std::string::npos) return 0;
        
        // 跳过空格和逗号
        while (pos < content_.length() && 
               (content_[pos] == ':' || content_[pos] == ' ' || 
                content_[pos] == '\t' || content_[pos] == '\n')) {
            pos++;
        }
        
        size_t end = pos;
        while (end < content_.length() && 
               (content_[end] >= '0' && content_[end] <= '9')) {
            end++;
        }
        
        if (end > pos) {
            return std::stoi(content_.substr(pos, end - pos));
        }
        return 0;
    }

public:
    /// @brief 从文件加载配置
    bool load(const std::string& file_path) {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            std::cerr << "[Config] 无法打开配置文件: " << file_path << std::endl;
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        content_ = buffer.str();
        
        std::cout << "[Config] 配置文件加载成功: " << file_path << std::endl;
        return true;
    }
    
    /// @brief 获取交易服务器地址
    std::string get_trading_host() const { return extract_value("host"); }
    
    /// @brief 获取交易端口
    int get_trading_port() const { 
        // 先找到 "trading" 块
        size_t trading_pos = content_.find("\"trading\"");
        if (trading_pos == std::string::npos) return 0;
        
        // 在 trading 块中查找 port
        size_t port_pos = content_.find("\"port\"", trading_pos);
        size_t next_section = content_.find("\"market\"", trading_pos);
        
        if (port_pos != std::string::npos && 
            (next_section == std::string::npos || port_pos < next_section)) {
            return extract_int("port");
        }
        return 0;
    }
    
    /// @brief 获取交易账号
    std::string get_trading_account() const { 
        size_t trading_pos = content_.find("\"trading\"");
        if (trading_pos == std::string::npos) return "";
        
        size_t account_pos = content_.find("\"account\"", trading_pos);
        size_t next_section = content_.find("\"market\"", trading_pos);
        
        if (account_pos != std::string::npos && 
            (next_section == std::string::npos || account_pos < next_section)) {
            return extract_value("account");
        }
        return "";
    }
    
    /// @brief 获取交易密码
    std::string get_trading_password() const { 
        size_t trading_pos = content_.find("\"trading\"");
        if (trading_pos == std::string::npos) return "";
        
        size_t pwd_pos = content_.find("\"password\"", trading_pos);
        size_t next_section = content_.find("\"market\"", trading_pos);
        
        if (pwd_pos != std::string::npos && 
            (next_section == std::string::npos || pwd_pos < next_section)) {
            return extract_value("password");
        }
        return "";
    }
    
    /// @brief 获取配置段名称
    std::string get_config_section() const { return extract_value("config_section"); }
    
    /// @brief 获取行情服务器地址
    std::string get_market_host() const {
        size_t market_pos = content_.find("\"market\"");
        if (market_pos == std::string::npos) return "";
        
        size_t host_pos = content_.find("\"host\"", market_pos);
        size_t next_section = content_.find("\"strategy\"", market_pos);
        
        if (host_pos != std::string::npos && 
            (next_section == std::string::npos || host_pos < next_section)) {
            // 手动提取（避免和 trading.host 冲突）
            size_t start = content_.find("\"", host_pos + 6);
            if (start == std::string::npos) return "";
            size_t end = content_.find("\"", start + 1);
            if (end == std::string::npos) return "";
            return content_.substr(start + 1, end - start - 1);
        }
        return "";
    }
    
    /// @brief 获取行情端口
    int get_market_port() const {
        size_t market_pos = content_.find("\"market\"");
        if (market_pos == std::string::npos) return 0;
        
        size_t port_pos = content_.find("\"port\"", market_pos);
        size_t next_section = content_.find("\"strategy\"", market_pos);
        
        if (port_pos != std::string::npos && 
            (next_section == std::string::npos || port_pos < next_section)) {
            // 手动解析数字
            size_t colon = content_.find(":", port_pos);
            if (colon == std::string::npos) return 0;
            
            size_t num_start = colon + 1;
            while (num_start < content_.length() && 
                   (content_[num_start] == ' ' || content_[num_start] == '\t')) {
                num_start++;
            }
            
            size_t num_end = num_start;
            while (num_end < content_.length() && 
                   content_[num_end] >= '0' && content_[num_end] <= '9') {
                num_end++;
            }
            
            if (num_end > num_start) {
                return std::stoi(content_.substr(num_start, num_end - num_start));
            }
        }
        return 0;
    }
    
    /// @brief 获取行情用户名
    std::string get_market_user() const {
        size_t market_pos = content_.find("\"market\"");
        if (market_pos == std::string::npos) return "";
        
        size_t user_pos = content_.find("\"user\"", market_pos);
        size_t next_section = content_.find("\"strategy\"", market_pos);
        
        if (user_pos != std::string::npos && 
            (next_section == std::string::npos || user_pos < next_section)) {
            size_t start = content_.find("\"", user_pos + 6);
            if (start == std::string::npos) return "";
            size_t end = content_.find("\"", start + 1);
            if (end == std::string::npos) return "";
            return content_.substr(start + 1, end - start - 1);
        }
        return "";
    }
    
    /// @brief 获取行情密码
    std::string get_market_password() const {
        size_t market_pos = content_.find("\"market\"");
        if (market_pos == std::string::npos) return "";
        
        size_t pwd_pos = content_.find("\"password\"", market_pos);
        size_t next_section = content_.find("\"strategy\"", market_pos);
        
        if (pwd_pos != std::string::npos && 
            (next_section == std::string::npos || pwd_pos < next_section)) {
            size_t start = content_.find("\"", pwd_pos + 10);
            if (start == std::string::npos) return "";
            size_t end = content_.find("\"", start + 1);
            if (end == std::string::npos) return "";
            return content_.substr(start + 1, end - start - 1);
        }
        return "";
    }
    
    /// @brief 获取 CSV 路径
    std::string get_csv_path() const { 
        size_t strategy_pos = content_.find("\"strategy\"");
        if (strategy_pos == std::string::npos) return "";
        
        size_t csv_pos = content_.find("\"csv_path\"", strategy_pos);
        if (csv_pos != std::string::npos) {
            size_t start = content_.find("\"", csv_pos + 10);
            if (start == std::string::npos) return "";
            size_t end = content_.find("\"", start + 1);
            if (end == std::string::npos) return "";
            return content_.substr(start + 1, end - start - 1);
        }
        return "";
    }
    
    /// @brief 获取策略账号ID
    std::string get_account_id() const { 
        size_t strategy_pos = content_.find("\"strategy\"");
        if (strategy_pos == std::string::npos) return "";
        
        size_t id_pos = content_.find("\"account_id\"", strategy_pos);
        if (id_pos != std::string::npos) {
            size_t start = content_.find("\"", id_pos + 12);
            if (start == std::string::npos) return "";
            size_t end = content_.find("\"", start + 1);
            if (end == std::string::npos) return "";
            return content_.substr(start + 1, end - start - 1);
        }
        return "";
    }

    /// @brief 获取策略 sell_to_mkt_ratio
    double get_strategy_sell_to_mkt_ratio(double default_val = 0.1) const {
        size_t strategy_pos = content_.find("\"strategy\"");
        if (strategy_pos == std::string::npos) return default_val;

        size_t key_pos = content_.find("\"sell_to_mkt_ratio\"", strategy_pos);
        if (key_pos == std::string::npos) return default_val;

        size_t colon = content_.find(":", key_pos);
        if (colon == std::string::npos) return default_val;

        size_t num_start = colon + 1;
        while (num_start < content_.length() &&
               (content_[num_start] == ' ' || content_[num_start] == '\t' ||
                content_[num_start] == '\n')) {
            num_start++;
        }

        size_t num_end = num_start;
        bool dot_seen = false;
        if (num_end < content_.length() &&
            (content_[num_end] == '-' || content_[num_end] == '+')) {
            num_end++;
        }
        while (num_end < content_.length()) {
            char c = content_[num_end];
            if ((c >= '0' && c <= '9') || (c == '.' && !dot_seen)) {
                if (c == '.') {
                    dot_seen = true;
                }
                num_end++;
                continue;
            }
            break;
        }

        if (num_end > num_start) {
            return std::stod(content_.substr(num_start, num_end - num_start));
        }
        return default_val;
    }

    /// @brief 获取策略 phase1_sell_ratio
    double get_strategy_phase1_sell_ratio(double default_val = 0.1) const {
        size_t strategy_pos = content_.find("\"strategy\"");
        if (strategy_pos == std::string::npos) return default_val;

        size_t key_pos = content_.find("\"phase1_sell_ratio\"", strategy_pos);
        if (key_pos == std::string::npos) return default_val;

        size_t colon = content_.find(":", key_pos);
        if (colon == std::string::npos) return default_val;

        size_t num_start = colon + 1;
        while (num_start < content_.length() &&
               (content_[num_start] == ' ' || content_[num_start] == '\t' ||
                content_[num_start] == '\n')) {
            num_start++;
        }

        size_t num_end = num_start;
        bool dot_seen = false;
        if (num_end < content_.length() &&
            (content_[num_end] == '-' || content_[num_end] == '+')) {
            num_end++;
        }
        while (num_end < content_.length()) {
            char c = content_[num_end];
            if ((c >= '0' && c <= '9') || (c == '.' && !dot_seen)) {
                if (c == '.') {
                    dot_seen = true;
                }
                num_end++;
                continue;
            }
            break;
        }

        if (num_end > num_start) {
            return std::stod(content_.substr(num_start, num_end - num_start));
        }
        return default_val;
    }

    /// @brief 获取策略 input_amt
    double get_strategy_input_amt(double default_val = 600000.0) const {
        size_t strategy_pos = content_.find("\"strategy\"");
        if (strategy_pos == std::string::npos) return default_val;

        size_t key_pos = content_.find("\"input_amt\"", strategy_pos);
        if (key_pos == std::string::npos) return default_val;

        size_t colon = content_.find(":", key_pos);
        if (colon == std::string::npos) return default_val;

        size_t num_start = colon + 1;
        while (num_start < content_.length() &&
               (content_[num_start] == ' ' || content_[num_start] == '\t' ||
                content_[num_start] == '\n')) {
            num_start++;
        }

        size_t num_end = num_start;
        bool dot_seen = false;
        if (num_end < content_.length() &&
            (content_[num_end] == '-' || content_[num_end] == '+')) {
            num_end++;
        }
        while (num_end < content_.length()) {
            char c = content_[num_end];
            if ((c >= '0' && c <= '9') || (c == '.' && !dot_seen)) {
                if (c == '.') {
                    dot_seen = true;
                }
                num_end++;
                continue;
            }
            break;
        }

        if (num_end > num_start) {
            return std::stod(content_.substr(num_start, num_end - num_start));
        }
        return default_val;
    }

    /// @brief 获取策略 hold_vol
    int64_t get_strategy_hold_vol(int64_t default_val = 300) const {
        size_t strategy_pos = content_.find("\"strategy\"");
        if (strategy_pos == std::string::npos) return default_val;

        size_t key_pos = content_.find("\"hold_vol\"", strategy_pos);
        if (key_pos == std::string::npos) return default_val;

        size_t colon = content_.find(":", key_pos);
        if (colon == std::string::npos) return default_val;

        size_t num_start = colon + 1;
        while (num_start < content_.length() &&
               (content_[num_start] == ' ' || content_[num_start] == '\t' ||
                content_[num_start] == '\n')) {
            num_start++;
        }

        size_t num_end = num_start;
        if (num_end < content_.length() &&
            (content_[num_end] == '-' || content_[num_end] == '+')) {
            num_end++;
        }
        while (num_end < content_.length()) {
            char c = content_[num_end];
            if (c >= '0' && c <= '9') {
                num_end++;
                continue;
            }
            break;
        }

        if (num_end > num_start) {
            return static_cast<int64_t>(std::stoll(content_.substr(num_start, num_end - num_start)));
        }
        return default_val;
    }

    /// @brief 获取策略 code_min（可选）
    std::string get_code_min() const {
        size_t strategy_pos = content_.find("\"strategy\"");
        if (strategy_pos == std::string::npos) return "";

        size_t key_pos = content_.find("\"code_min\"", strategy_pos);
        if (key_pos == std::string::npos) return "";

        size_t start = content_.find("\"", key_pos + 10);
        if (start == std::string::npos) return "";
        size_t end = content_.find("\"", start + 1);
        if (end == std::string::npos) return "";
        return content_.substr(start + 1, end - start - 1);
    }

    /// @brief 获取策略 code_max（可选）
    std::string get_code_max() const {
        size_t strategy_pos = content_.find("\"strategy\"");
        if (strategy_pos == std::string::npos) return "";

        size_t key_pos = content_.find("\"code_max\"", strategy_pos);
        if (key_pos == std::string::npos) return "";

        size_t start = content_.find("\"", key_pos + 10);
        if (start == std::string::npos) return "";
        size_t end = content_.find("\"", start + 1);
        if (end == std::string::npos) return "";
        return content_.substr(start + 1, end - start - 1);
    }

    /// @brief 模块开关：sell
    int get_module_sell(int default_val = 0) const {
        size_t modules_pos = content_.find("\"modules\"");
        if (modules_pos == std::string::npos) return default_val;
        size_t key_pos = content_.find("\"sell\"", modules_pos);
        if (key_pos == std::string::npos) return default_val;
        return extract_int("sell");
    }

    /// @brief 模块开关：base_cancel
    int get_module_base_cancel(int default_val = 0) const {
        size_t modules_pos = content_.find("\"modules\"");
        if (modules_pos == std::string::npos) return default_val;
        size_t key_pos = content_.find("\"base_cancel\"", modules_pos);
        if (key_pos == std::string::npos) return default_val;
        return extract_int("base_cancel");
    }

    /// @brief 模块开关：usage_example
    int get_module_usage_example(int default_val = 0) const {
        size_t modules_pos = content_.find("\"modules\"");
        if (modules_pos == std::string::npos) return default_val;
        size_t key_pos = content_.find("\"usage_example\"", modules_pos);
        if (key_pos == std::string::npos) return default_val;
        return extract_int("usage_example");
    }

    /// @brief modules_config.usage_example.csv_path（目录路径）
    std::string get_usage_example_csv_dir() const {
        size_t root_pos = content_.find("\"modules_config\"");
        if (root_pos == std::string::npos) return "";

        size_t usage_pos = content_.find("\"usage_example\"", root_pos);
        if (usage_pos == std::string::npos) return "";

        size_t key_pos = content_.find("\"csv_path\"", usage_pos);
        if (key_pos == std::string::npos) return "";

        size_t start = content_.find("\"", key_pos + 10);
        if (start == std::string::npos) return "";
        size_t end = content_.find("\"", start + 1);
        if (end == std::string::npos) return "";
        return content_.substr(start + 1, end - start - 1);
    }

    /// @brief modules_config.base_cancel.order_dir
    std::string get_base_cancel_order_dir() const {
        size_t root_pos = content_.find("\"modules_config\"");
        if (root_pos == std::string::npos) return "";

        size_t base_pos = content_.find("\"base_cancel\"", root_pos);
        if (base_pos == std::string::npos) return "";

        size_t key_pos = content_.find("\"order_dir\"", base_pos);
        if (key_pos == std::string::npos) return "";

        size_t start = content_.find("\"", key_pos + 11);
        if (start == std::string::npos) return "";
        size_t end = content_.find("\"", start + 1);
        if (end == std::string::npos) return "";
        return content_.substr(start + 1, end - start - 1);
    }
};
