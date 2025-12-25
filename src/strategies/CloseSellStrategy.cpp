#include "CloseSellStrategy.h"
#include "../core/util.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <cmath>
#include <algorithm>

CloseSellStrategy::CloseSellStrategy(
    TradingMarketApi* api,
    const std::string& account_id
) : api_(api), 
    account_id_(account_id),
    uniform_dist_(0.0, 1.0),
    normal_dist_(0.0, 1.0) {
    std::random_device rd;
    rng_.seed(rd());
}

bool CloseSellStrategy::init() {
    std::cout << "=== Initializing CloseSellStrategy ===" << std::endl;
    
    // 查询持仓
    auto positions = api_->query_positions();
    std::cout << "Current positions: " << positions.size() << std::endl;
    
    for (const auto& pos : positions) {
        // txt line 231-237: 只处理持仓大于hold_vol的股票
        if (pos.total > hold_vol_) {
            total_volumes_[pos.symbol] = pos.total;
            sold_volumes_[pos.symbol] = 0;
            remarks_[pos.symbol] = "empty";
            callbacks_[pos.symbol] = 0;
            std::cout << "  " << pos.symbol << ": total=" << pos.total 
                      << ", avail=" << pos.available << std::endl;
        }
    }
    
    std::cout << "Strategy initialized successfully" << std::endl;
    return true;
}

void CloseSellStrategy::on_timer() {
    int now = get_current_time();
    
    // Phase 1: 随机卖出 (14:53:00-14:56:45) - txt line 47-143
    if (now >= 145300 && now < 145645) {
        phase1_random_sell();
    }
    
    // Phase 2: 撤单 (14:56:45-14:57:00) - txt line 145-158
    if (now >= 145645 && now < 145700 && phase2_cancel_done_ == 0) {
        phase2_cancel_orders();
        phase2_cancel_done_ = 1;
    }
    
    // Phase 3: 测试卖出 (14:57:20-14:57:50) - txt line 160-194
    if (now >= 145720 && now < 145750 && phase3_test_sell_done_ == 0) {
        phase3_test_sell();
        phase3_test_sell_done_ = 1;
    }
    
    // Phase 4: 大量卖出 (14:58:00-14:59:50) - txt line 196-228
    if (now >= 145800 && now < 145950 && phase4_bulk_sell_done_ == 0) {
        phase4_bulk_sell();
        phase4_bulk_sell_done_ = 1;
    }
}

void CloseSellStrategy::phase1_random_sell() {
    // txt line 47-143: 随机卖出阶段
    // 每3秒触发，15%概率卖出，中间价
    
    auto positions = api_->query_positions();
    // 用当前持仓校正实际已卖数量，避免累计委托量虚高
    for (const auto& pos : positions) {
        auto it = total_volumes_.find(pos.symbol);
        if (it != total_volumes_.end()) {
            int64_t init_total = it->second;
            int64_t actual_sold = std::max<int64_t>(0, init_total - pos.total);
            sold_volumes_[pos.symbol] = actual_sold;
        }
    }
    
    for (const auto& pos : positions) {
        const std::string& symbol = pos.symbol;
        
        // 只处理记录中的股票（持仓>hold_vol）
        if (total_volumes_.find(symbol) == total_volumes_.end()) {
            continue;
        }
        
        // txt line 50-52: 15%概率触发
        double p = uniform_dist_(rng_);
        if (p >= trigger_probability_) {
            continue;
        }
        
        // txt line 55-57: 检查70%卖出限制
        if (sold_volumes_[symbol] > total_volumes_[symbol] * 0.7) {
            continue;
        }
        
        // txt line 62-65: 计算可卖数量
        int64_t available_vol = pos.available;
        int64_t holding_vol = pos.total;
        
        if (available_vol <= 0) {
            continue;
        }
        
        if (holding_vol <= hold_vol_) {
            continue;
        }
        
        int64_t remaining = std::min(available_vol, holding_vol) - hold_vol_ - sold_volumes_[symbol];
        if (remaining <= 0) {
            continue;
        }
        
        int64_t vol = std::min<int64_t>(available_vol - hold_vol_, remaining);
        
        // 获取行情 - txt line 68-84
        MarketSnapshot snap = api_->get_snapshot(symbol);
        if (!snap.valid) {
            continue;
        }
        
        double buy_price1 = snap.bid_price1;
        double sell_price1 = snap.ask_price1;
        
        // 检查涨停价 - txt line 85-90
        auto limits = api_->get_limits(symbol);
        double zt_price = limits.first;
        
        if (!(zt_price > 0)) {
            continue;
        }
        
        if (std::abs(buy_price1 - zt_price) < 0.01) {
            std::cout << "  " << symbol << " is ZT, skip." << std::endl;
            continue;
        }
        
        // 计算中间价（向下取整）- txt line 92
        double sell_price = ceil_round((buy_price1 + sell_price1) / 2.0 - 1e-6, 2);
        if (!(buy_price1 > 0)) {
            buy_price1 = sell_price;
        }
        
        // 随机数量计算 - txt line 95-98
        if (single_amt_ < buy_price1 * vol) {
            double U = uniform_dist_(rng_);
            double N = normal_dist_(rng_);
            double temp_amt = single_amt_ - rand_amt1_ / 2.0 
                            + rand_amt1_ * U 
                            + N * rand_amt2_;
            int64_t temp_vol = static_cast<int64_t>(temp_amt / buy_price1 / 100) * 100;
            vol = std::min(vol, temp_vol);
        }
        
        if (vol <= 0) {
            continue;
        }
        
        std::cout << "  [Phase1] " << symbol << " sell " << vol 
                  << " @ " << sell_price << " (buy1=" << buy_price1 
                  << ", sell1=" << sell_price1 << ")" << std::endl;
        
        // 下单 - txt line 100-101
        OrderRequest req;
        req.account_id = account_id_;
        req.symbol = symbol;
        req.price = sell_price;
        req.volume = vol;
        req.is_market = false;
        req.remark = "收盘卖出" + symbol;
        
        std::string order_id = api_->place_order(req);
        
        if (!order_id.empty()) {
            remarks_[symbol] = req.remark;
            auto& ids = order_ids_[symbol];
            if (std::find(ids.begin(), ids.end(), order_id) == ids.end()) {
                ids.push_back(order_id);
            }
            std::cout << "    Order placed: " << order_id << std::endl;
        }
    }
}

void CloseSellStrategy::phase2_cancel_orders() {
    // txt line 145-158: 撤单未成交订单
    std::cout << "=== Phase 2: Canceling orders ===" << std::endl;
    
    int cancel_count = 0;
    
    // 检查是否所有股票都已处理回调
    int total_callback = 0;
    for (const auto& pair : callbacks_) {
        total_callback += pair.second;
    }
    
    if (total_callback == static_cast<int>(callbacks_.size())) {
        std::cout << "All callbacks processed, skip." << std::endl;
        return;
    }
    
    // 查询订单列表
    auto orders = api_->query_orders();
    std::map<std::string, OrderResult::Status> status_by_id;
    for (const auto& order : orders) {
        status_by_id[order.order_id] = order.status;
    }
    
    std::cout << "[Phase2] orders_from_api=" << orders.size()
              << ", tracked_symbols=" << order_ids_.size() << std::endl;
    
    // 遍历所有记录的股票
    for (auto& pair : remarks_) {
        const std::string& symbol = pair.first;
        const std::string& remark = pair.second;
        
        int cancel_try = 0;
        
        // 优先按本地记录的 order_id 撤单
        auto ids_it = order_ids_.find(symbol);
        if (ids_it != order_ids_.end()) {
            for (const auto& order_id : ids_it->second) {
                auto st_it = status_by_id.find(order_id);
                if (st_it == status_by_id.end()) {
                    std::cout << "  [Phase2] order_id not found: " << symbol 
                              << " " << order_id << std::endl;
                    continue;
                }
                
                auto status = st_it->second;
                if (status == OrderResult::Status::FILLED ||
                    status == OrderResult::Status::CANCELLED ||
                    status == OrderResult::Status::REJECTED) {
                    continue;
                }
                
                cancel_try++;
                if (api_->cancel_order(order_id)) {
                    cancel_count++;
                    std::cout << "  Cancelled: " << symbol 
                              << ", order_id=" << order_id << std::endl;
                }
            }
        }
        
        // 兜底：按 remark 匹配
        if (cancel_try == 0) {
            for (const auto& order : orders) {
                if (order.remark == remark) {
                    if (order.status != OrderResult::Status::FILLED &&
                        order.status != OrderResult::Status::CANCELLED &&
                        order.status != OrderResult::Status::REJECTED) {
                        if (api_->cancel_order(order.order_id)) {
                            cancel_count++;
                            std::cout << "  Cancelled: " << symbol 
                                      << ", order_id=" << order.order_id << std::endl;
                        }
                    }
                }
            }
        }
        
        // 标记已处理回调
        callbacks_[symbol] = 1;
    }
    
    std::cout << "Total cancelled: " << cancel_count << " orders" << std::endl;
}

void CloseSellStrategy::phase3_test_sell() {
    // txt line 160-194: 测试卖出，每只股票固定卖出100股，挂跌停价
    std::cout << "=== Phase 3: Test sell (100 shares each) ===" << std::endl;
    
    auto positions = api_->query_positions();
    // 校正已卖数量
    for (const auto& pos : positions) {
        auto it = total_volumes_.find(pos.symbol);
        if (it != total_volumes_.end()) {
            int64_t init_total = it->second;
            int64_t actual_sold = std::max<int64_t>(0, init_total - pos.total);
            sold_volumes_[pos.symbol] = actual_sold;
        }
    }
    
    for (const auto& pos : positions) {
        const std::string& symbol = pos.symbol;
        
        // 只处理记录中的股票
        if (total_volumes_.find(symbol) == total_volumes_.end()) {
            continue;
        }
        
        // txt line 165-167: 检查可用仓位
        if (pos.available <= 0) {
            continue;
        }
        
        int64_t available_vol = pos.available;
        int64_t holding_vol = pos.total;
        
        if (holding_vol <= hold_vol_) {
            continue;
        }
        
        if (available_vol < 100) {
            continue;
        }
        
        // 固定卖出100股
        int64_t remaining = std::min(available_vol, holding_vol) - hold_vol_ - sold_volumes_[symbol];
        if (remaining < 100) {
            continue;
        }
        
        int64_t vol = 100;
        
        // 获取行情 - txt line 172-183
        MarketSnapshot snap = api_->get_snapshot(symbol);
        if (!snap.valid) {
            continue;
        }
        
        double buy_price1 = snap.bid_price1;
        
        // 获取涨跌停价
        auto limits = api_->get_limits(symbol);
        double zt_price = limits.first;
        double dt_price = limits.second;
        
        if (!(zt_price > 0)) {
            continue;
        }
        
        // 涨停不卖
        if (std::abs(buy_price1 - zt_price) < 0.01) {
            std::cout << "  " << symbol << " is ZT, skip." << std::endl;
            continue;
        }
        
        // 使用跌停价卖出 - txt line 187
        double sell_price = dt_price;
        if (!(buy_price1 > 0)) {
            buy_price1 = sell_price;
        }
        
        std::cout << "  [Phase3-Test] " << symbol << " sell " << vol 
                  << " @ " << sell_price << " (dt_price)" << std::endl;
        
        // 下单
        OrderRequest req;
        req.account_id = account_id_;
        req.symbol = symbol;
        req.price = sell_price;
        req.volume = vol;
        req.is_market = false;
        req.remark = "收盘卖出" + symbol;
        
        std::string order_id = api_->place_order(req);
        
        if (!order_id.empty()) {
            auto& ids = order_ids_[symbol];
            if (std::find(ids.begin(), ids.end(), order_id) == ids.end()) {
                ids.push_back(order_id);
            }
            std::cout << "    Order placed: " << order_id << std::endl;
        }
    }
}

void CloseSellStrategy::phase4_bulk_sell() {
    // txt line 196-228: 大量卖出，卖出剩余仓位（扣除底仓和100股），挂跌停价
    std::cout << "=== Phase 4: Bulk sell (remaining positions) ===" << std::endl;
    
    auto positions = api_->query_positions();
    // 校正已卖数量
    for (const auto& pos : positions) {
        auto it = total_volumes_.find(pos.symbol);
        if (it != total_volumes_.end()) {
            int64_t init_total = it->second;
            int64_t actual_sold = std::max<int64_t>(0, init_total - pos.total);
            sold_volumes_[pos.symbol] = actual_sold;
        }
    }
    
    for (const auto& pos : positions) {
        const std::string& symbol = pos.symbol;
        
        // 只处理记录中的股票
        if (total_volumes_.find(symbol) == total_volumes_.end()) {
            continue;
        }
        
        // txt line 201-202: 检查可用仓位
        if (pos.available <= 0) {
            continue;
        }
        
        int64_t available_vol = pos.available;
        int64_t holding_vol = pos.total;
        
        if (holding_vol <= hold_vol_) {
            continue;
        }
        
        // 可卖数量基于当前持仓/可用计算，避免“已卖量”重复扣减
        int64_t sellable = std::max<int64_t>(0, std::min(available_vol, holding_vol) - hold_vol_);
        if (sellable <= 0) {
            continue;
        }
        
        // txt line 204-210: 计算卖出数量
        int64_t vol = sellable;
        
        if (vol <= 0) {
            continue;
        }
        
        // 获取行情 - txt line 212-223
        MarketSnapshot snap = api_->get_snapshot(symbol);
        if (!snap.valid) {
            continue;
        }
        
        double buy_price1 = snap.bid_price1;
        
        // 获取涨跌停价
        auto limits = api_->get_limits(symbol);
        double zt_price = limits.first;
        double dt_price = limits.second;
        
        if (!(zt_price > 0) || !(buy_price1 > 0)) {
            continue;
        }
        
        // 涨停不卖
        if (std::abs(buy_price1 - zt_price) < 0.01) {
            std::cout << "  " << symbol << " is ZT, skip." << std::endl;
            continue;
        }
        
        // 使用跌停价卖出 - txt line 224
        double sell_price = dt_price;
        if (!(buy_price1 > 0)) {
            buy_price1 = sell_price;
        }
        
        std::cout << "  [Phase4-Bulk] " << symbol << " sell " << vol 
                  << " @ " << sell_price << " (dt_price)" << std::endl;
        
        // 下单
        OrderRequest req;
        req.account_id = account_id_;
        req.symbol = symbol;
        req.price = sell_price;
        req.volume = vol;
        req.is_market = false;
        req.remark = "收盘卖出" + symbol;
        
        std::string order_id = api_->place_order(req);
        
        if (!order_id.empty()) {
            auto& ids = order_ids_[symbol];
            if (std::find(ids.begin(), ids.end(), order_id) == ids.end()) {
                ids.push_back(order_id);
            }
            std::cout << "    Order placed: " << order_id << std::endl;
        }
    }
}

int CloseSellStrategy::get_current_time() const {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;
#ifdef _WIN32
    localtime_s(&now_tm, &now_time_t);
#else
    localtime_r(&now_time_t, &now_tm);
#endif
    
    return now_tm.tm_hour * 10000 + now_tm.tm_min * 100 + now_tm.tm_sec;
}

void CloseSellStrategy::print_status() const {
    std::cout << "\n=== Close Strategy Status ===" << std::endl;
    std::cout << "Total stocks: " << total_volumes_.size() << std::endl;
    
    int64_t total_sold = 0;
    for (const auto& pair : sold_volumes_) {
        total_sold += pair.second;
        double sold_ratio = (total_volumes_.at(pair.first) > 0) 
            ? static_cast<double>(pair.second) / total_volumes_.at(pair.first) 
            : 0.0;
        std::cout << "  " << pair.first << ": sold=" << pair.second 
                  << "/" << total_volumes_.at(pair.first) 
                  << " (" << (sold_ratio * 100) << "%)" << std::endl;
    }
    
    std::cout << "Total sold volume: " << total_sold << std::endl;
}
