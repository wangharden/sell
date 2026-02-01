#include "IntradaySellStrategy.h"
#include "../core/util.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <iomanip>
#include <ctime>
#include <cmath>

IntradaySellStrategy::IntradaySellStrategy(
    TradingMarketApi* api,
    const std::string& csv_path,
    const std::string& account_id,
    int64_t hold_vol,
    double input_amt
) : api_(api),
    csv_path_(csv_path),
    account_id_(account_id),
    single_amt_(input_amt * 0.025),
    rand_amt1_(input_amt * 0.02),
    hold_vol_(hold_vol) {
}

bool IntradaySellStrategy::init() {
    std::cout << "=== Initializing IntradaySellStrategy ===" << std::endl;
    
    // 1. 加载CSV配置
    if (!csv_config_.load_from_file(csv_path_)) {
        std::cerr << "Failed to load CSV config from: " << csv_path_ << std::endl;
        return false;
    }
    
    // 2. 查询持仓并更新CSV中的avail_vol和total_vol
    auto positions = api_->query_positions();
    std::cout << "Current positions: " << positions.size() << std::endl;
    
    for (const auto& pos : positions) {
        auto* stock = csv_config_.get_stock(pos.symbol);
        if (stock) {
            stock->avail_vol = pos.available;
            stock->total_vol = pos.total;
            std::cout << "  " << pos.symbol << ": total=" << pos.total 
                      << ", avail=" << pos.available << std::endl;
        }
    }
    
    // 3. 获取涨跌停价（从API）
    // 注意：昨收价(pre_close)已经在CSV加载时从close字段读取了
    std::cout << "Fetching limits prices..." << std::endl;
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (!stock) continue;
        
        // 从API获取涨跌停价
        auto limits = api_->get_limits(symbol);
        if (limits.first > 0) {
            stock->zt_price = limits.first;   // 涨停价
            stock->dt_price = limits.second;  // 跌停价
        }
    }
    
    std::cout << "Loaded " << csv_config_.size() << " stocks from CSV" << std::endl;
    std::cout << "Strategy initialized successfully" << std::endl;
    
    return true;
}

void IntradaySellStrategy::on_timer() {
    int now = get_current_time();
    
    // Phase 1: 收集集合竞价数据 (09:26:00 - 11:28:10)
    if (now >= 92600 && now < 112810 && before_check_ == 0) {
        collect_auction_data();
        before_check_ = 1;
    }
    
    // Phase 2: 执行卖出 (09:30:03 - 14:48:55)
    if ((now >= 93003 && now < 113000) || (now >= 130000 && now < 144855)) {
        if (before_check_ == 1) {
            execute_sell();
        }
    }
    
    // Phase 3: 撤单 (14:49:00 - 14:51:00)
    if (now >= 144900 && now < 145100) {
        cancel_orders();
    }
}

void IntradaySellStrategy::collect_auction_data() {
    std::cout << "=== Collecting auction data ===" << std::endl;

    // 采样一次“竞价阶段结束后的可用仓位”，作为盘中 keep_position 的基准分母。
    // 注意：这里不要求包含 AuctionSellStrategy::after_open_sell() 的影响。
    if (!base_captured_) {
        auto positions = api_->query_positions();
        for (const auto& pos : positions) {
            // 只记录本策略关注的股票
            if (csv_config_.get_stock(pos.symbol)) {
                base_avail_after_auction_[pos.symbol] = pos.available;
            }
        }
        base_captured_ = true;
    }

    // txt line 132-143: 获取09:27前最后一条tick的amount和open字段
    // jjamt_data = ContextInfo.get_market_data_ex(fields=['open', 'volume', 'amount', ...], 
    //              stock_code=[key], period='tick', ...)[key]
    // single_jjamt = jjamt_data[(jjamt_data.index <= day + "092700")].iloc[-1]
    // df.loc[key, 'jjamt'] = single_jjamt['amount']
    // df.loc[key, 'open'] = single_jjamt['open']
    
    int date = get_current_date();
    std::string date_str = std::to_string(date);
    
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (!stock) continue;
        
        // 通过API获取09:15-09:27的集合竞价数据
        auto auction_data = api_->get_auction_data(symbol, date_str, "092700000");
        stock->open_price = auction_data.first;   // 开盘价
        stock->jjamt = auction_data.second;       // 集合竞价成交金额

        MarketSnapshot snap = api_->get_snapshot(symbol);
        if (snap.valid && snap.pre_close > 0.0) {
            stock->pre_close = snap.pre_close;
        }

        std::cout << "  " << symbol << ": jjamt=" << stock->jjamt 
                  << ", open=" << stock->open_price << std::endl;
    }
}

void IntradaySellStrategy::execute_sell() {
    int now = get_current_time();
    
    // 更新可用持仓
    auto positions = api_->query_positions();
    for (const auto& pos : positions) {
        auto* stock = csv_config_.get_stock(pos.symbol);
        if (stock) {
            stock->avail_vol = pos.available;
        }
    }
    
    // 遍历所有股票
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (!stock) continue;
        
        // 检查是否已完成卖出
        if (stock->sell_flag == 1) continue;
        if (stock->avail_vol < hold_vol_) {
            stock->sell_flag = 1;
            continue;
        }
        if (stock->total_vol < hold_vol_) {
            stock->sell_flag = 1;
            continue;
        }
        
        // 判断卖出条件类型
        std::string condition = determine_condition(*stock);
        if (condition.empty()) {
            continue;  // 不满足任何卖出条件
        }
        
        // 【新增日志】打印触发条件
        std::cout << "  " << symbol << ": 触发卖出条件 [" << condition << "] ";
        if (condition == "lb") std::cout << "(连板)";
        else if (condition == "fb") std::cout << "(封板未炸板)";
        else if (condition == "hf") std::cout << "(回封-封板后炸板)";
        else if (condition == "zb") std::cout << "(炸板)";
        std::cout << std::endl;
        
        // 读取开盘数据，获取策略
        double jjamt = stock->jjamt;
        double limit_up = stock->zt_price;
        if (limit_up <= 0.0) {
            MarketSnapshot snap = api_->get_snapshot(symbol);
            if (snap.valid && snap.high_limit > 0.0) {
                limit_up = snap.high_limit;
                stock->zt_price = snap.high_limit;
            }
        }
        double pre_close = 0.0;
        if (limit_up > 0.0) {
            pre_close = std::round((limit_up / 1.1 - 1e-6) * 100.0) / 100.0;
        }
        double open_ratio = (pre_close > 0.0) ? 
            (stock->open_price / pre_close) : 0.0;
        
        auto windows = sell_strategy_.get_windows(condition, jjamt, open_ratio);
        
        // 检查当前时间是否在卖出窗口内
        bool placed = false;
        for (const auto& window : windows) {
            if (now >= window.start_time && now < window.end_time) {
                // 50%概率随机跳过（txt line 165）
                double p = rng_.uni();
                if (p >= 0.16) {
                    std::cout << "  " << symbol << ": skip (random p=" << p << ")" << std::endl;
                    break;
                }
                
                std::cout << "  " << symbol << " (" << stock->shortname 
                          << "): condition=" << condition 
                          << ", time_window=" << window.start_time << "-" << window.end_time
                          << ", keep=" << window.keep_position << std::endl;
                
                sell_order(symbol, window.keep_position, now);
                placed = true;
                break;
            }
        }
        if (!placed) {
            std::cout << "  " << symbol << ": not in window at " << now 
                      << ", windows=";
            for (const auto& w : windows) {
                std::cout << "[" << w.start_time << "-" << w.end_time << "]";
            }
            std::cout << std::endl;
        }
    }
}

void IntradaySellStrategy::sell_order(
    const std::string& symbol,
    double keep_position,
    int current_time
) {
    auto* stock = csv_config_.get_stock(symbol);
    if (!stock) return;
    
    // txt line 197-198: 计算可卖数量
    int64_t holding_vol = std::max(stock->total_vol - hold_vol_, int64_t(0));
    int64_t available_vol = std::max(stock->avail_vol - hold_vol_, int64_t(0));
    int64_t vol = std::min(available_vol, holding_vol);
    
    if (vol == 0) {
        std::cout << "    " << symbol << ": vol=0 (avail=" << stock->avail_vol
                  << ", total=" << stock->total_vol << ")" << std::endl;
        stock->sell_flag = 1;
        return;
    }
    
    // txt line 199-207: 检查是否已完成目标卖出
    if (stock->sold_vol >= stock->total_vol) {
        stock->sell_flag = 1;
        std::cout << "    " << symbol << ": sold_vol=" << stock->sold_vol 
                  << ", total_vol=" << stock->total_vol 
                  << ", sold_ratio=" << (double)stock->sold_vol / stock->total_vol << std::endl;
        return;
    }
    
    // txt line 208-210: 检查保留仓位
    {
        const int64_t avail_for_ratio = std::max<int64_t>(0, stock->avail_vol - hold_vol_);
        auto it = base_avail_after_auction_.find(symbol);
        const int64_t base = (it != base_avail_after_auction_.end()) ? it->second : 0;
        const int64_t denom = (base > 0) ? base : stock->total_vol;  // 兜底仍使用 init() 的 total_vol

        if (denom > 0) {
            if (static_cast<double>(avail_for_ratio) / static_cast<double>(denom) <= keep_position) {
                const double sold_ratio = (stock->total_vol > 0)
                    ? (static_cast<double>(stock->sold_vol) / static_cast<double>(stock->total_vol))
                    : 0.0;
                std::cout << "    " << symbol << ": reach keep_position=" << keep_position 
                          << ", sold_ratio=" << sold_ratio << std::endl;
                return;
            }
        }
    }
    // keep_position 判断已在上方用 base_avail_after_auction_/total_vol 兜底处理
    
    // txt line 212-220: 获取实时orderbook
    MarketSnapshot snapshot = api_->get_snapshot(symbol);
    if (!snapshot.valid) {
        return;
    }
    
    double buy_price1 = snapshot.bid_price1;
    double sell_price1 = snapshot.ask_price1;
    
    // txt line 225-227: 涨停判断
    if (stock->zt_price > 0 && std::abs(buy_price1 - stock->zt_price) < 0.01) {
        // std::cout << "    " << symbol << " is at limit up, skip" << std::endl;
        return;
    }
    
    // txt line 228: 计算中间价
    double sell_price = ceil_round((buy_price1 + sell_price1) / 2.0 - 1e-6, 2);
    if (buy_price1 <= 0) {
        buy_price1 = sell_price;
    }
    
    // txt line 234-236: 随机数量计算
    if (single_amt_ < buy_price1 * vol) {
        double U = rng_.uni();
        double N = rng_.normal(0, 1);
        double temp_amt = single_amt_ - rand_amt1_ / 2.0 
                         + rand_amt1_ * U 
                         + N * rand_amt2_;
        int64_t temp_vol = static_cast<int64_t>(temp_amt / buy_price1 / 100) * 100;
        vol = std::min(vol, temp_vol);
    }
    
    if (vol <= 0) {
        return;
    }
    
    std::cout << "    buy1=" << buy_price1 << ", sell1=" << sell_price1 << std::endl;
    std::cout << "    sell " << symbol << " " << vol << " at price: " << sell_price << std::endl;
    
    // txt line 242: 下单
    OrderRequest req;
    req.account_id = account_id_;
    req.symbol = symbol;
    req.price = sell_price;
    req.volume = vol;
    req.is_market = false;
    req.remark = "盘中卖出" + symbol;
    
    std::string order_id = api_->place_order(req);
    
    if (!order_id.empty()) {
        stock->sold_vol += vol;
        stock->remark = "盘中卖出" + symbol;
        std::cout << "    ✓ Order placed: " << order_id << std::endl;
        
        // 【新增】查询订单状态（延迟后查询最新状态）
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto orders = api_->query_orders();
        for (const auto& order : orders) {
            if (order.order_id == order_id) {
                std::cout << "    订单状态: ";
                switch (order.status) {
                    case OrderResult::Status::SUBMITTED:
                        std::cout << "已提交"; break;
                    case OrderResult::Status::PARTIAL:
                        std::cout << "部分成交 (" << order.filled_volume << "/" << order.volume << ")"; break;
                    case OrderResult::Status::FILLED:
                        std::cout << "全部成交"; break;
                    case OrderResult::Status::CANCELLED:
                        std::cout << "已撤单"; break;
                    case OrderResult::Status::REJECTED:
                        std::cout << "已拒绝"; break;
                    default:
                        std::cout << "未知";
                }
                std::cout << std::endl;
                
                if (order.filled_volume > 0) {
                    double avg_price = (order.filled_volume > 0) ? 
                        (order.price * order.filled_volume) / order.filled_volume : 0.0;
                    std::cout << "    成交信息: 已成交 " << order.filled_volume 
                             << " 股，剩余 " << (order.volume - order.filled_volume) << " 股" << std::endl;
                }
                break;
            }
        }
    } else {
        std::cerr << "    ✗ Order failed!" << std::endl;
    }
}

void IntradaySellStrategy::cancel_orders() {
    int today = get_current_date();
    if (cancel_attempt_date_ != today) {
        cancel_attempt_date_ = today;
        cancel_attempts_ = 0;
    }
    if (cancel_attempts_ >= 3) {
        return;
    }
    ++cancel_attempts_;

    std::cout << "=== Canceling unfilled orders (attempt " 
              << cancel_attempts_ << "/3) ===" << std::endl;
    
    // txt line 176-190逻辑:
    // 1. 获取所有订单列表 get_trade_detail_data(MyaccountID, 'STOCK', 'order')
    // 2. 遍历订单，精确匹配remark字段
    // 3. 如果订单状态不是已成交(56)，则撤单
    
    auto orders = api_->query_orders();
    std::cout << "查询到 " << orders.size() << " 个订单" << std::endl;
    
    int cancel_count = 0;
    int checked_count = 0;
    
    // 遍历CSV中的所有股票
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (!stock) continue;
        
        // 匹配remark
        std::string expected_remark = "盘中卖出" + symbol;
        
        for (const auto& order : orders) {
            // txt: if order.m_strRemark == df.loc[key,'remark']
            // 精确匹配且状态不是已成交(FILLED)
            if (order.remark == expected_remark) {
                checked_count++;
                std::cout << "  检查订单: " << symbol << " order_id=" << order.order_id 
                         << " status=";
                switch (order.status) {
                    case OrderResult::Status::SUBMITTED: std::cout << "已提交"; break;
                    case OrderResult::Status::PARTIAL: std::cout << "部分成交"; break;
                    case OrderResult::Status::FILLED: std::cout << "全部成交"; break;
                    case OrderResult::Status::CANCELLED: std::cout << "已撤单"; break;
                    case OrderResult::Status::REJECTED: std::cout << "已拒绝"; break;
                    default: std::cout << "未知";
                }
                std::cout << " filled=" << order.filled_volume << "/" << order.volume << std::endl;
                
                if (order.status == OrderResult::Status::SUBMITTED ||
                    order.status == OrderResult::Status::PARTIAL) {
                    if (api_->cancel_order(order.order_id)) {
                        cancel_count++;
                        std::cout << "    ✓ Cancelled order: " << symbol 
                                  << ", order_id=" << order.order_id << std::endl;
                    } else {
                        std::cout << "    ✗ Cancel failed: " << symbol << std::endl;
                    }
                }
            }
        }
        
        stock->call_back = 1;  // 标记已处理
    }
    
    std::cout << "检查了 " << checked_count << " 个订单，成功撤单 " << cancel_count << " 个" << std::endl;
}

std::string IntradaySellStrategy::determine_condition(const StockParams& params) const {
    // txt line 144-153: 条件判断
    if (params.second_flag == 1) {
        return "lb";  // 连板
    }
    if (params.fb_flag == 1 && params.zb_flag == 0) {
        return "fb";  // 封板未炸板
    }
    if (params.fb_flag == 1 && params.zb_flag == 1) {
        return "hf";  // 回封（封板后炸板）
    }
    if (params.fb_flag == 0 && params.zb_flag == 1) {
        return "zb";  // 炸板
    }
    
    return "";  // 不满足任何条件
}

int IntradaySellStrategy::get_current_time() const {
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

int IntradaySellStrategy::get_current_date() const {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now_c);
#else
    localtime_r(&now_c, &local_tm);
#endif
    int year = local_tm.tm_year + 1900;
    int month = local_tm.tm_mon + 1;
    int day = local_tm.tm_mday;
    return year * 10000 + month * 100 + day;
}

void IntradaySellStrategy::print_status() const {
    std::cout << "\n=== Strategy Status ===" << std::endl;
    std::cout << "Total stocks: " << csv_config_.size() << std::endl;
    
    int completed = 0;
    int64_t total_sold = 0;
    
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (stock && stock->sell_flag == 1) {
            completed++;
        }
        if (stock) {
            total_sold += stock->sold_vol;
        }
    }
    
    std::cout << "Completed: " << completed << " / " << csv_config_.size() << std::endl;
    std::cout << "Total sold volume: " << total_sold << std::endl;
}
