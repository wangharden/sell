#include "SecTradingApi.h"
#include "../../src/core/Order.h"
#include "../../src/core/MarketData.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstring>
#include <vector>

// SEC ITPDK 头文件（C++ API）
#include "secitpdk/secitpdk.h"
#include "secitpdk/secitpdk_struct.h"

using std::vector;

// 静态成员初始化
std::map<std::string, SecTradingApi*> SecTradingApi::instances_;
std::mutex SecTradingApi::instances_mutex_;

// 注意：以下常量已在 SDK 的 itpdk_dict.h 中定义，这里仅作注释说明
// #define NOTIFY_PUSH_ORDER     1  // 确认推送
// #define NOTIFY_PUSH_MATCH     2  // 成交推送
// #define NOTIFY_PUSH_WITHDRAW  3  // 撤单推送
// #define NOTIFY_PUSH_INVALID   4  // 废单推送
// #define JYLB_BUY   0   // 买入
// #define JYLB_SALE  1   // 卖出

SecTradingApi::SecTradingApi() 
    : is_connected_(false),
      dry_run_mode_(false),
      order_id_counter_(100000) {
}

SecTradingApi::~SecTradingApi() {
    disconnect();
}

bool SecTradingApi::connect(const std::string& host, int port,
                            const std::string& user, 
                            const std::string& password) {
    if (is_connected_) {
        std::cerr << "[SEC] Already connected" << std::endl;
        return false;
    }
    
    // 保存连接参数
    account_id_ = user;
    password_ = password;
    config_section_ = host;  // host 参数作为配置段名称
    
    // ==================== 1. 设置路径（必须在 Init 之前）====================
    std::cout << "[SEC] Setting paths before init..." << std::endl;
    SECITPDK_SetLogPath("./log");      // 日志目录
    SECITPDK_SetProfilePath("./");     // 配置文件目录（itpdk.ini 所在位置）
    
    // ==================== 2. 初始化 SECITPDK ====================
    std::cout << "[SEC] Initializing SECITPDK..." << std::endl;
    bool bInit = SECITPDK_Init(HEADER_VER);
    if (!bInit) {
        std::cerr << "[SEC] SECITPDK_Init failed" << std::endl;
        return false;
    }
    
    // ==================== 3. 设置日志和委托方式 ====================
    SECITPDK_SetWriteLog(true);
    SECITPDK_SetFixWriteLog(true);     // FIX协议日志
    SECITPDK_SetWTFS("32");            // 委托方式：32=程序化交易
    
    // 获取版本信息
    char sVer[64] = {0};
    SECITPDK_GetVersion(sVer);
    std::cout << "[SEC] SECITPDK Version: " << sVer << std::endl;
    
    // ==================== 4. 登录（在设置回调之前）====================
    std::cout << "[SEC] Logging in (section: " << config_section_ 
              << ", account: " << account_id_ << ")..." << std::endl;
    
    int64_t nRet = SECITPDK_TradeLogin(config_section_.c_str(), 
                                       account_id_.c_str(), 
                                       password_.c_str());
    
    if (nRet <= 0) {
        char error_msg[256] = {0};
        SECITPDK_GetLastError(error_msg);
        std::string error(error_msg);
        std::cerr << "[SEC] Login failed: " << error << std::endl;
        SECITPDK_Exit();
        return false;
    }
    
    std::cout << "[SEC] Login success, token: " << nRet << std::endl;
    is_connected_ = true;
    
    // 注册实例到静态map（用于回调）
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        instances_[std::to_string(nRet)] = this;
        token_ = std::to_string(nRet); // 保存token
    }
    
    // ==================== 5. 设置回调函数（登录成功后）====================
    std::cout << "[SEC] Setting callbacks..." << std::endl;
    SECITPDK_SetStructMsgCallback(OnStructMsgCallback);
    
    // ==================== 6. 查询并缓存股东号 ====================
    std::cout << "[SEC] Querying shareholder accounts..." << std::endl;
    query_positions();  // 这会填充股东号
    
    return true;
}

void SecTradingApi::disconnect() {
    if (!is_connected_) {
        return;
    }
    
    // 从实例map中移除（使用 token_ 而非 account_id_）
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        instances_.erase(token_);  //  修正：使用 token_ 作为key
    }
    
    std::cout << "[SEC] Disconnecting..." << std::endl;
    SECITPDK_Exit();
    is_connected_ = false;
}

bool SecTradingApi::is_connected() const {
    return is_connected_;
}

void SecTradingApi::set_dry_run(bool enable) {
    dry_run_mode_ = enable;
    if (enable) {
        std::cout << "[SEC] *** DRY-RUN MODE ENABLED ***" << std::endl;
        std::cout << "[SEC] 将使用跌停价买入后立即撤单（不会实际成交）" << std::endl;
    } else {
        std::cout << "[SEC] DRY-RUN MODE DISABLED (正常交易模式)" << std::endl;
    }
}

std::string SecTradingApi::place_order(const OrderRequest& req) {
    if (!is_connected_) {
        std::cerr << "[SEC] Not connected" << std::endl;
        return "";
    }
    
    // 确定市场和股东号
    std::string market;
    std::string account;
    
    if (req.symbol.find(".SH") != std::string::npos) {
        market = "SH";
        account = sh_account_;
    } else if (req.symbol.find(".SZ") != std::string::npos) {
        market = "SZ";
        account = sz_account_;
    } else {
        std::cerr << "[SEC] Invalid symbol format: " << req.symbol << std::endl;
        return "";
    }
    
    if (account.empty()) {
        std::cerr << "[SEC] Shareholder account not found for market: " << market << std::endl;
        return "";
    }
    
    // 提取股票代码（去掉市场后缀）
    std::string stock_code = req.symbol.substr(0, req.symbol.find('.'));
    
    // ===== DRY-RUN 模式：使用跌停价买入后立即撤单（测试连接） =====
    if (dry_run_mode_) {
        std::cout << "[SEC] *** DRY-RUN MODE *** 测试交易API连接" << std::endl;
        
        // 注意：SecTradingApi 不负责行情，直接使用请求价格的90%作为跌停价
        // 如需真实跌停价，请使用 TradingMarketApi 组合适配器
        double down_limit = req.price * 0.9;
        
        std::cout << "[SEC] [DRY-RUN] 使用跌停价 " << down_limit 
                  << " 买入 100 股 " << stock_code << "（不会实际成交）" << std::endl;
        
        // 用买入方向（跌停价不会成交）
        int64_t sys_id = SECITPDK_OrderEntrust(
            account_id_.c_str(),
            market.c_str(),
            stock_code.c_str(),
            JYLB_BUY,              // 买入（而非卖出）
            100,                   // 最小单位
            down_limit,            // 跌停价（不会成交）
            0,                     // 限价单
            account.c_str()
        );
        
        if (sys_id > 0) {
            std::cout << "[SEC] [DRY-RUN] 测试订单已提交，sys_id: " << sys_id << std::endl;
            // 等待1秒，然后撤单
            std::this_thread::sleep_for(std::chrono::seconds(1));
            int64_t cancel_ret = SECITPDK_OrderWithdraw(account_id_.c_str(), market.c_str(), sys_id);
            if (cancel_ret > 0) {
                std::cout << "[SEC] [DRY-RUN] ✓ 测试订单已撤单，交易接口连接正常！" << std::endl;
            } else {
                std::cerr << "[SEC] [DRY-RUN] 撤单失败，但不影响测试" << std::endl;
            }
            return "dry-run-" + std::to_string(sys_id);
        } else {
            char error_msg[256] = {0};
            SECITPDK_GetLastError(error_msg);
            std::cerr << "[SEC] [DRY-RUN] 测试下单失败: " << error_msg << std::endl;
            return "";
        }
    }
    
    // ===== 正常模式：真实卖出 =====
    // 交易类别：卖出
    int trade_type = JYLB_SALE;
    
    // 订单类型：0-限价单
    int order_type = req.is_market ? 1 : 0;
    
    std::cout << "[SEC] Placing order: " << stock_code 
              << " " << market << " " << req.volume 
              << "@" << req.price << std::endl;
    
    // 生成本地订单ID
    std::string local_id = generate_order_id();
    
    // 调用下单接口（同步）
    int64_t sys_id = SECITPDK_OrderEntrust(
        account_id_.c_str(),    // 客户号
        market.c_str(),         // 交易所
        stock_code.c_str(),     // 证券代码
        trade_type,             // 交易类别（卖出）
        req.volume,             // 委托数量
        req.price,              // 委托价格
        order_type,             // 订单类型
        account.c_str()         // 股东号
    );
    
    if (sys_id <= 0) {
        char error_msg[256] = {0};
        SECITPDK_GetLastError(error_msg);
        std::string error(error_msg);
        std::cerr << "[SEC] Order failed: " << error << std::endl;
        return "";
    }
    
    std::cout << "[SEC] Order placed successfully, sys_id: " << sys_id << ", local_id: " << local_id << std::endl;
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        Order& order = orders_[local_id];
        order.order_id = local_id;
        order.symbol = req.symbol;
        order.volume = req.volume;
        order.price = req.price;
        order.status = OrderStatus::SUBMITTED;
        order.filled_volume = 0;
        order.filled_price = 0.0;
        order.remark = req.remark;  // ✅ 添加备注字段
        sysid_to_local_[sys_id] = local_id;
    }
    return local_id;
}

bool SecTradingApi::cancel_order(const std::string& order_id) {
    if (!is_connected_) {
        std::cerr << "[SEC] Not connected" << std::endl;
        return false;
    }
    
    std::string market;
    int64_t sys_id = 0;
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        for (const auto& kv : sysid_to_local_) {
            if (kv.second == order_id) {
                sys_id = kv.first;
                break;
            }
        }
        auto it = orders_.find(order_id);
        if (it != orders_.end()) {
            if (it->second.symbol.find(".SH") != std::string::npos) {
                market = "SH";
            } else if (it->second.symbol.find(".SZ") != std::string::npos) {
                market = "SZ";
            }
        }
    }
    if (market.empty() || sys_id == 0) {
        std::cerr << "[SEC] Cannot determine market/sys_id for order: " << order_id << std::endl;
        return false;
    }
    int64_t nRet = SECITPDK_OrderWithdraw(account_id_.c_str(), market.c_str(), sys_id);
    
    if (nRet <= 0) {
        char error_msg[256] = {0};
        SECITPDK_GetLastError(error_msg);
        std::string error(error_msg);
        std::cerr << "[SEC] Cancel order failed: " << error << std::endl;
        return false;
    }
    
    std::cout << "[SEC] Cancel order submitted: " << nRet << std::endl;
    
    // 更新订单状态
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it = orders_.find(order_id);
        if (it != orders_.end()) {
            it->second.status = OrderStatus::CANCELING;
        }
    }
    
    return true;
}

std::vector<Position> SecTradingApi::query_positions() {
    if (!is_connected_) {
        std::cerr << "[SEC] Not connected" << std::endl;
        return {};
    }
    
    std::cout << "[SEC] Querying positions..." << std::endl;
    
    std::vector<ITPDK_ZQGL> positions;
    
    // 查询持仓
    int64_t nRet = SECITPDK_QueryPositions(  // 使用 int64_t 而非 long
        account_id_.c_str(),  // 客户号
        0,                    // 排序类型
        0,                    // 请求行数（0表示全部）
        0,                    // 定位串
        "",                   // 股东号（空表示全部）
        "",                   // 交易所（空表示全部）
        "",                   // 证券代码（空表示全部）
        1,                    // 执行标志
        positions             // 返回结果
    );
    
    if (nRet < 0) {
        char error_msg[256] = {0};
        SECITPDK_GetLastError(error_msg);
        std::string error(error_msg);
        std::cerr << "[SEC] Query positions failed: " << error << std::endl;
        return {};
    }
    
    std::cout << "[SEC] Found " << nRet << " positions" << std::endl;
    
    // 转换为通用格式
    std::vector<Position> result;
    
    for (const auto& pos : positions) {
        // 缓存股东号
        std::string market(pos.Market);        // 交易所 - 修正字段名
        std::string account(pos.SecuAccount);  // 股东号 - 修正字段名
        
        if (market == "SH" && sh_account_.empty()) {
            sh_account_ = account;
            std::cout << "[SEC] Cached SH account: " << sh_account_ << std::endl;
        } else if (market == "SZ" && sz_account_.empty()) {
            sz_account_ = account;
            std::cout << "[SEC] Cached SZ account: " << sz_account_ << std::endl;
        }
        
        // 构造完整股票代码
        std::string symbol = std::string(pos.StockCode) + "." + market;  // 证券代码 - 修正字段名
        
        Position p;
        p.symbol = symbol;
        p.total = static_cast<int64_t>(pos.CurrentQty);    // 当前持仓 - 修正字段名
        p.available = static_cast<int64_t>(pos.QtyAvl);    // 可用数量 - 修正字段名
        p.frozen = static_cast<int64_t>(pos.FrozenQty);    // 冻结数量 - 修正字段名
        
        result.push_back(p);
        
        std::cout << "  " << p.symbol << ": total=" << p.total 
                  << ", available=" << p.available 
                  << ", frozen=" << p.frozen << std::endl;
    }
    
    // 缓存持仓
    {
        std::lock_guard<std::mutex> lock(positions_mutex_);
        positions_cache_ = result;
    }
    
    return result;
}

std::vector<OrderResult> SecTradingApi::query_orders() {
    if (!is_connected_) {
        std::cerr << "[SEC] Not connected" << std::endl;
        return {};
    }
    
    std::cout << "[SEC] Querying orders from API..." << std::endl;
    
    std::vector<ITPDK_DRWT> api_orders;
    
    // 调用真实API查询订单
    // 参数: khh, nType(0=当日), nSortType, nRowcount(0=全部), nBrowindex, 
    //       jys(空=全部), zqdm(空=全部), lWth(0=全部), arDrwt, nKFSBDBH
    int64_t nRet = SECITPDK_QueryOrders(
        account_id_.c_str(),  // 客户号
        0,                    // 查询类型：0=当日委托
        0,                    // 排序类型
        0,                    // 请求行数（0=全部）
        0,                    // 定位串
        "",                   // 交易所（空=全部）
        "",                   // 证券代码（空=全部）
        0,                    // 委托号（0=全部）
        api_orders            // 返回结果
    );
    
    if (nRet < 0) {
        char error_msg[256] = {0};
        SECITPDK_GetLastError(error_msg);
        std::string error(error_msg);
        std::cerr << "[SEC] Query orders failed: " << error << std::endl;
        return {};
    }
    
    std::cout << "[SEC] Found " << nRet << " orders from API" << std::endl;
    
    // 转换API返回的数据为 OrderResult 格式
    std::vector<OrderResult> result;
    result.reserve(api_orders.size());
    
    for (const auto& api_order : api_orders) {
        OrderResult order_result;
        order_result.success = true;
        order_result.order_id = std::to_string(api_order.OrderId);
        order_result.symbol = std::string(api_order.StockCode) + "." + api_order.Market;
        order_result.volume = api_order.OrderQty;
        order_result.filled_volume = api_order.MatchQty;
        order_result.price = api_order.OrderPrice;
        
        // 订单状态转换 (OrderStatus字段)
        // 参考 itpdk_dict.h 中的定义
        // 常见状态: 0=未报, 1=待报, 2=已报, 3=已报待撤, 4=部成待撤, 5=部成, 6=已成, 7=已撤, 8=部撤, 9=废单
        int status = api_order.OrderStatus;
        if (status == 0 || status == 1 || status == 2) {
            order_result.status = OrderResult::Status::SUBMITTED;  // 已提交/已报
        } else if (status == 5) {
            order_result.status = OrderResult::Status::PARTIAL;    // 部分成交
        } else if (status == 6) {
            order_result.status = OrderResult::Status::FILLED;     // 全部成交
        } else if (status == 7 || status == 8) {
            order_result.status = OrderResult::Status::CANCELLED;  // 已撤单
        } else if (status == 9) {
            order_result.status = OrderResult::Status::REJECTED;   // 废单
        } else {
            order_result.status = OrderResult::Status::UNKNOWN;
        }
        
        // 备注信息（如果API支持）
        // 注意：ITPDK_DRWT 结构中可能没有 remark 字段，这里留空
        // 如果需要，可以通过其他方式关联
        order_result.remark = "";
        
        result.push_back(order_result);
        
        std::cout << "  Order: " << order_result.order_id 
                  << " " << order_result.symbol
                  << " vol=" << order_result.volume
                  << " filled=" << order_result.filled_volume
                  << " status=" << status << std::endl;
    }
    
    // 更新内存缓存
    {
        std::lock_guard<std::mutex> lock(orders_mutex_);
        for (const auto& order : result) {
            // 如果内存中已有该订单，更新状态
            auto it = orders_.find(order.order_id);
            if (it != orders_.end()) {
                // 更新已有订单的状态
                it->second.filled_volume = order.filled_volume;
                if (order.status == OrderResult::Status::FILLED) {
                    it->second.status = OrderStatus::FILLED;
                } else if (order.status == OrderResult::Status::PARTIAL) {
                    it->second.status = OrderStatus::PARTIAL;
                } else if (order.status == OrderResult::Status::CANCELLED) {
                    it->second.status = OrderStatus::CANCELLED;
                } else if (order.status == OrderResult::Status::REJECTED) {
                    it->second.status = OrderStatus::REJECTED;
                }
            }
        }
    }
    
    return result;
}

OrderResult SecTradingApi::query_order(const std::string& order_id) {
    std::lock_guard<std::mutex> lock(orders_mutex_);
    
        auto it = orders_.find(order_id); // Changed to use string directly
    
    if (it != orders_.end()) {
        OrderResult result;
        result.success = true;
        result.order_id = order_id;
        result.symbol = it->second.symbol;
        result.volume = it->second.volume;
        result.filled_volume = it->second.filled_volume;
        result.price = it->second.price;
        
        // 转换字符串状态到枚举
        const std::string& status_str = it->second.status;
        if (status_str == "submitted" || status_str == "accepted") {
            result.status = OrderResult::Status::SUBMITTED;
        } else if (status_str == "partial_filled") {
            result.status = OrderResult::Status::PARTIAL;
        } else if (status_str == "filled") {
            result.status = OrderResult::Status::FILLED;
        } else if (status_str == "canceled" || status_str == "canceling") {
            result.status = OrderResult::Status::CANCELLED;
        } else if (status_str == "rejected") {
            result.status = OrderResult::Status::REJECTED;
        } else {
            result.status = OrderResult::Status::UNKNOWN;
        }
        
        return result;
    }
    
    // 订单未找到
    OrderResult result;
    result.success = false;
    result.err_msg = "Order not found";
    return result;
}

OrderResult SecTradingApi::wait_order(const std::string& order_id, int timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    
    while (true) {
        OrderResult order = query_order(order_id);
        
        // 订单未找到
        if (!order.success) {
            return order;
        }
        
        // 检查订单是否完成
        if (order.status == OrderResult::Status::FILLED || 
            order.status == OrderResult::Status::CANCELLED || 
            order.status == OrderResult::Status::REJECTED) {
            return order;
        }
        
        // 检查超时
        if (timeout_ms > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed >= timeout_ms) {
                break;
            }
        }
        
        // 短暂休眠
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return query_order(order_id);
}

// 静态回调函数
void SecTradingApi::OnStructMsgCallback(const char* pTime, stStructMsg& stMsg, int nType) {
    std::string token = std::to_string(stMsg.nStructToken); // 使用nStructToken
    
    // 查找对应的实例
    SecTradingApi* instance = nullptr;
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        auto it = instances_.find(token);
        if (it != instances_.end()) {
            instance = it->second;
        }
    }
    
    if (instance) {
        instance->handle_struct_msg(pTime, stMsg, nType);
    }
}

void SecTradingApi::OnOrderAsyncCallback(const char* pTime, stStructOrderFuncMsg& stMsg, int nType) {
    std::string account_id(stMsg.AccountId);
    
    // 查找对应的实例
    SecTradingApi* instance = nullptr;
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        auto it = instances_.find(account_id);
        if (it != instances_.end()) {
            instance = it->second;
        }
    }
    
    if (instance) {
        instance->handle_order_async(pTime, stMsg, nType);
    }
}

void SecTradingApi::handle_struct_msg(const char* pTime, stStructMsg& stMsg, int nType) {
    int64_t sys_id = stMsg.OrderId;
    std::string symbol = stMsg.StockCode;
    std::lock_guard<std::mutex> lock(orders_mutex_);
    auto it_local = sysid_to_local_.find(sys_id);
    if (it_local == sysid_to_local_.end()) return;
    auto it = orders_.find(it_local->second);
    if (it == orders_.end()) return;
    Order& order = it->second;
    
    if (nType == NOTIFY_PUSH_ORDER) {
        std::cout << "[SEC] Order confirmed: " << sys_id << " (" << symbol << ")" << std::endl;
        order.status = OrderStatus::ACCEPTED;
    } else if (nType == NOTIFY_PUSH_MATCH) {
        std::cout << "[SEC] Order matched: " << sys_id << " (" << symbol << ")"
                  << " qty=" << stMsg.MatchQty << " price=" << stMsg.MatchPrice << std::endl;
        // 累计加权平均价
        double total_value = order.filled_price * order.filled_volume + stMsg.MatchPrice * stMsg.MatchQty;
        order.filled_volume += stMsg.MatchQty;
        order.filled_price = order.filled_volume > 0 ? total_value / order.filled_volume : 0.0;
        if (order.filled_volume >= order.volume) {
            order.status = OrderStatus::FILLED;
        } else {
            order.status = OrderStatus::PARTIAL;
        }
    } else if (nType == NOTIFY_PUSH_WITHDRAW) {
        std::cout << "[SEC] Order canceled: " << sys_id << " (" << symbol << ")" << std::endl;
        order.status = OrderStatus::CANCELLED;
    } else if (nType == NOTIFY_PUSH_INVALID) {
        std::cout << "[SEC] Order rejected: " << sys_id << " (" << symbol << ")" << std::endl;
        order.status = OrderStatus::REJECTED;
    }
}

void SecTradingApi::handle_order_async(const char* pTime, stStructOrderFuncMsg& stMsg, int nType) {
    // 异步下单回调处理
    int64_t order_id = stMsg.OrderId;
    
    std::cout << "[SEC] Async order callback: order_id=" << order_id 
              << ", retcode=" << stMsg.nRetCode << std::endl;
    
    if (stMsg.nRetCode != 0) {
        std::string error(stMsg.sRetNote);
        std::cerr << "[SEC] Order error: " << error << std::endl;
        
        std::lock_guard<std::mutex> lock(orders_mutex_);
        auto it_local = sysid_to_local_.find(order_id);
        if (it_local != sysid_to_local_.end()) {
            auto it = orders_.find(it_local->second);
            if (it != orders_.end()) {
                it->second.status = OrderStatus::REJECTED;
            }
        }
    }
}

std::string SecTradingApi::generate_order_id() {
    std::lock_guard<std::mutex> lock(id_mutex_);
    return std::to_string(++order_id_counter_);
}

void SecTradingApi::update_order_status(int64_t order_id, 
                                       const std::string& status,
                                       const std::string& info) {
    std::lock_guard<std::mutex> lock(orders_mutex_);
    
    // 先找到local_id
    auto it_local = sysid_to_local_.find(order_id);
    if (it_local != sysid_to_local_.end()) {
        auto it = orders_.find(it_local->second);
        if (it != orders_.end()) {
            it->second.status = status;
            if (!info.empty()) {
                std::cout << "[SEC] Order " << order_id << ": " << info << std::endl;
            }
        }
    }
}
