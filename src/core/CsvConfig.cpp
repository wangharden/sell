#include "CsvConfig.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <map>

bool CsvConfig::load_from_file(const std::string& csv_path) {
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open CSV file: " << csv_path << std::endl;
        return false;
    }
    
    stocks_.clear();
    
    std::string line;
    bool is_header = true;
    std::map<std::string, size_t> header_index;
    bool warned_unknown = false;
    
    while (std::getline(file, line)) {
        // 跳过空行
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        
        // 处理表头：按列名解析，大小写不敏感
        if (is_header) {
            auto header_fields = parse_line(line);
            for (size_t i = 0; i < header_fields.size(); ++i) {
                std::string key = header_fields[i];
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                header_index[key] = i;
            }
            // 必需列校验
            const char* required[] = {"shortname", "symbol", "tradingdate", "avail_vol", "total_vol", "close", "fb_flag", "zb_flag", "second_flag"};
            for (auto* req : required) {
                if (header_index.find(req) == header_index.end()) {
                    std::cerr << "Missing required column: " << req << " in CSV " << csv_path << std::endl;
                    return false;
                }
            }
            is_header = false;
            continue;
        }
        
        auto fields = parse_line(line);
        
        StockParams params;
        
        try {
            auto get_field = [&](const std::string& name) -> std::string {
                std::string key = name;
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                auto it = header_index.find(key);
                if (it == header_index.end() || it->second >= fields.size()) {
                    return "";
                }
                return fields[it->second];
            };
            
            params.shortname = get_field("shortname");
            params.symbol = normalize_symbol(get_field("symbol"));
            params.trading_date = get_field("tradingdate");
            params.avail_vol = std::stoll(get_field("avail_vol"));
            params.total_vol = std::stoll(get_field("total_vol"));
            params.pre_close = get_field("close").empty() ? 0.0 : std::stod(get_field("close"));
            params.fb_flag = get_field("fb_flag").empty() ? 0 : std::stoi(get_field("fb_flag"));
            params.zb_flag = get_field("zb_flag").empty() ? 0 : std::stoi(get_field("zb_flag"));
            params.second_flag = get_field("second_flag").empty() ? 0 : std::stoi(get_field("second_flag"));
            
            // 初始化运行时状态
            params.sell_flag = 0;
            params.sold_vol = 0;
            params.jjamt = 0.0;
            params.open_price = 0.0;
            params.call_back = 0;
            
            stocks_[params.symbol] = params;
            
            // 记录未知列（只警告一次）
            if (!warned_unknown && header_index.size() > 0 && fields.size() > header_index.size()) {
                std::cerr << "Warning: extra columns detected, they will be ignored." << std::endl;
                warned_unknown = true;
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error parsing CSV line: " << line << ", error: " << e.what() << std::endl;
            continue;
        }
    }
    
    file.close();
    std::cout << "Loaded " << stocks_.size() << " stocks from " << csv_path << std::endl;
    return !stocks_.empty();
}

StockParams* CsvConfig::get_stock(const std::string& symbol) {
    auto it = stocks_.find(symbol);
    if (it != stocks_.end()) {
        return &it->second;
    }
    return nullptr;
}

const StockParams* CsvConfig::get_stock(const std::string& symbol) const {
    auto it = stocks_.find(symbol);
    if (it != stocks_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> CsvConfig::get_all_symbols() const {
    std::vector<std::string> symbols;
    symbols.reserve(stocks_.size());
    for (const auto& pair : stocks_) {
        symbols.push_back(pair.first);
    }
    return symbols;
}

std::vector<std::string> CsvConfig::parse_line(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    
    while (std::getline(ss, field, ',')) {
        // 去除首尾空格
        field.erase(0, field.find_first_not_of(" \t\r\n"));
        field.erase(field.find_last_not_of(" \t\r\n") + 1);
        fields.push_back(field);
    }
    
    return fields;
}

std::string CsvConfig::normalize_symbol(const std::string& raw_symbol) {
    // 如果已经有后缀，直接返回
    if (raw_symbol.find('.') != std::string::npos) {
        return raw_symbol;
    }
    
    // 根据前缀添加市场后缀
    if (raw_symbol.length() >= 2) {
        std::string prefix = raw_symbol.substr(0, 2);
        if (prefix == "00" || prefix == "30") {
            return raw_symbol + ".SZ";
        } else if (prefix == "60" || prefix == "68") {
            return raw_symbol + ".SH";
        }
    }
    
    // 未识别的代码，返回原值
    return raw_symbol;
}
