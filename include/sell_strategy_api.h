#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// ==================== 导出宏定义 ====================
#ifdef _WIN32
    #ifdef SELL_STRATEGY_EXPORTS
        #define SELL_API __declspec(dllexport)
    #else
        #define SELL_API __declspec(dllimport)
    #endif
#else
    #define SELL_API __attribute__((visibility("default")))
#endif

namespace SellStrategy {

// ==================== 数据结构 ====================

/// @brief 订单请求
struct OrderRequest {
    std::string account_id;  // 账号
    std::string symbol;      // 股票代码 (e.g., "600000.SH")
    int64_t volume;          // 数量
    double price;            // 价格
    bool is_market;          // 是否市价单
    std::string remark;      // 备注
};

/// @brief 持仓信息
struct Position {
    std::string symbol;      // 股票代码
    int64_t total;           // 总持仓
    int64_t available;       // 可用数量
    int64_t frozen;          // 冻结数量
};

/// @brief 订单结果
struct OrderResult {
    bool success;            // 是否成功
    std::string order_id;    // 订单号
    std::string symbol;      // 股票代码
    int64_t volume;          // 委托数量
    int64_t filled_volume;   // 成交数量
    double price;            // 委托价格
    
    enum class Status {
        Pending,           // 待成交
        PartiallyFilled,   // 部分成交
        Filled,            // 全部成交
        Cancelled,         // 已撤销
        Rejected           // 已拒绝
    };
    Status status;
};

/// @brief 市场快照
struct MarketSnapshot {
    std::string symbol;
    double last_price;
    double pre_close;
    
    // 五档买盘
    double bid_price1, bid_price2, bid_price3, bid_price4, bid_price5;
    int64_t bid_volume1, bid_volume2, bid_volume3, bid_volume4, bid_volume5;
    
    // 五档卖盘
    double ask_price1, ask_price2, ask_price3, ask_price4, ask_price5;
    int64_t ask_volume1, ask_volume2, ask_volume3, ask_volume4, ask_volume5;
    
    double up_limit;      // 涨停价
    double down_limit;    // 跌停价
    bool valid;           // 数据是否有效
};

// ==================== 策略引擎接口 ====================

/// @brief 策略引擎（单例模式）
class SELL_API StrategyEngine {
public:
    /// @brief 获取单例
    static StrategyEngine& getInstance();
    
    /// @brief 初始化引擎
    /// @param tdf_host TDF行情服务器地址
    /// @param tdf_port TDF端口
    /// @param tdf_user TDF用户名
    /// @param tdf_password TDF密码
    /// @param trade_config_key 交易配置key（itpdk配置文件中的key）
    /// @param trade_account 交易账号
    /// @param trade_password 交易密码
    /// @return 是否成功
    bool initialize(
        const std::string& tdf_host,
        int tdf_port,
        const std::string& tdf_user,
        const std::string& tdf_password,
        const std::string& trade_config_key,
        const std::string& trade_account,
        const std::string& trade_password
    );
    
    /// @brief 加载CSV配置文件
    /// @param csv_path CSV文件路径
    /// @return 是否成功
    bool loadConfig(const std::string& csv_path);
    
    /// @brief 启动策略（开始定时器循环）
    /// @param strategy_type 策略类型："intraday"盘中/"auction"竞价/"close"收盘
    /// @return 是否成功
    bool startStrategy(const std::string& strategy_type);
    
    /// @brief 停止策略
    void stopStrategy();
    
    /// @brief 手动触发一次策略执行（用于测试）
    void trigger();
    
    /// @brief 获取当前持仓
    std::vector<Position> getPositions();
    
    /// @brief 获取当前订单
    std::vector<OrderResult> getOrders();
    
    /// @brief 获取行情快照
    /// @param symbol 股票代码
    MarketSnapshot getSnapshot(const std::string& symbol);
    
    /// @brief 订阅股票行情
    /// @param symbols 股票代码列表
    bool subscribe(const std::vector<std::string>& symbols);
    
    /// @brief 关闭引擎
    void shutdown();
    
    /// @brief 设置日志级别
    /// @param level "DEBUG"/"INFO"/"WARN"/"ERROR"
    void setLogLevel(const std::string& level);
    
    /// @brief 获取最后错误信息
    std::string getLastError() const;

private:
    StrategyEngine();
    ~StrategyEngine();
    StrategyEngine(const StrategyEngine&) = delete;
    StrategyEngine& operator=(const StrategyEngine&) = delete;
    
    class Impl;
    std::unique_ptr<Impl> pImpl_;  // PIMPL模式隐藏实现细节
};

// ==================== 工厂函数（简化使用）====================

/// @brief 快速启动盘中卖出策略
/// @param csv_path CSV配置文件
/// @param account_id 账号
/// @return 是否成功
SELL_API bool quickStartIntradayStrategy(
    const std::string& csv_path,
    const std::string& account_id
);

/// @brief 快速启动竞价卖出策略
SELL_API bool quickStartAuctionStrategy(
    const std::string& csv_path,
    const std::string& account_id
);

/// @brief 快速启动收盘卖出策略
SELL_API bool quickStartCloseStrategy(
    const std::string& csv_path,
    const std::string& account_id
);

} // namespace SellStrategy
