#pragma once
#include "ITradingApi.h"
#include "TdfApiAdapter.h"
#include "ItpdkApiAdapter.h"
#include <memory>
#include <vector>

/// @brief 组合适配器：行情用TDF，交易用itpdk
/// 
/// 使用场景：
/// - 行情数据从TDFAPI获取（高频、准确）
/// - 交易功能通过itpdk执行（下单、撤单、查询）
/// 
/// 示例：
/// ```cpp
/// auto tdf = std::make_shared<TdfApiAdapter>();
/// auto itpdk = std::make_shared<ItpdkApiAdapter>();
/// auto composite = std::make_shared<CompositeAdapter>(tdf, itpdk);
/// 
/// // 连接行情
/// tdf->connect("58.210.86.54", 10001, "test", "test");
/// 
/// // 连接交易
/// itpdk->connect("trade_server", 0, "083200004967", "password");
/// 
/// // 使用组合适配器
/// auto snapshot = composite->get_snapshot("600000.SH");  // 从TDF获取
/// auto order_id = composite->place_order(req);           // 通过itpdk下单
/// ```
class CompositeAdapter : public ITradingApi {
public:
    CompositeAdapter(std::shared_ptr<TdfApiAdapter> market_api,
                     std::shared_ptr<ItpdkApiAdapter> trading_api)
        : market_api_(market_api), trading_api_(trading_api) {}
    
    // ========== 连接管理（需要分别连接两个API）==========
    
    /// @brief 连接交易API（itpdk）
    /// @param host 配置key
    /// @param port 未使用
    /// @param user 客户号
    /// @param password 交易密码
    bool connect(const std::string& host, int port,
                const std::string& user, const std::string& password) override {
        return trading_api_->connect(host, port, user, password);
    }
    
    /// @brief 连接行情API（TDF）
    /// @param host 行情服务器IP
    /// @param port 行情端口
    /// @param user 行情账号
    /// @param password 行情密码
    bool connect_market(const std::string& host, int port,
                       const std::string& user, const std::string& password) {
        return market_api_->connect(host, port, user, password);
    }
    
    /// @brief 订阅股票代码（通过TDF）
    bool subscribe(const std::vector<std::string>& symbols) {
        return market_api_->subscribe(symbols);
    }
    
    void disconnect() override {
        if (market_api_) market_api_->disconnect();
        if (trading_api_) trading_api_->disconnect();
    }
    
    bool is_connected() const override {
        // 两个API都连接才返回true
        return market_api_->is_connected() && trading_api_->is_connected();
    }
    
    // ========== 行情接口（委托给TDF）==========
    
    MarketSnapshot get_snapshot(const std::string& symbol) override {
        return market_api_->get_snapshot(symbol);
    }
    
    std::pair<double, double> get_limits(const std::string& symbol) override {
        return market_api_->get_limits(symbol);
    }
    
    // ========== 交易接口（委托给itpdk）==========
    
    std::string place_order(const OrderRequest& req) override {
        return trading_api_->place_order(req);
    }
    
    bool cancel_order(const std::string& order_id) override {
        return trading_api_->cancel_order(order_id);
    }
    
    std::vector<Position> query_positions() override {
        return trading_api_->query_positions();
    }
    
    std::vector<OrderResult> query_orders() override {
        return trading_api_->query_orders();
    }
    
private:
    std::shared_ptr<TdfApiAdapter> market_api_;    // 行情API
    std::shared_ptr<ItpdkApiAdapter> trading_api_; // 交易API
};

using CompositeAdapterPtr = std::shared_ptr<CompositeAdapter>;
