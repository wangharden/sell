#include "Qh2hSellModule.h"

#include "../core/util.h"
#include "SecTradingApi.h"
#include "ImprovedLogger.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <thread>

namespace {
constexpr const char* kStrategyName = "qh2h_sell";
constexpr int NOTIFY_PUSH_MATCH = 2;
}

Qh2hSellModule::Qh2hSellModule(std::string account_id,
                               int hold_vol,
                               std::string code_min,
                               std::string code_max)
    : account_id_(std::move(account_id)),
      hold_vol_(hold_vol),
      code_min_(std::move(code_min)),
      code_max_(std::move(code_max)) {}

bool Qh2hSellModule::init(AppContext& ctx) {
    logger_ = std::make_shared<ImprovedLogger>("qh2h_sell", "./log", LogLevel::INFO);
    logger_->info("========== qh2h_sell module init ==========");

    if (!ctx.trading || !ctx.market || !ctx.trading_raw) {
        logger_->error("[INIT] missing trading/market api in context");
        return false;
    }

    if (account_id_.empty()) {
        logger_->warn("[INIT] account_id is empty; continuing (SDK uses login account)");
    }

    auto positions = ctx.trading->query_positions();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pos_map_ = build_position_map(positions);
        symbols_ = build_symbol_list(positions);

        states_.clear();
        for (const auto& symbol : symbols_) {
            states_[symbol] = StockState();
        }

        zt_cache_.clear();
        dt_cache_.clear();
        pair_buy_orders_.clear();
        sell_orders_.clear();

        before_init_ = false;
        transform_flag_ = false;
        last_pos_refresh_ = std::chrono::steady_clock::now() - std::chrono::seconds(5);

        active_ = !symbols_.empty();
    }

    if (!active_) {
        logger_->warn("[INIT] no symbols available for sell; module will stay idle");
    } else {
        logger_->info_f("[INIT] loaded %zu symbols", symbols_.size());
    }

    return true;
}

void Qh2hSellModule::tick(AppContext& ctx) {
    if (!active_ || ctx.stop.load()) {
        return;
    }

    int now = current_hhmmss();

    // Before-init refresh during trading day window.
    if (!before_init_ && time_in_range(now, 91000, 150000)) {
        refresh_positions(ctx);
        before_init_ = true;
    }

    // Transform refresh right after continuous auction starts.
    if (!transform_flag_ && time_in_range(now, 93500, 93510)) {
        auto refreshed = ctx.trading->query_positions();
        std::lock_guard<std::mutex> lock(mutex_);
        pos_map_ = build_position_map(refreshed);
        symbols_ = build_symbol_list(refreshed);
        states_.clear();
        for (const auto& symbol : symbols_) {
            states_[symbol] = StockState();
        }
        zt_cache_.clear();
        dt_cache_.clear();
        transform_flag_ = true;
    }

    if (!(time_in_range(now, 92515, 93500) || time_in_range(now, 93500, 145650))) {
        return;
    }

    auto steady_now = std::chrono::steady_clock::now();
    if (steady_now - last_pos_refresh_ > std::chrono::seconds(1)) {
        auto refreshed = ctx.trading->query_positions();
        std::lock_guard<std::mutex> lock(mutex_);
        pos_map_ = build_position_map(refreshed);
        last_pos_refresh_ = steady_now;
    }

    bool use_post_rules = now >= 93500;
    std::vector<std::string> local_symbols;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        local_symbols = symbols_;
    }

    for (const auto& symbol : local_symbols) {
        StockState state;
        Position pos;
        double zt = 0.0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto st_it = states_.find(symbol);
            if (st_it == states_.end()) {
                continue;
            }
            state = st_it->second;
            auto pos_it = pos_map_.find(symbol);
            if (pos_it != pos_map_.end()) {
                pos = pos_it->second;
            }
            auto zt_it = zt_cache_.find(symbol);
            if (zt_it != zt_cache_.end()) {
                zt = zt_it->second;
            }
        }

        if (state.zhaban == 0) {
            MarketSnapshot snap;
            {
                std::lock_guard<std::mutex> lock(ctx.market_mutex);
                snap = ctx.market->get_snapshot(symbol);
            }
            if (!snap.valid) {
                continue;
            }

            double buy_price1 = round_price(snap.bid_price1);
            int64_t buy_vol1 = snap.bid_volume1;

            if (zt <= 0.0) {
                zt = resolve_zt_price(ctx, symbol);
                if (zt > 0.0) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    zt_cache_[symbol] = zt;
                }
            }
            if (zt <= 0.0) {
                continue;
            }

            if (buy_price1 == zt && buy_vol1 > 0 && state.fengban == 0) {
                logger_->info("[FB] " + symbol + " is FB! sleeping 1s...");
                std::this_thread::sleep_for(std::chrono::seconds(1));

                double buy_price = use_post_rules ? round_price(zt - 0.01) : zt;
                OrderRequest req;
                req.account_id = account_id_;
                req.symbol = symbol;
                req.side = OrderSide::Buy;
                req.price = buy_price;
                req.volume = 100;
                req.is_market = false;
                req.remark = std::string(kStrategyName) + "_pair_buy_" + symbol;

                std::string order_id = ctx.trading->place_order(req);
                if (!order_id.empty()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    states_[symbol].fengban = 1;
                    pair_buy_orders_[order_id] = symbol;
                }
            } else if (state.fengban == 1 && (buy_price1 != zt || buy_vol1 <= 1000)) {
                int64_t vol = calc_sell_volume(pos, hold_vol_);
                int64_t split_vol = (vol / 100 / 2) * 100;
                if (split_vol <= 0) {
                    continue;
                }

                double sell_price = resolve_sell_price(ctx, symbol);
                if (sell_price <= 0.0) {
                    continue;
                }

                std::vector<std::string> new_orders;
                for (int i = 0; i < 2; ++i) {
                    OrderRequest req;
                    req.account_id = account_id_;
                    req.symbol = symbol;
                    req.side = OrderSide::Sell;
                    req.price = sell_price;
                    req.volume = split_vol;
                    req.is_market = true;
                    req.remark = std::string(kStrategyName) + "_zb_sell_" + symbol;
                    std::string order_id = ctx.trading->place_order(req);
                    if (!order_id.empty()) {
                        new_orders.push_back(order_id);
                    }
                }

                if (!new_orders.empty()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    states_[symbol].zhaban = 1;
                    auto& orders = sell_orders_[symbol];
                    orders.insert(new_orders.begin(), new_orders.end());
                }
            }
        } else if (state.zhaban == 1 && state.sold_out != 1) {
            if (pos.available > hold_vol_) {
                int64_t vol = calc_sell_volume(pos, hold_vol_);
                int64_t split_vol = (vol / 100 / 2) * 100;
                if (split_vol <= 0) {
                    continue;
                }

                double sell_price = resolve_sell_price(ctx, symbol);
                if (sell_price <= 0.0) {
                    continue;
                }

                std::vector<std::string> new_orders;
                for (int i = 0; i < 2; ++i) {
                    OrderRequest req;
                    req.account_id = account_id_;
                    req.symbol = symbol;
                    req.side = OrderSide::Sell;
                    req.price = sell_price;
                    req.volume = split_vol;
                    req.is_market = true;
                    req.remark = std::string(kStrategyName) + "_zb_sell_" + symbol;
                    std::string order_id = ctx.trading->place_order(req);
                    if (!order_id.empty()) {
                        new_orders.push_back(order_id);
                    }
                }
                if (!new_orders.empty()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto& orders = sell_orders_[symbol];
                    orders.insert(new_orders.begin(), new_orders.end());
                }
            } else {
                std::vector<std::string> to_cancel;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = sell_orders_.find(symbol);
                    if (it != sell_orders_.end()) {
                        to_cancel.assign(it->second.begin(), it->second.end());
                    }
                }
                for (const auto& order_id : to_cancel) {
                    OrderResult ord = ctx.trading_raw->query_order(order_id);
                    if (!ord.success) {
                        continue;
                    }
                    if (ord.status == OrderResult::Status::FILLED ||
                        ord.status == OrderResult::Status::CANCELLED ||
                        ord.status == OrderResult::Status::REJECTED) {
                        continue;
                    }
                    ctx.trading->cancel_order(order_id);
                }

                auto refreshed = ctx.trading->query_positions();
                Position updated;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    pos_map_ = build_position_map(refreshed);
                    auto pos_it = pos_map_.find(symbol);
                    if (pos_it != pos_map_.end()) {
                        updated = pos_it->second;
                    }
                }

                if (updated.available > hold_vol_) {
                    int64_t vol = calc_sell_volume(updated, hold_vol_);
                    int64_t split_vol = (vol / 100 / 2) * 100;
                    if (split_vol <= 0) {
                        continue;
                    }

                    double sell_price = resolve_sell_price(ctx, symbol);
                    if (sell_price <= 0.0) {
                        continue;
                    }

                    std::vector<std::string> new_orders;
                    for (int i = 0; i < 2; ++i) {
                        OrderRequest req;
                        req.account_id = account_id_;
                        req.symbol = symbol;
                        req.side = OrderSide::Sell;
                        req.price = sell_price;
                        req.volume = split_vol;
                        req.is_market = true;
                        req.remark = std::string(kStrategyName) + "_zb_sell_" + symbol;
                        std::string order_id = ctx.trading->place_order(req);
                        if (!order_id.empty()) {
                            new_orders.push_back(order_id);
                        }
                    }
                    if (!new_orders.empty()) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        auto& orders = sell_orders_[symbol];
                        orders.insert(new_orders.begin(), new_orders.end());
                    }
                } else {
                    std::lock_guard<std::mutex> lock(mutex_);
                    states_[symbol].sold_out = 1;
                }
            }
        }
    }
}

void Qh2hSellModule::on_order_event(AppContext& ctx, const OrderResult& result, int notify_type) {
    if (!active_ || ctx.stop.load()) {
        return;
    }
    if (notify_type != NOTIFY_PUSH_MATCH) {
        return;
    }

    std::string symbol;
    double zt = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pair_buy_orders_.find(result.order_id);
        if (it == pair_buy_orders_.end()) {
            return;
        }
        symbol = it->second;
        auto st_it = states_.find(symbol);
        if (st_it != states_.end() && st_it->second.zhaban == 1) {
            return;
        }
        auto zt_it = zt_cache_.find(symbol);
        if (zt_it != zt_cache_.end()) {
            zt = zt_it->second;
        }
    }
    if (symbol.empty()) {
        return;
    }

    if (zt <= 0.0) {
        zt = resolve_zt_price(ctx, symbol);
        if (zt <= 0.0) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        zt_cache_[symbol] = zt;
    }

    double match_price = result.last_fill_price > 0.0 ? result.last_fill_price : result.filled_price;
    if (match_price <= 0.0) {
        return;
    }
    match_price = round_price(match_price);
    double zt_limit = round_price(zt);

    int now = current_hhmmss();
    bool ok = false;
    if (now < 93500) {
        ok = std::fabs(match_price - zt_limit) < 0.001;
    } else {
        ok = match_price <= round_price(zt_limit - 0.01) + 1e-6;
    }
    if (!ok) {
        return;
    }

    Position pos;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pos_map_.find(symbol);
        if (it != pos_map_.end()) {
            pos = it->second;
        }
    }
    int64_t vol = calc_sell_volume(pos, hold_vol_);
    int64_t split_vol = (vol / 100 / 10) * 100;
    if (split_vol <= 0) {
        return;
    }

    double sell_price = resolve_sell_price(ctx, symbol);
    if (sell_price <= 0.0) {
        return;
    }

    std::vector<std::string> new_orders;
    for (int i = 0; i < 10; ++i) {
        OrderRequest req;
        req.account_id = account_id_;
        req.symbol = symbol;
        req.side = OrderSide::Sell;
        req.price = sell_price;
        req.volume = split_vol;
        req.is_market = true;
        req.remark = std::string(kStrategyName) + "_zb_sell_" + symbol;
        std::string order_id = ctx.trading->place_order(req);
        if (!order_id.empty()) {
            new_orders.push_back(order_id);
        }
    }

    if (!new_orders.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        states_[symbol].zhaban = 1;
        auto& orders = sell_orders_[symbol];
        orders.insert(new_orders.begin(), new_orders.end());
    }
}

int Qh2hSellModule::current_hhmmss() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now_c);
#else
    localtime_r(&now_c, &local_tm);
#endif
    return local_tm.tm_hour * 10000 + local_tm.tm_min * 100 + local_tm.tm_sec;
}

bool Qh2hSellModule::time_in_range(int now, int start, int end) {
    return now >= start && now < end;
}

std::string Qh2hSellModule::extract_code_from_symbol(const std::string& symbol) {
    size_t dot = symbol.find('.');
    if (dot == std::string::npos) {
        return symbol;
    }
    return symbol.substr(0, dot);
}

std::string Qh2hSellModule::to_symbol(const std::string& code) {
    if (code.size() != 6) {
        return "";
    }
    if (code.rfind("00", 0) == 0 || code.rfind("30", 0) == 0) {
        return code + ".SZ";
    }
    if (code.rfind("60", 0) == 0 || code.rfind("68", 0) == 0) {
        return code + ".SH";
    }
    return "";
}

double Qh2hSellModule::round_price(double value) {
    return std::round(value * 100.0) / 100.0;
}

bool Qh2hSellModule::pass_code_filter(const std::string& code,
                                      const std::string& min_code,
                                      const std::string& max_code) {
    if (!min_code.empty() && code <= min_code) {
        return false;
    }
    if (!max_code.empty() && code >= max_code) {
        return false;
    }
    return true;
}

int64_t Qh2hSellModule::calc_sell_volume(const Position& pos, int hold_vol) {
    int64_t surplus = pos.total - hold_vol;
    if (surplus <= 0) {
        return 0;
    }
    int64_t vol = std::min(pos.available, surplus);
    return to_lot(vol, 100);
}

void Qh2hSellModule::refresh_positions(AppContext& ctx) {
    auto refreshed = ctx.trading->query_positions();
    std::lock_guard<std::mutex> lock(mutex_);
    pos_map_ = build_position_map(refreshed);
    symbols_ = build_symbol_list(refreshed);
    states_.clear();
    for (const auto& symbol : symbols_) {
        states_[symbol] = StockState();
    }
}

std::vector<std::string> Qh2hSellModule::build_symbol_list(const std::vector<Position>& positions) const {
    std::vector<std::string> symbols;
    for (const auto& pos : positions) {
        std::string code = extract_code_from_symbol(pos.symbol);
        if (!pass_code_filter(code, code_min_, code_max_)) {
            continue;
        }
        if (pos.available > hold_vol_) {
            symbols.push_back(pos.symbol);
        }
    }
    std::sort(symbols.begin(), symbols.end());
    symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
    return symbols;
}

std::unordered_map<std::string, Position> Qh2hSellModule::build_position_map(const std::vector<Position>& positions) const {
    std::unordered_map<std::string, Position> map;
    for (const auto& pos : positions) {
        std::string code = extract_code_from_symbol(pos.symbol);
        if (!pass_code_filter(code, code_min_, code_max_)) {
            continue;
        }
        map[pos.symbol] = pos;
    }
    return map;
}

double Qh2hSellModule::resolve_sell_price(AppContext& ctx, const std::string& symbol) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = dt_cache_.find(symbol);
        if (it != dt_cache_.end()) {
            return it->second;
        }
    }
    std::pair<double, double> limits;
    {
        std::lock_guard<std::mutex> lock(ctx.market_mutex);
        limits = ctx.market->get_limits(symbol);
    }
    double dt = round_price(limits.second);
    if (dt > 0.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        dt_cache_[symbol] = dt;
    }
    return dt;
}

double Qh2hSellModule::resolve_zt_price(AppContext& ctx, const std::string& symbol) {
    std::pair<double, double> limits;
    {
        std::lock_guard<std::mutex> lock(ctx.market_mutex);
        limits = ctx.market->get_limits(symbol);
    }
    return round_price(limits.first);
}
