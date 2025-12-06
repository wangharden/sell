#pragma once
#include "Order.h"
#include "MarketData.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

/// @brief 交易回调接口（对应 Python 的 XtQuantTraderCallback）
class ITradingCallback {
public:
    virtual ~ITradingCallback() = default;

    /// @brief 委托回报（对应 on_stock_order）
    virtual void on_order(const OrderResult& result) = 0;

    /// @brief 成交回报（对应 on_stock_trade）
    virtual void on_trade(const OrderResult& result) = 0;

    /// @brief 委托失败（对应 on_order_error）
    virtual void on_order_error(const std::string& order_id, 
                                 int error_code, 
                                 const std::string& error_msg) = 0;

    /// @brief 撤单失败（对应 on_cancel_error）
    virtual void on_cancel_error(const std::string& order_id,
                                  int error_code,
                                  const std::string& error_msg) = 0;

    /// @brief 连接断开
    virtual void on_disconnected() = 0;
};

/// @brief 交易 API 抽象接口
class ITradingApi {
public:
    virtual ~ITradingApi() = default;

    /// @brief 连接交易服务
    /// @param host 服务器地址
    /// @param port 端口
    /// @param user 用户名
    /// @param password 密码
    /// @return 是否成功
    virtual bool connect(const std::string& host, int port,
                        const std::string& user, const std::string& password) = 0;

    /// @brief 断开连接
    virtual void disconnect() = 0;
    
    /// @brief 是否已连接
    virtual bool is_connected() const = 0;

    /// @brief 下单（对应 passorder）
    /// @param req 委托请求
    /// @return 委托结果（包含 order_id）
    virtual std::string place_order(const OrderRequest& req) = 0;

    /// @brief 撤单（对应 cancel）
    /// @param order_id 订单系统 ID
    /// @return 是否成功
    virtual bool cancel_order(const std::string& order_id) = 0;

    /// @brief 查询持仓（对应 get_trade_detail_data POSITION）
    /// @return 持仓列表
    virtual std::vector<Position> query_positions() = 0;

    /// @brief 查询订单（对应 query_stock_orders）
    /// @return 订单列表
    virtual std::vector<OrderResult> query_orders() = 0;
};

using TradingApiPtr = std::shared_ptr<ITradingApi>;
