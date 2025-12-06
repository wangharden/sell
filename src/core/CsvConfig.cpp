#include "CsvConfig.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

bool CsvConfig::load_from_file(const std::string& csv_path) {
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open CSV file: " << csv_path << std::endl;
        return false;
    }
    
    stocks_.clear();
    
    std::string line;
    bool is_header = true;
    
    while (std::getline(file, line)) {
        // 跳过空行
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        
        // 跳过表头
        if (is_header) {
            is_header = false;
            continue;
        }
        
        auto fields = parse_line(line);
        if (fields.size() < 13) {
            std::cerr << "Invalid CSV line (expected >= 13 fields): " << line << std::endl;
            continue;
        }
        
        StockParams params;
        
        // 解析字段（索引从0开始，跳过第一列序号）
        // 格式：0序号,1SHORTNAME,2SYMBOL,3TRADINGDATE,4avail_vol,5total_vol,
        //       6limit_time,7lock_time,8break_time,9high,10close,11FB_FLAG,12ZB_FLAG,13SECOND_FLAG
        
        try {
            params.shortname = fields[1];
            params.symbol = normalize_symbol(fields[2]);
            params.trading_date = fields[3];
            params.avail_vol = std::stoll(fields[4]);
            params.total_vol = std::stoll(fields[5]);
            // fields[6-9] 是limit_time, lock_time, break_time, high (暂不使用)
            // field[10] 是close，即昨收价
            params.pre_close = (fields.size() > 10) ? std::stod(fields[10]) : 0.0;
            params.fb_flag = (fields.size() > 11) ? std::stoi(fields[11]) : 0;
            params.zb_flag = (fields.size() > 12) ? std::stoi(fields[12]) : 0;
            params.second_flag = (fields.size() > 13) ? std::stoi(fields[13]) : 0;
            
            // 初始化运行时状态
            params.sell_flag = 0;
            params.sold_vol = 0;
            params.jjamt = 0.0;
            params.open_price = 0.0;
            params.call_back = 0;
            
            stocks_[params.symbol] = params;
            
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
