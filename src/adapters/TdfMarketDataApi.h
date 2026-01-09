#pragma once
#include "IMarketDataApi.h"  // 假设你有这个接口
#include <map>
#include <vector>
#include <mutex>
#include <string>
#include <utility>
#include <functional>

// 前向声明 TDF API 类型（从 PDF）
typedef void* THANDLE;
struct TDF_OPEN_SETTING_EXT;
struct TDF_MSG;
struct TDF_MARKET_DATA;
struct TDF_TRANSACTION;

// 逐笔成交数据结构（用于回调）
struct TransactionData {
    std::string symbol;     // 股票代码 (如 600000.SH)
    int timestamp;          // 时间 HHMMSSmmm
    double price;           // 成交价格
    int volume;             // 成交量
    double turnover;        // 成交金额
    int bsf_flag;           // 买卖方向标志 (0未知, 1买, 2卖)
    char function_code;     // 成交类别 (如 '0'成交, 'C'撤单)
};

// Tick 数据结构（用于集合竞价）
struct TickData {
    int timestamp;     // 时间 HHMMSSmmm (nTime)
    double open;       // 逐笔成交价 (nPrice / 10000.0)
    long long amount;  // 单笔成交金额 (nTurnover)
};

// 逐笔成交回调类型
using TransactionCallback = std::function<void(const TransactionData&)>;

/// @brief TDF行情API适配器
class TdfMarketDataApi : public IMarketDataApi {
private:
    THANDLE tdf_handle_;
    bool is_connected_;
    std::string host_;
    int port_;
    std::string user_;
    std::string password_;
    std::string subscription_list_;  //  保存订阅列表，避免c_str()指针失效
    std::string csv_path_;           // CSV 配置文件路径
    
    // 逐笔成交回调
    TransactionCallback transaction_callback_;
    
    // 缓存
    std::map<std::string, MarketSnapshot> snapshot_cache_;
    std::map<std::string, std::vector<TickData>> tick_cache_;
    bool auction_tick_logged_ = false;
    int continuous_tick_logged_ = 0;
    mutable std::mutex cache_mutex_;
    
    // 回调（静态）
    static void OnDataReceived(THANDLE hTdf, TDF_MSG* pMsgHead);
    static void OnSystemMessage(THANDLE hTdf, TDF_MSG* pSysMsg);
    
    // 实例处理
    void HandleMarketData(TDF_MSG* pMsgHead);
    void HandleTransactionData(TDF_MSG* pMsgHead);  // 新加：处理逐笔
    void HandleSystemMessage(TDF_MSG* pSysMsg);
    
    // 辅助：从 CSV 加载订阅列表
    std::string GenerateSubscriptionList(const std::string& csv_path);
    
public:
    TdfMarketDataApi();
    virtual ~TdfMarketDataApi();
    
    /// @brief 设置 CSV 配置文件路径（在 connect 之前调用）
    void set_csv_path(const std::string& csv_path) { csv_path_ = csv_path; }
    
    /// @brief 设置逐笔成交回调（在 connect 之前调用）
    /// @param callback 每收到一笔成交数据时调用的函数
    void set_transaction_callback(TransactionCallback callback) { 
        transaction_callback_ = std::move(callback); 
    }
    
    bool connect(const std::string& host, int port,
                 const std::string& user = "", 
                 const std::string& password = "") override;
    
    void disconnect() override;
    
    bool is_connected() const override;
    
    MarketSnapshot get_snapshot(const std::string& symbol) override;
    
    std::pair<double, double> get_limits(const std::string& symbol) override;
    
    std::pair<double, double> get_auction_data(
        const std::string& symbol,
        const std::string& date,
        const std::string& end_time
    ) override;
    
    std::vector<MarketSnapshot> get_history_ticks(
        const std::string& symbol,
        const std::string& start_time,
        const std::string& end_time = ""
    ) override;
    
    bool subscribe(const std::vector<std::string>& symbols);
    bool unsubscribe(const std::vector<std::string>& symbols);
};
