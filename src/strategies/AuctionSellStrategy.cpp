#include "AuctionSellStrategy.h"
#include "../core/util.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <cmath>

AuctionSellStrategy::AuctionSellStrategy(
    TradingMarketApi* api,
    const std::string& csv_path,
    const std::string& account_id
) : api_(api), 
    csv_path_(csv_path), 
    account_id_(account_id),
    uniform_dist_(0.0, 1.0),
    normal_dist_(0.0, 1.0) {
    std::random_device rd;
    rng_.seed(rd());
}

bool AuctionSellStrategy::init() {
    std::cout << "=== Initializing AuctionSellStrategy ===" << std::endl;
    
    // 1. 加载CSV配置
    if (!csv_config_.load_from_file(csv_path_)) {
        std::cerr << "Failed to load CSV config from: " << csv_path_ << std::endl;
        return false;
    }
    
    // 2. 查询持仓并更新CSV
    auto positions = api_->query_positions();
    for (const auto& pos : positions) {
        auto* stock = csv_config_.get_stock(pos.symbol);
        if (stock) {
            stock->avail_vol = pos.available;
            stock->total_vol = pos.total;
            std::cout << "  " << pos.symbol << ": total=" << pos.total 
                      << ", avail=" << pos.available << std::endl;
        }
    }
    
    // 3. 获取涨停价和跌停价（从API获取）
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (stock) {
            // 获取涨停价和跌停价
            auto limits = api_->get_limits(symbol);
            stock->zt_price = limits.first;   // 涨停价
            stock->dt_price = limits.second;  // 跌停价
            
            std::cout << "  " << symbol << ": zt=" << stock->zt_price 
                      << ", dt=" << stock->dt_price 
                      << ", pre_close=" << stock->pre_close << std::endl;
        }
    }
    
    std::cout << "Strategy initialized with " << csv_config_.size() << " stocks" << std::endl;
    return true;
}

void AuctionSellStrategy::on_timer() {
    int now = get_current_time();
    
    // Phase 0: 行情检查 (09:20:05 - 09:23:00)
    if (now >= 92005 && now < 92300 && hangqin_check_ == 0) {
        check_market_data();
        hangqin_check_ = 1;
    }
    
    // Phase 1: 无条件卖出10% (09:23:30 - 09:25:00)
    if (now >= 92330 && now < 92500) {
        phase1_return1_sell();
    }
    
    // Phase 2: 条件卖出 (09:23:40 - 09:24:45)
    if (now >= 92340 && now < 92445) {
        phase2_conditional_sell();
    }
    
    // Phase 3: 涨停未封板处理与最后冲刺 (09:24:50 - 09:25:00)
    if (now >= 92450 && now < 92500) {
        phase3_final_sell();
    }
    
    // 撤单处理 (09:25:13 - 09:25:23)
    if (now >= 92513 && now < 92523) {
        cancel_auction_orders();
    }
    
    // 收集集合竞价数据 (09:26:00 - 09:28:10)
    if (now >= 92600 && now < 92810 && before_check_ == 0) {
        collect_auction_data();
        before_check_ = 1;
    }
    
    // 开盘后继续卖出 (09:29:55 - 09:30:40)
    if (now >= 92955 && now < 93040) {
        after_open_sell();
    }
}

void AuctionSellStrategy::check_market_data() {
    std::cout << "=== Phase 0: Checking market data ===" << std::endl;
    
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        MarketSnapshot snap = api_->get_snapshot(symbol);
        if (snap.valid) {
            std::cout << "  " << symbol << " market data OK" << std::endl;
        } else {
            std::cout << "  " << symbol << " market data FAILED" << std::endl;
        }
    }
}

void AuctionSellStrategy::phase1_return1_sell() {
    auto positions = api_->query_positions();
    
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (!stock || stock->return1_sell == 1 || stock->sell_flag == 1) {
            continue;
        }
        
        // 查找持仓
        int64_t avail_vol = 0;
        int64_t total_vol = 0;
        for (const auto& pos : positions) {
            if (pos.symbol == symbol) {
                avail_vol = std::max(pos.available - hold_vol_, int64_t(0));
                total_vol = std::max(pos.total - hold_vol_, int64_t(0));
                break;
            }
        }
        
        int64_t vol = std::min(avail_vol, total_vol);
        if (vol == 0) {
            stock->sell_flag = 1;
            stock->return1_sell = 1;
            continue;
        }
        
        // 获取行情
        MarketSnapshot snap = api_->get_snapshot(symbol);
        if (!snap.valid) continue;
        
        double buy_price1 = snap.bid_price1;
        double ask_vol2 = snap.ask_volume2;
        
        // 涨停判断：买一价=涨停价 且 卖二有量（说明涨停但未封牢）
        // 集合竞价期间，如果涨停但未封板：Ask2、Ask1、Bid1 都会在涨停价，且Ask2有量
        if (std::abs(buy_price1 - stock->zt_price) < 0.01 && ask_vol2 > 0) {
            continue;
        }
        
        // 无条件卖出10%仓位，挂跌停价
        vol = (vol / 100 / 10) * 100;  // 10%向下取整到100股
        if (vol <= 0) continue;
        
        stock->return1_sell = 1;
        
        OrderRequest req;
        req.account_id = account_id_;
        req.symbol = symbol;
        req.price = stock->dt_price;  // 跌停价
        req.volume = vol;
        req.is_market = false;
        req.remark = "盘前卖出" + symbol;
        
        std::string order_id = api_->place_order(req);
        
        if (!order_id.empty()) {
            stock->total_sell += vol;
            stock->userOrderId = req.remark;
            std::cout << "  [Phase1] " << symbol << " sell " << vol 
                      << " @ " << stock->dt_price << ", order=" << order_id << std::endl;
        }
    }
}

void AuctionSellStrategy::phase2_conditional_sell() {
    auto positions = api_->query_positions();
    
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (!stock || stock->sell_flag == 1) {
            continue;
        }
        
        // 12.5%概率触发 (txt line 142)
        double p = uniform_dist_(rng_);
        if (p >= 0.125) {
            continue;
        }
        
        // 查找持仓
        int64_t avail_vol = 0;
        int64_t total_vol = 0;
        for (const auto& pos : positions) {
            if (pos.symbol == symbol) {
                avail_vol = std::max(pos.available - hold_vol_, int64_t(0));
                total_vol = std::max(pos.total - hold_vol_, int64_t(0));
                break;
            }
        }
        
        int64_t vol = std::min(avail_vol, total_vol);
        if (vol == 0) {
            stock->sell_flag = 1;
            continue;
        }
        
        // 获取行情
        MarketSnapshot snap = api_->get_snapshot(symbol);
        if (!snap.valid) continue;
        
        double buy_price1 = snap.bid_price1;
        double ask_vol1 = snap.ask_volume1;
        double ask_vol2 = snap.ask_volume2;
        
        // 限制总卖出量不超过市场ask1的一定比例
        if (sell_to_mkt_ratio_ > 0) {
            if (stock->total_sell / 100.0 >= ask_vol1 * sell_to_mkt_ratio_) {
                std::cout << "  " << symbol << " skip: total_sell=" 
                          << stock->total_sell / 100.0 << ", ask1=" << ask_vol1 << std::endl;
                continue;
            }
        }
        
        // 涨停判断：买一价=涨停价 且 卖二有量（说明涨停但未封牢）
        if (std::abs(buy_price1 - stock->zt_price) < 0.01 && ask_vol2 > 0) {
            continue;
        }
        
        double pre_close = stock->pre_close;
        if (pre_close <= 0) continue;
        
        // 随机数量计算
        if (single_amt_ < buy_price1 * vol) {
            double U = uniform_dist_(rng_);
            double N = normal_dist_(rng_);
            double temp_amt = single_amt_ - rand_amt1_ / 2.0 
                            + rand_amt1_ * U 
                            + N * rand_amt2_;
            int64_t temp_vol = static_cast<int64_t>(temp_amt / buy_price1 / 100) * 100;
            vol = std::min(vol, temp_vol);
        }
        
        if (vol <= 0) continue;
        
        // 根据条件确定卖出价格
        double sell_price = 0;
        std::string condition;
        
        if (stock->second_flag == 1) {
            // 连板：买1价>=昨收*1.07，挂昨收*1.07
            double gaokai_price = ceil_round(pre_close * 1.07 + 1e-6, 2);
            if (buy_price1 >= gaokai_price) {
                sell_price = gaokai_price;
                condition = "连板";
            }
        } else if (stock->fb_flag == 1 && stock->zb_flag == 0 
                   && buy_price1 * ask_vol1 * 100 < 15e6) {
            // 封死：ask1成交额<1500万，买1>=昨收*1.015
            double gaokai_price = ceil_round(pre_close * 1.015 + 1e-6, 2);
            if (buy_price1 >= gaokai_price) {
                sell_price = gaokai_price;
                condition = "封死";
            }
        } else if (stock->fb_flag == 0 && stock->zb_flag == 1 
                   && buy_price1 * ask_vol1 * 100 < 3e6) {
            // 炸板：ask1成交额<300万，买1>=昨收*1.01
            double gaokai_price = ceil_round(pre_close * 1.01 + 1e-6, 2);
            if (buy_price1 >= gaokai_price) {
                sell_price = gaokai_price;
                condition = "炸板";
            }
        }
        
        if (sell_price <= 0) continue;  // 不满足条件
        
        OrderRequest req;
        req.account_id = account_id_;
        req.symbol = symbol;
        req.price = sell_price;
        req.volume = vol;
        req.is_market = false;
        req.remark = "盘前卖出" + symbol;
        
        std::string order_id = api_->place_order(req);
        
        if (!order_id.empty()) {
            stock->total_sell += vol;
            stock->userOrderId = req.remark;
            std::cout << "  [Phase2] " << symbol << " " << condition 
                      << " sell " << vol << " @ " << sell_price 
                      << ", order=" << order_id << std::endl;
        }
    }
}

int AuctionSellStrategy::get_current_time() const {
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

int AuctionSellStrategy::get_current_date() const {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;
#ifdef _WIN32
    localtime_s(&now_tm, &now_time_t);
#else
    localtime_r(&now_time_t, &now_tm);
#endif
    
    return (now_tm.tm_year + 1900) * 10000 + (now_tm.tm_mon + 1) * 100 + now_tm.tm_mday;
}

void AuctionSellStrategy::print_status() const {
    std::cout << "\n=== Auction Strategy Status ===" << std::endl;
    std::cout << "Total stocks: " << csv_config_.size() << std::endl;
    
    int completed = 0;
    int64_t total_sold = 0;
    
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (stock && stock->sell_flag == 1) {
            completed++;
        }
        if (stock) {
            total_sold += stock->total_sell;
        }
    }
    
    std::cout << "Completed: " << completed << " / " << csv_config_.size() << std::endl;
    std::cout << "Total sold volume: " << total_sold << std::endl;
}

void AuctionSellStrategy::phase3_final_sell() {
    auto positions = api_->query_positions();
    
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (!stock || stock->sell_flag == 1) {
            continue;
        }
        
        // 查找持仓
        int64_t avail_vol = 0;
        int64_t total_vol = 0;
        for (const auto& pos : positions) {
            if (pos.symbol == symbol) {
                avail_vol = std::max(pos.available - hold_vol_, int64_t(0));
                total_vol = std::max(pos.total - hold_vol_, int64_t(0));
                break;
            }
        }
        
        int64_t vol = std::min(avail_vol, total_vol);
        if (vol == 0) {
            stock->sell_flag = 1;
            continue;
        }
        
        // 获取行情
        MarketSnapshot snap = api_->get_snapshot(symbol);
        if (!snap.valid) continue;
        
        double buy_price1 = snap.bid_price1;
        double buy_vol2 = snap.bid_volume2;
        double ask_vol1 = snap.ask_volume1;
        double ask_vol2 = snap.ask_volume2;
        
        // Phase3 涨停未封板判断
        // 原始逻辑：买1=涨停 且 买2==0 且 卖2>0（说明涨停但封板不牢固）
        // 集合竞价涨停未封板特征：Ask2、Ask1、Bid1都在涨停价，Bid2为0或很小，Ask2有量
        if (std::abs(buy_price1 - stock->zt_price) < 0.01 && 
            buy_vol2 == 0 && 
            ask_vol2 > 0 && 
            stock->limit_sell == 0) {
            // 卖出一半仓位，价格为涨停价-0.01
            double gaokai_price = stock->zt_price - 0.01;
            int64_t half_vol = (vol / 100 / 2) * 100;  // 一半向下取整
            
            if (half_vol > 0) {
                OrderRequest req;
                req.account_id = account_id_;
                req.symbol = symbol;
                req.price = gaokai_price;
                req.volume = half_vol;
                req.is_market = false;
                req.remark = "盘前卖出" + symbol;
                
                std::string order_id = api_->place_order(req);
                
                if (!order_id.empty()) {
                    stock->total_sell += half_vol;
                    stock->userOrderId = req.remark;
                    stock->limit_sell = 1;
                    std::cout << "  [Phase3-LimitUp] " << symbol << " sell " << half_vol 
                              << " @ " << gaokai_price << " (zt-0.01), order=" << order_id << std::endl;
                }
            }
            continue;  // 处理完涨停，跳过后续逻辑
        }
        
        // txt line 248-251: 仓位系数限流
        if (sell_to_mkt_ratio_ > 0) {
            if (stock->total_sell / 100.0 > ask_vol1 * sell_to_mkt_ratio_) {
                std::cout << "  " << symbol << " skip (ratio): total_sell=" 
                          << stock->total_sell / 100.0 << ", ask1=" << ask_vol1 << std::endl;
                continue;
            }
            // 按仓位系数限制vol
            int64_t temp_vol = static_cast<int64_t>((ask_vol1 * sell_to_mkt_ratio_) - (stock->total_sell / 100.0)) * 100;
            vol = std::min(vol, temp_vol);
        }
        
        // Phase3 普通卖出涨停判断
        // 原始逻辑：涨停 且 卖2无量 → 跳过（说明涨停封死）
        // 集合竞价涨停封死特征：买1=涨停价 且 卖2无量
        if (std::abs(buy_price1 - stock->zt_price) < 0.01 && ask_vol2 <= 0) {
            std::cout << "  " << symbol << " is at limit up (sealed), skip phase3 normal sell" << std::endl;
            continue;
        }
        
        double pre_close = stock->pre_close;
        if (pre_close <= 0 || vol <= 0) continue;
        
        // txt line 254-275: 重复连板/封死/炸板判断，时间窗内成交后置sell_flag
        double sell_price = 0;
        std::string condition;
        
        if (stock->second_flag == 1) {
            double gaokai_price = ceil_round(pre_close * 1.07 + 1e-6, 2);
            if (buy_price1 >= gaokai_price) {
                sell_price = gaokai_price;
                condition = "连板";
            }
        } else if (stock->fb_flag == 1 && stock->zb_flag == 0 
                   && buy_price1 * ask_vol1 * 100 < 15e6) {
            double gaokai_price = ceil_round(pre_close * 1.015 + 1e-6, 2);
            if (buy_price1 >= gaokai_price) {
                sell_price = gaokai_price;
                condition = "封死";
            }
        } else if (stock->fb_flag == 0 && stock->zb_flag == 1 
                   && buy_price1 * ask_vol1 * 100 < 3e6) {
            double gaokai_price = ceil_round(pre_close * 1.01 + 1e-6, 2);
            if (buy_price1 >= gaokai_price) {
                sell_price = gaokai_price;
                condition = "炸板";
            }
        }
        
        if (sell_price <= 0) continue;
        
        OrderRequest req;
        req.account_id = account_id_;
        req.symbol = symbol;
        req.price = sell_price;
        req.volume = vol;
        req.is_market = false;
        req.remark = "盘前卖出" + symbol;
        
        std::string order_id = api_->place_order(req);
        
        if (!order_id.empty()) {
            stock->total_sell += vol;
            stock->userOrderId = req.remark;
            stock->sell_flag = 1;  // 此窗口成交后置标志
            std::cout << "  [Phase3] " << symbol << " " << condition 
                      << " sell " << vol << " @ " << sell_price 
                      << ", order=" << order_id << std::endl;
        }
    }
}

void AuctionSellStrategy::cancel_auction_orders() {
    std::cout << "=== Canceling auction orders ===" << std::endl;
    
    // txt line 279-294: 查询订单列表，撤销未成交的"盘前卖出"订单
    auto orders = api_->query_orders();
    int cancel_count = 0;
    
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (!stock) continue;
        
        for (const auto& order : orders) {
            // 匹配remark字段
            if (order.remark == stock->userOrderId || 
                order.remark.find("盘前卖出" + symbol) != std::string::npos) {
                // 状态不是已成交(56)则撤单
                if (order.status != OrderResult::Status::FILLED) {
                    if (api_->cancel_order(order.order_id)) {
                        cancel_count++;
                        std::cout << "  Cancelled: " << symbol 
                                  << ", order_id=" << order.order_id << std::endl;
                    }
                }
            }
        }
        
        stock->call_back = 1;  // 标记已处理
    }
    
    std::cout << "Total cancelled: " << cancel_count << " orders" << std::endl;
}

void AuctionSellStrategy::collect_auction_data() {
    std::cout << "=== Collecting auction data ===" << std::endl;
    
    // txt line 297-306: 取09:27前最后一条tick，记录jjamt和open
    int date = get_current_date();
    std::string date_str = std::to_string(date);
    
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (!stock) continue;
        
        // 通过API获取09:15-09:27的集合竞价数据
        auto auction_data = api_->get_auction_data(symbol, date_str, "092700");
        stock->open_price = auction_data.first;   // 开盘价
        stock->jjamt = auction_data.second;       // 集合竞价成交金额
        
        std::cout << "  " << symbol << ": open=" << stock->open_price 
                  << ", jjamt=" << stock->jjamt << std::endl;
        
        // 重置sell_flag为0，准备开盘后卖出
        stock->sell_flag = 0;
    }
}

void AuctionSellStrategy::after_open_sell() {
    // txt line 308-391: 开盘后继续卖出，每3秒触发一次
    kaipan_timer_++;
    
    // 每3秒触发
    if (kaipan_timer_ % 6 != 0) {  // 500ms * 6 = 3秒
        return;
    }
    
    auto positions = api_->query_positions();
    
    for (const auto& symbol : csv_config_.get_all_symbols()) {
        auto* stock = csv_config_.get_stock(symbol);
        if (!stock || stock->sell_flag == 1) {
            continue;
        }
        
        // 查找持仓
        int64_t avail_vol = 0;
        int64_t total_vol = 0;
        for (const auto& pos : positions) {
            if (pos.symbol == symbol) {
                avail_vol = std::max(pos.available - hold_vol_, int64_t(0));
                total_vol = std::max(pos.total - hold_vol_, int64_t(0));
                break;
            }
        }
        
        int64_t vol = std::min(avail_vol, total_vol);
        if (vol == 0) {
            stock->sell_flag = 1;
            continue;
        }
        
        // 获取行情
        MarketSnapshot snap = api_->get_snapshot(symbol);
        if (!snap.valid) continue;
        
        double buy_price1 = snap.bid_price1;
        double sell_price1 = snap.ask_price1;
        
        // 检查涨停
        if (stock->zt_price > 0 && std::abs(buy_price1 - stock->zt_price) < 0.01) {
            continue;
        }
        
        double pre_close = stock->pre_close;
        if (pre_close <= 0) continue;
        
        // txt line 359-361: 随机数量计算（较大的上限）
        // single_amt*5 - rand_amt1*2 + rand_amt1*4*rand() + normal*rand_amt2
        if (single_amt_ < buy_price1 * vol) {
            double U = uniform_dist_(rng_);
            double N = normal_dist_(rng_);
            double temp_amt = single_amt_ * 5.0 - rand_amt1_ * 2.0 
                            + rand_amt1_ * 4.0 * U 
                            + N * rand_amt2_;
            int64_t temp_vol = static_cast<int64_t>(temp_amt / buy_price1 / 100) * 100;
            vol = std::min(vol, temp_vol);
        }
        
        if (vol <= 0) continue;
        
        // txt line 363-377: 封死票，小量高开，盘前没卖完
        if (stock->fb_flag == 1 && stock->zb_flag == 0 && 
            stock->open_price >= ceil_round(pre_close * 1.015 + 1e-6, 2) && 
            stock->jjamt < 15e6) {
            // 接受1%滑点
            double open_ratio = stock->open_price / pre_close;
            double loss_price = ceil_round(pre_close * (open_ratio - 0.01) + 1e-6, 2);
            double base_price = ceil_round(pre_close * 1.015 + 1e-6, 2);
            double sell_price = std::max(base_price, loss_price);
            
            if (sell_price >= loss_price) {
                OrderRequest req;
                req.account_id = account_id_;
                req.symbol = symbol;
                req.price = sell_price;
                req.volume = vol;
                req.is_market = false;
                req.remark = "盘前卖出" + symbol;
                
                std::string order_id = api_->place_order(req);
                
                if (!order_id.empty()) {
                    stock->total_sell += vol;
                    stock->userOrderId = req.remark;
                    stock->call_back = 0;
                    std::cout << "  [AfterOpen-封死] " << symbol << " sell " << vol 
                              << " @ " << sell_price << ", order=" << order_id << std::endl;
                }
            }
        }
        // txt line 379-391: 炸板票，小量高开，盘前没卖完
        else if (stock->fb_flag == 0 && stock->zb_flag == 1 && 
                 stock->open_price >= ceil_round(pre_close * 1.01 + 1e-6, 2) && 
                 stock->jjamt < 3e6) {
            // 接受1%滑点
            double open_ratio = stock->open_price / pre_close;
            double loss_price = ceil_round(pre_close * (open_ratio - 0.01) + 1e-6, 2);
            double base_price = ceil_round(pre_close * 1.01 + 1e-6, 2);
            double sell_price = std::max(base_price, loss_price);
            
            if (sell_price >= loss_price) {
                OrderRequest req;
                req.account_id = account_id_;
                req.symbol = symbol;
                req.price = sell_price;
                req.volume = vol;
                req.is_market = false;
                req.remark = "盘前卖出" + symbol;
                
                std::string order_id = api_->place_order(req);
                
                if (!order_id.empty()) {
                    stock->total_sell += vol;
                    stock->userOrderId = req.remark;
                    stock->call_back = 0;
                    std::cout << "  [AfterOpen-炸板] " << symbol << " sell " << vol 
                              << " @ " << sell_price << ", order=" << order_id << std::endl;
                }
            }
        }
    }
}
