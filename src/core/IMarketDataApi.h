#pragma once
#include "MarketData.h"
#include <string>
#include <vector>
#include <utility>

/// @brief 行情数据接口
class IMarketDataApi {
public:
    virtual ~IMarketDataApi() = default;
    
    /// @brief 连接行情服务
    /// @param host 服务器地址
    /// @param port 端口
    /// @param user 用户名（可选）
    /// @param password 密码（可选）
    /// @return 是否成功
    virtual bool connect(const std::string& host, int port,
                        const std::string& user = "", 
                        const std::string& password = "") = 0;
    
    /// @brief 断开连接
    virtual void disconnect() = 0;
    
    /// @brief 是否已连接
    virtual bool is_connected() const = 0;
    
    /// @brief 获取行情快照
    /// @param symbol 股票代码
    /// @return 行情快照
    virtual MarketSnapshot get_snapshot(const std::string& symbol) = 0;
    
    /// @brief 获取涨跌停价
    /// @param symbol 股票代码
    /// @return 涨停价、跌停价
    virtual std::pair<double, double> get_limits(const std::string& symbol) = 0;
    
    /// @brief 获取集合竞价数据
    /// @param symbol 股票代码
    /// @param date 日期 YYYYMMDD格式
    /// @param end_time 截止时间 "HHMMSS" 或 "HHMMSSmmm" 格式，如 "092700" 或 "092700000"
    /// @return 开盘价、集合竞价成交金额
    virtual std::pair<double, double> get_auction_data(
        const std::string& symbol,
        const std::string& date,
        const std::string& end_time
    ) = 0;
    
    /// @brief 获取历史tick数据（可选扩展）
    /// @param symbol 股票代码
    /// @param start_time 开始时间 "HHMMSS"
    /// @param end_time 结束时间 "HHMMSS"
    /// @return tick数据列表
    virtual std::vector<MarketSnapshot> get_history_ticks(
        const std::string& symbol, 
        const std::string& start_time,
        const std::string& end_time = ""
    ) = 0;
};
