#pragma once
#include <string>
#include <cstdint>

/// @brief 市场行情快照（扩展版，包含五档盘口）
struct MarketSnapshot {
    std::string symbol;
    int timestamp = 0;           // 时间戳 (HHMMSSmmm格式)
    
    // 基础价格
    double last_price = 0.0;     // 最新价
    double pre_close = 0.0;      // 昨收价
    double open = 0.0;           // 开盘价
    double high = 0.0;           // 最高价
    double low = 0.0;            // 最低价
    double up_limit = 0.0;       // 涨停价（别名）
    double down_limit = 0.0;     // 跌停价（别名）
    double high_limit = 0.0;     // 涨停价
    double low_limit = 0.0;      // 跌停价
    
    // 成交量和成交额
    int64_t volume = 0;          // 成交量
    int64_t turnover = 0;        // 成交额
    
    // 五档买盘
    double bid_price1 = 0.0;
    double bid_price2 = 0.0;
    double bid_price3 = 0.0;
    double bid_price4 = 0.0;
    double bid_price5 = 0.0;
    
    int64_t bid_volume1 = 0;
    int64_t bid_volume2 = 0;
    int64_t bid_volume3 = 0;
    int64_t bid_volume4 = 0;
    int64_t bid_volume5 = 0;
    
    // 五档卖盘
    double ask_price1 = 0.0;
    double ask_price2 = 0.0;
    double ask_price3 = 0.0;
    double ask_price4 = 0.0;
    double ask_price5 = 0.0;
    
    int64_t ask_volume1 = 0;
    int64_t ask_volume2 = 0;
    int64_t ask_volume3 = 0;
    int64_t ask_volume4 = 0;
    int64_t ask_volume5 = 0;
    
    bool valid = false;
};

/// @brief 持仓信息（用于可用量校验）
struct Position {
    std::string symbol;
    int64_t total = 0;           // 总持仓
    int64_t available = 0;       // 可用量（已解冻）
    int64_t frozen = 0;          // 冻结量（挂单中）
};
