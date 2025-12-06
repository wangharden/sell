#include "TradingManager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

// ============================================================================
// SessionSelector 实现
// ============================================================================
Session SessionSelector::get_session(const std::string& time_str) {
    if (time_str.length() < 6) {
        return Session::CLOSED;
    }

    int hhmm = std::stoi(time_str.substr(0, 4));
    int hhmmss = std::stoi(time_str.substr(0, 6));

    // 盘前/开盘竞价 09:15-09:30
    if (hhmmss >= 91500 && hhmmss < 93000) {
        if (hhmmss >= 92500) {
            return Session::OPEN_AUCTION;  // 09:25-09:30 集合竞价
        }
        return Session::PRE_MARKET;  // 09:15-09:25 盘前
    }

    // 盘中 09:30-14:57
    if ((hhmmss >= 93000 && hhmmss < 113000) ||  // 上午
        (hhmmss >= 130000 && hhmmss < 145700)) { // 下午
        return Session::INTRADAY;
    }

    // 收盘集合竞价 14:57-15:00
    if (hhmmss >= 145700 && hhmmss < 150000) {
        return Session::CLOSE_AUCTION;
    }

    // 盘后 15:00-15:30（可选）
    if (hhmmss >= 150000 && hhmmss < 153000) {
        return Session::POST_MARKET;
    }

    return Session::CLOSED;
}

// ============================================================================
// TradingManager 实现
// ============================================================================
TradingManager::TradingManager(TradingApiPtr api, ConfigPtr config)
    : api_(api), config_(config), initialized_(false) {}

bool TradingManager::initialize(const std::string& account_id) {
    account_id_ = account_id;
    
    // 注意：ITradingApi的connect现在需要host, port, user, password参数
    // 这里简化为使用默认值，实际应从配置文件读取
    if (!api_->connect("localhost", 8080, account_id, "")) {
        std::cerr << "Failed to connect trading API for account: " 
                  << account_id << std::endl;
        return false;
    }

    initialized_ = true;
    std::cout << "TradingManager initialized for account: " 
              << account_id << std::endl;
    return true;
}

void TradingManager::shutdown() {
    if (initialized_) {
        api_->disconnect();
        initialized_ = false;
        std::cout << "TradingManager shut down." << std::endl;
    }
}

void TradingManager::register_executor(const std::string& name, 
                                        SellExecutorPtr executor) {
    executors_[name] = executor;
    std::cout << "Registered executor: " << name << std::endl;
}

std::vector<OrderResult> TradingManager::execute_sell(const SellRequest& req) {
    std::vector<OrderResult> results;

    if (!initialized_) {
        std::cerr << "TradingManager not initialized!" << std::endl;
        return results;
    }

    // 1. 查找执行器
    auto it = executors_.find(req.executor_name);
    if (it == executors_.end()) {
        std::cerr << "Executor not found: " << req.executor_name << std::endl;
        return results;
    }
    SellExecutorPtr executor = it->second;

    // 2. 查询持仓与行情
    auto positions = api_->query_positions();
    Position pos;
    for (const auto& p : positions) {
        if (p.symbol == req.symbol) {
            pos = p;
            break;
        }
    }

    if (pos.symbol.empty() || pos.available <= 0) {
        std::cerr << "No available position for: " << req.symbol << std::endl;
        return results;
    }

    MarketSnapshot snapshot = api_->get_snapshot(req.symbol);

    // 3. 构造 OrderRequest
    OrderRequest order_req;
    order_req.account_id = account_id_;
    order_req.symbol = req.symbol;
    order_req.volume = req.target_qty;
    order_req.price = req.price_hint;
    order_req.is_market = (req.price_hint == 0.0);
    order_req.remark = generate_remark(req.remark_prefix, req.symbol);

    // 4. 执行器生成子订单
    auto child_orders = executor->execute(order_req, pos, snapshot);

    // 5. 逐个下单
    for (auto& child : child_orders) {
        child.account_id = account_id_;
        child.remark = order_req.remark;  // 统一 remark

        std::string order_id = api_->place_order(child);
        
        OrderResult result;
        result.success = !order_id.empty();
        result.order_id = order_id;
        result.symbol = child.symbol;
        result.volume = child.volume;
        result.price = child.price;
        
        results.push_back(result);

        if (result.success) {
            pending_orders_[result.order_id] = child;
            std::cout << "[ORDER] " << req.symbol << " qty=" << child.volume
                      << " price=" << child.price << " id=" << result.order_id 
                      << std::endl;
        } else {
            std::cerr << "[ORDER_FAIL] " << req.symbol << std::endl;
        }
    }

    return results;
}

Session TradingManager::current_session() const {
    // 获取当前时间（格式 HHMMSS）
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << tm_buf.tm_hour
        << std::setw(2) << tm_buf.tm_min
        << std::setw(2) << tm_buf.tm_sec;

    return SessionSelector::get_session(oss.str());
}

size_t TradingManager::cancel_orders(const std::vector<std::string>& symbols,
                                      const std::string& remark_filter) {
    if (!initialized_) {
        return 0;
    }

    auto orders = api_->query_orders();
    size_t canceled = 0;

    for (const auto& order : orders) {
        // 跳过已完成或已撤销的订单
        if (order.status == OrderResult::Status::FILLED || 
            order.status == OrderResult::Status::CANCELLED) {
            continue;
        }
        
        // 过滤条件：symbols 和 remark
        bool match_symbol = symbols.empty();
        if (!match_symbol) {
            auto it = pending_orders_.find(order.order_id);
            if (it != pending_orders_.end()) {
                for (const auto& sym : symbols) {
                    if (it->second.symbol == sym) {
                        match_symbol = true;
                        break;
                    }
                }
            }
        }

        bool match_remark = remark_filter.empty();
        if (!match_remark) {
            auto it = pending_orders_.find(order.order_id);
            if (it != pending_orders_.end()) {
                if (it->second.remark.find(remark_filter) != std::string::npos) {
                    match_remark = true;
                }
            }
        }

        if (match_symbol && match_remark) {
            if (api_->cancel_order(order.order_id)) {
                canceled++;
                std::cout << "[CANCEL] order_id=" << order.order_id << std::endl;
            }
        }
    }

    return canceled;
}

void TradingManager::on_order(const OrderResult& result) {
    std::cout << "[ON_ORDER] id=" << result.order_id 
              << " success=" << result.success << std::endl;
}

void TradingManager::on_trade(const OrderResult& result) {
    std::cout << "[ON_TRADE] id=" << result.order_id 
              << " filled=" << result.filled_volume << std::endl;

    // 更新已卖出量
    auto it = pending_orders_.find(result.order_id);
    if (it != pending_orders_.end()) {
        const std::string& symbol = it->second.symbol;
        sold_volumes_[symbol] += result.filled_volume;
    }
}

void TradingManager::on_order_error(const std::string& order_id, 
                                     int error_code, 
                                     const std::string& error_msg) {
    std::cerr << "[ON_ORDER_ERROR] id=" << order_id 
              << " code=" << error_code 
              << " msg=" << error_msg << std::endl;
}

void TradingManager::on_cancel_error(const std::string& order_id,
                                      int error_code,
                                      const std::string& error_msg) {
    std::cerr << "[ON_CANCEL_ERROR] id=" << order_id 
              << " code=" << error_code 
              << " msg=" << error_msg << std::endl;
}

void TradingManager::on_disconnected() {
    std::cerr << "[ON_DISCONNECTED]" << std::endl;
    initialized_ = false;
}

std::string TradingManager::generate_remark(const std::string& prefix, 
                                             const std::string& symbol) const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream oss;
    oss << prefix << "_" << symbol << "_" << ms;
    return oss.str();
}
