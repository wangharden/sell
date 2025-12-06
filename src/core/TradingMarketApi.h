#pragma once
#include "ITradingApi.h"
#include "IMarketDataApi.h"
#include <memory>
#include <vector>
#include <utility>

/// @brief 交易和行情组合API（适配现有策略，保持向后兼容）
/// 
/// 这个类通过组合模式将交易API和行情API整合在一起，
/// 使得现有策略代码无需修改即可继续使用。
/// 
/// 使用示例：
/// @code
/// auto trading_api = std::make_shared<ItpdkTradingApi>();
/// auto market_api = std::make_shared<TdfMarketDataApi>();
/// auto combined_api = std::make_shared<TradingMarketApi>(trading_api, market_api);
/// 
/// // 现在可以同时使用交易和行情功能
/// combined_api->place_order(order_req);
/// auto snapshot = combined_api->get_snapshot("600000.SH");
/// @endcode
class TradingMarketApi : public ITradingApi {
private:
    std::shared_ptr<ITradingApi> trading_api_;
    std::shared_ptr<IMarketDataApi> market_data_api_;

public:
    /// @brief 构造函数
    /// @param trading_api 交易API实例（如 ItpdkTradingApi）
    /// @param market_data_api 行情API实例（如 TdfMarketDataApi）
    TradingMarketApi(std::shared_ptr<ITradingApi> trading_api, 
                     std::shared_ptr<IMarketDataApi> market_data_api)
        : trading_api_(trading_api), market_data_api_(market_data_api) {}
    
    virtual ~TradingMarketApi() = default;

    // ========== 交易API方法（转发到 trading_api_）==========
    
    /// @brief 连接交易服务
    virtual bool connect(const std::string& host, int port,
                        const std::string& user, const std::string& password) override {
        return trading_api_->connect(host, port, user, password);
    }
    
    /// @brief 断开交易连接
    virtual void disconnect() override {
        trading_api_->disconnect();
    }
    
    /// @brief 交易是否已连接
    virtual bool is_connected() const override {
        return trading_api_->is_connected();
    }
    
    /// @brief 下单
    virtual std::string place_order(const OrderRequest& req) override {
        return trading_api_->place_order(req);
    }
    
    /// @brief 撤单
    virtual bool cancel_order(const std::string& order_id) override {
        return trading_api_->cancel_order(order_id);
    }
    
    /// @brief 查询持仓
    virtual std::vector<Position> query_positions() override {
        return trading_api_->query_positions();
    }
    
    /// @brief 查询订单
    virtual std::vector<OrderResult> query_orders() override {
        return trading_api_->query_orders();
    }

    // ========== 行情API方法（转发到 market_data_api_）==========
    
    /// @brief 获取行情快照
    /// @param symbol 股票代码
    /// @return 行情快照
    MarketSnapshot get_snapshot(const std::string& symbol) {
        return market_data_api_->get_snapshot(symbol);
    }
    
    /// @brief 获取涨跌停价
    /// @param symbol 股票代码
    /// @return 涨停价、跌停价
    std::pair<double, double> get_limits(const std::string& symbol) {
        return market_data_api_->get_limits(symbol);
    }
    
    /// @brief 获取集合竞价数据
    /// @param symbol 股票代码
    /// @param date 日期 YYYYMMDD格式
    /// @param end_time 截止时间 "HHMMSS" 格式，如 "092700"
    /// @return 开盘价、集合竞价成交金额
    std::pair<double, double> get_auction_data(
        const std::string& symbol,
        const std::string& date,
        const std::string& end_time
    ) {
        return market_data_api_->get_auction_data(symbol, date, end_time);
    }
    
    /// @brief 获取历史tick数据
    /// @param symbol 股票代码
    /// @param start_time 开始时间 "HHMMSS"
    /// @param end_time 结束时间 "HHMMSS"
    /// @return tick数据列表
    std::vector<MarketSnapshot> get_history_ticks(
        const std::string& symbol, 
        const std::string& start_time,
        const std::string& end_time = ""
    ) {
        return market_data_api_->get_history_ticks(symbol, start_time, end_time);
    }

    // ========== 行情连接管理 ==========
    
    /// @brief 连接行情服务
    /// @param host 服务器地址
    /// @param port 端口
    /// @param user 用户名（可选）
    /// @param password 密码（可选）
    /// @return 是否成功
    bool connect_market(const std::string& host, int port,
                       const std::string& user = "", 
                       const std::string& password = "") {
        return market_data_api_->connect(host, port, user, password);
    }
    
    /// @brief 断开行情连接
    void disconnect_market() {
        market_data_api_->disconnect();
    }
    
    /// @brief 行情是否已连接
    bool is_market_connected() const {
        return market_data_api_->is_connected();
    }
    
    // ========== 访问器方法 ==========
    
    /// @brief 获取交易API实例
    std::shared_ptr<ITradingApi> get_trading_api() {
        return trading_api_;
    }
    
    /// @brief 获取行情API实例
    std::shared_ptr<IMarketDataApi> get_market_api() {
        return market_data_api_;
    }
};

using TradingMarketApiPtr = std::shared_ptr<TradingMarketApi>;
