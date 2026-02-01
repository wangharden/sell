#pragma once

#include "IModule.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

class ImprovedLogger;
class TradingMarketApi;
class IntradaySellStrategy;
class AuctionSellStrategy;
class CloseSellStrategy;

class UsageExampleModule final : public IModule {
public:
    UsageExampleModule(std::string csv_path,
                       std::string account_id,
                       double sell_to_mkt_ratio,
                       double phase1_sell_ratio,
                       double input_amt,
                       int64_t hold_vol);

    const char* name() const override { return "usage_example"; }
    std::chrono::milliseconds tick_interval() const override { return std::chrono::seconds(1); }

    bool init(AppContext& ctx) override;
    void tick(AppContext& ctx) override;
    void on_order_event(AppContext& ctx, const OrderResult& result, int notify_type) override;

private:
    std::string csv_path_;
    std::string account_id_;
    double sell_to_mkt_ratio_ = 0.1;
    double phase1_sell_ratio_ = 0.1;
    double input_amt_ = 600000.0;
    int64_t hold_vol_ = 300;

    std::shared_ptr<ImprovedLogger> logger_;
    std::shared_ptr<TradingMarketApi> combined_api_;

    std::unique_ptr<IntradaySellStrategy> intraday_;
    std::unique_ptr<AuctionSellStrategy> auction_;
    std::unique_ptr<CloseSellStrategy> close_;
};
