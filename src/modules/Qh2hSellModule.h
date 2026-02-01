#pragma once

#include "IModule.h"
#include "../core/MarketData.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ImprovedLogger;

class Qh2hSellModule final : public IModule {
public:
    Qh2hSellModule(std::string account_id,
                   int hold_vol,
                   std::string code_min,
                   std::string code_max);

    const char* name() const override { return "qh2h_sell"; }
    std::chrono::milliseconds tick_interval() const override { return std::chrono::milliseconds(100); }

    bool init(AppContext& ctx) override;
    void tick(AppContext& ctx) override;
    void on_order_event(AppContext& ctx, const OrderResult& result, int notify_type) override;

private:
    struct StockState {
        int fengban = 0;
        int zhaban = 0;
        int sold_out = 0;
    };

    static int current_hhmmss();
    static bool time_in_range(int now, int start, int end);
    static std::string extract_code_from_symbol(const std::string& symbol);
    static std::string to_symbol(const std::string& code);
    static double round_price(double value);
    static bool pass_code_filter(const std::string& code,
                                 const std::string& min_code,
                                 const std::string& max_code);
    static int64_t calc_sell_volume(const Position& pos, int hold_vol);

    void refresh_positions(AppContext& ctx);
    std::vector<std::string> build_symbol_list(const std::vector<Position>& positions) const;
    std::unordered_map<std::string, Position> build_position_map(const std::vector<Position>& positions) const;
    double resolve_sell_price(AppContext& ctx, const std::string& symbol);
    double resolve_zt_price(AppContext& ctx, const std::string& symbol);

    std::string account_id_;
    int hold_vol_ = 300;
    std::string code_min_;
    std::string code_max_;

    std::shared_ptr<ImprovedLogger> logger_;
    bool active_ = false;

    mutable std::mutex mutex_;
    bool before_init_ = false;
    bool transform_flag_ = false;
    std::chrono::steady_clock::time_point last_pos_refresh_{};

    std::vector<std::string> symbols_;
    std::unordered_map<std::string, StockState> states_;
    std::unordered_map<std::string, Position> pos_map_;

    std::unordered_map<std::string, double> zt_cache_;
    std::unordered_map<std::string, double> dt_cache_;

    std::unordered_map<std::string, std::string> pair_buy_orders_; // order_id -> symbol
    std::unordered_map<std::string, std::unordered_set<std::string>> sell_orders_; // symbol -> order_ids
};
