#pragma once
#include <string>
#include <cstdint>

/// @brief 委托请求结构体（对接交易 API 的输入）
struct OrderRequest {
    std::string account_id;
    std::string symbol;     // e.g. "000001.SZ"
    double price = 0.0;     // limit price; ignored if is_market==true
    int64_t volume = 0;     // >=0
    bool is_market = false;
    std::string remark;     // 用于撤单与回溯跟踪
};

/// @brief 委托结果（下单后返回）
struct OrderResult {
    bool success = false;
    std::string order_id;   // 交易所/API 返回的订单编号
    std::string err_msg;    // 错误信息
    std::string symbol;     // 股票代码
    int64_t volume = 0;     // 委托数量
    int64_t filled_volume = 0; // 已成交数量（实时/回报更新）
    double price = 0.0;     // 委托价格
    std::string remark;     // 订单备注（用于撤单与回溯跟踪）
    
    // 订单状态
    enum class Status {
        UNKNOWN = 0,
        SUBMITTED = 1,   // 已提交
        PARTIAL = 2,     // 部分成交
        FILLED = 3,      // 全部成交
        CANCELLED = 4,   // 已撤销
        REJECTED = 5     // 已拒绝
    };
    Status status = Status::UNKNOWN;
};
