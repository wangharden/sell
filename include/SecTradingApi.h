#pragma once

#include "ITradingApi.h"
#include "Order.h"
#include "MarketData.h"
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <memory>
#include <vector>

// 前向声明 SEC ITPDK 结构
// 注意：ITPDK_ZQGL 在 SDK 中已定义，这里不需要前向声明
struct stStructMsg;
struct stStructOrderFuncMsg;
struct ITPDK_CusReqInfo;

/// @brief 订单状态常量定义（避免字符串不一致）
namespace OrderStatus {
    constexpr const char* SUBMITTED = "submitted";   // 已提交
    constexpr const char* ACCEPTED = "accepted";     // 已确认
    constexpr const char* PARTIAL = "partial_filled"; // 部分成交
    constexpr const char* FILLED = "filled";         // 全部成交
    constexpr const char* CANCELING = "canceling";   // 撤单中
    constexpr const char* CANCELLED = "cancelled";   // 已撤单
    constexpr const char* REJECTED = "rejected";     // 已拒绝/废单
}

/// @brief SEC 交易接口实现
/// 封装华泰证券 SECITPDK 交易 API
class SecTradingApi : public ITradingApi {
public:
    using OrderEventCallback = std::function<void(const OrderResult&, int)>;

    SecTradingApi();
    virtual ~SecTradingApi();

    // ITradingApi 接口实现
    bool connect(const std::string& host, int port,
                const std::string& user, 
                const std::string& password) override;
    
    void disconnect() override;
    
    bool is_connected() const override;
    
    std::string place_order(const OrderRequest& req) override;
    
    bool cancel_order(const std::string& order_id) override;
    
    std::vector<Position> query_positions() override;
    
    std::vector<OrderResult> query_orders() override;

    /// @brief 查询单个订单状态
    /// @param order_id 订单ID
    /// @return OrderResult 对象，如果未找到则返回空OrderResult
    OrderResult query_order(const std::string& order_id);

    /// @brief 等待订单完成（成交或撤单）
    /// @param order_id 订单ID
    /// @param timeout_ms 超时时间（毫秒），0表示不等待
    /// @return 订单最终状态
    OrderResult wait_order(const std::string& order_id, int timeout_ms = 0);

    /// @brief 设置 dry-run 模式（测试模式）
    /// @param enable true=测试模式（用跌停价买入后立即撤单，不会实际成交），false=正常模式
    void set_dry_run(bool enable);

    /// @brief 获取当前是否为 dry-run 模式
    bool is_dry_run() const { return dry_run_mode_; }

    /// @brief 设置订单回调（委托/成交/撤单/废单）
    void set_order_callback(OrderEventCallback callback);

private:
    // 内部订单跟踪结构
    struct Order {
        std::string order_id;
        std::string symbol;
        int64_t volume = 0;
        double price = 0.0;
        std::string status;  // "submitted", "accepted", "partial_filled", "filled", "canceled", "rejected"
        int64_t filled_volume = 0;
        double filled_price = 0.0;
        double last_fill_price = 0.0;
        std::string remark;  // ✅ 添加备注字段
        int side = -1;       // 0=Buy, 1=Sell
        int order_type = -1;
        int entrust_type = -1;
    };

    // SEC ITPDK 回调函数（静态）
    static void OnStructMsgCallback(const char* pTime, stStructMsg& stMsg, int nType);
    static void OnOrderAsyncCallback(const char* pTime, stStructOrderFuncMsg& stMsg, int nType);
    
    // 实例方法处理回调
    void handle_struct_msg(const char* pTime, stStructMsg& stMsg, int nType);
    void handle_order_async(const char* pTime, stStructOrderFuncMsg& stMsg, int nType);
    
    // 内部辅助方法
    std::string generate_order_id();
    void update_order_status(int64_t order_id, const std::string& status, 
                            const std::string& info = "");
    
    // 连接参数
    std::string config_section_;    // 配置段名称
    std::string account_id_;        // 客户号
    std::string password_;          // 密码
    std::string token_;             // 登录token，用于回调查找实例
    bool is_connected_;
    
    // 订单管理
    std::map<std::string, Order> orders_;      // local_id -> 订单状态
    std::map<int64_t, std::string> sysid_to_local_; // sys_id -> local_id
    std::map<int64_t, std::string> kfsbdbh_to_local_; // kfsbdbh -> local_id (异步下单)
    std::mutex orders_mutex_;              // 订单状态互斥锁
    
    // 持仓缓存
    std::vector<Position> positions_cache_;
    std::mutex positions_mutex_;
    
    // 股东号缓存
    std::string sh_account_;        // 上海股东号
    std::string sz_account_;        // 深圳股东号
    
    // Dry-run 模式标志
    bool dry_run_mode_;             // true=测试模式，false=正常模式
    
    // 订单ID生成器
    int64_t order_id_counter_;
    std::mutex id_mutex_;

    OrderEventCallback order_callback_;
    std::mutex callback_mutex_;
    
    // 实例映射（用于回调）
    static std::map<std::string, SecTradingApi*> instances_; // token做key
    static std::map<std::string, SecTradingApi*> instances_by_account_; // account_id做key
    static std::mutex instances_mutex_;
};
