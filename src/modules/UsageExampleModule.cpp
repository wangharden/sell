#include "UsageExampleModule.h"

#include "../core/TradingMarketApi.h"
#include "ImprovedLogger.h"
#include "../strategies/IntradaySellStrategy.h"
#include "../strategies/AuctionSellStrategy.h"
#include "../strategies/CloseSellStrategy.h"

#include <fstream>

UsageExampleModule::UsageExampleModule(std::string csv_path,
                                       std::string account_id,
                                       double sell_to_mkt_ratio,
                                       double phase1_sell_ratio,
                                       double input_amt,
                                       int64_t hold_vol)
    : csv_path_(std::move(csv_path)),
      account_id_(std::move(account_id)),
      sell_to_mkt_ratio_(sell_to_mkt_ratio),
      phase1_sell_ratio_(phase1_sell_ratio),
      input_amt_(input_amt),
      hold_vol_(hold_vol) {}

bool UsageExampleModule::init(AppContext& ctx) {
    logger_ = std::make_shared<ImprovedLogger>("usage_example", "./log", LogLevel::INFO);
    logger_->info("========== usage_example module init ==========");

    if (!ctx.trading || !ctx.market) {
        logger_->error("[INIT] missing trading/market api in context");
        return false;
    }

    if (csv_path_.empty()) {
        logger_->error("[INIT] csv_path is empty");
        return false;
    }
    std::ifstream csv(csv_path_.c_str());
    if (!csv.good()) {
        logger_->error("[INIT] csv not found: " + csv_path_);
        return false;
    }

    combined_api_ = std::make_shared<TradingMarketApi>(ctx.trading, ctx.market);

    intraday_.reset(new IntradaySellStrategy(combined_api_.get(), csv_path_, account_id_, hold_vol_, input_amt_));
    auction_.reset(new AuctionSellStrategy(combined_api_.get(), csv_path_, account_id_,
                                           sell_to_mkt_ratio_, phase1_sell_ratio_, hold_vol_));
    close_.reset(new CloseSellStrategy(combined_api_.get(), account_id_, hold_vol_));

    if (!intraday_->init()) {
        logger_->error("[INIT] intraday strategy init failed");
        return false;
    }
    if (!auction_->init()) {
        logger_->error("[INIT] auction strategy init failed");
        return false;
    }
    if (!close_->init()) {
        logger_->error("[INIT] close strategy init failed");
        return false;
    }

    logger_->info("[INIT] all strategies initialized");
    return true;
}

void UsageExampleModule::tick(AppContext& ctx) {
    (void)ctx;
    intraday_->on_timer();
    auction_->on_timer();
    close_->on_timer();
}

void UsageExampleModule::on_order_event(AppContext& ctx, const OrderResult& result, int notify_type) {
    (void)ctx;
    (void)result;
    (void)notify_type;
}
