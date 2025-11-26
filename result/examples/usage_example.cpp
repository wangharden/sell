// 使用示例：SecTradingApi + TdfMarketDataApi + IntradaySellStrategy
// 演示完整的交易、行情和策略功能

#include "SecTradingApi.h"
#include "TdfMarketDataApi.h"
#include "TradingMarketApi.h"
#include "../src/strategies/IntradaySellStrategy.h"  // 使用strategies目录中的版本
#include "../src/core/ConfigReader.h"  // 配置文件读取器

#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <thread>
#include <vector>
#include <string>
#include <mutex>  // 添加mutex头文件

#ifdef _WIN32
#include <direct.h>  // Windows: _mkdir
#define MKDIR(dir) _mkdir(dir)
#else
#include <sys/stat.h>  // Linux: mkdir
#define MKDIR(dir) mkdir(dir, 0755)
#endif

// 辅助函数：从CSV文件读取所有股票代码
std::vector<std::string> load_symbols_from_csv(const std::string& csv_path) {
    std::vector<std::string> symbols;
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        std::cerr << "[CSV错误] 无法打开文件: " << csv_path << std::endl;
        return symbols;
    }
    
    std::string line;
    bool first_line = true;
    while (std::getline(file, line)) {
        if (first_line) {
            first_line = false;
            continue;  // 跳过表头
        }
        if (line.empty()) continue;
        
        // 解析CSV：SYMBOL在第3列（索引2）
        std::stringstream ss(line);
        std::string token;
        int col = 0;
        std::string symbol_code;
        
        while (std::getline(ss, token, ',')) {
            if (col == 2) {  // SYMBOL列
                symbol_code = token;
                break;
            }
            col++;
        }
        
        if (symbol_code.empty() || symbol_code.length() != 6) continue;
        
        // 转换为Wind代码格式：6开头 -> .SH，否则 -> .SZ
        std::string wind_code = symbol_code;
        if (symbol_code[0] == '6') {
            wind_code += ".SH";
        } else {
            wind_code += ".SZ";
        }
        
        symbols.push_back(wind_code);
    }
    
    file.close();
    std::cout << "[CSV加载] 从 " << csv_path << " 读取到 " << symbols.size() << " 只股票" << std::endl;
    return symbols;
}

// 改进的日志工具类 - 线程安全、性能优化、支持上下文
class Logger {
private:
    std::ofstream log_file_; 
    std::string log_path_;
    std::mutex log_mutex_;
    std::string context_;
    int flush_counter_ = 0;
    static const int FLUSH_INTERVAL = 10;

    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);
        std::stringstream ss;
        ss << time_str << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    static std::string get_date() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        char date_str[16];
        strftime(date_str, sizeof(date_str), "%Y%m%d", &tm_buf);
        return std::string(date_str);
    }

    // 检查并创建日志目录
    static void ensure_log_dir(const std::string& dir) {
        // C++14 兼容：使用平台相关的 mkdir
        MKDIR(dir.c_str());
    }

public:
    Logger(const std::string& log_name = "trading_test") {
        // 使用相对路径，程序会在当前工作目录下创建log文件夹
        std::string log_dir = "./log";
        std::cerr << "[Logger] 日志目录: " << log_dir << std::endl;
        ensure_log_dir(log_dir);
        log_path_ = log_dir + "/" + log_name + "_" + get_date() + ".log";
        std::cerr << "[Logger] 日志文件路径: " << log_path_ << std::endl;
        log_file_.open(log_path_, std::ios::app);
        if (log_file_.is_open()) {
            std::cerr << "[Logger] 日志文件打开成功!" << std::endl;
            log("========== 新的测试会话开始 ==========");
        } else {
            std::cerr << "[Logger] 无法打开日志文件: " << log_path_ << std::endl;
        }
    }

    ~Logger() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        if (log_file_.is_open()) {
            log_file_ << "[" << get_timestamp() << "] [INFO] ========== 测试会话结束 ==========\n";
            log_file_.flush();
            log_file_.close();
        }
    }

    void set_context(const std::string& context) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        context_ = context;
    }

    void clear_context() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        context_.clear();
    }

    void log(const std::string& message, const std::string& level = "INFO") {
        std::lock_guard<std::mutex> lock(log_mutex_);
        std::stringstream ss;
        ss << "[" << get_timestamp() << "] [" << level << "]";
        if (!context_.empty()) {
            ss << " [" << context_ << "]";
        }
        ss << " " << message;
        std::string log_line = ss.str();
        std::cout << log_line << std::endl;
        if (log_file_.is_open()) {
            log_file_ << log_line << std::endl;
            flush_counter_++;
            if (flush_counter_ >= FLUSH_INTERVAL || level == "ERROR" || level == "WARN") {
                log_file_.flush();
                flush_counter_ = 0;
            }
        }
    }

    void info(const std::string& message) { log(message, "INFO"); }
    void warn(const std::string& message) { log(message, "WARN"); }
    void error(const std::string& message) { log(message, "ERROR"); }
    void debug(const std::string& message) { log(message, "DEBUG"); }

    void flush() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        if (log_file_.is_open()) {
            log_file_.flush();
        }
    }
};

// 全局日志实例
static std::unique_ptr<Logger> g_logger;

void example_basic_usage(const ConfigReader& config) {
    g_logger->set_context("example_basic_usage");  // 设置上下文
    g_logger->info("=== 基础使用示例 ===");
    
    // 1. 创建交易API实例
    auto trading_api = std::make_shared<SecTradingApi>();
    g_logger->info("创建 SecTradingApi 实例");
    
    // 2. 创建行情API实例
    auto market_api = std::make_shared<TdfMarketDataApi>();
    market_api->set_csv_path(config.get_csv_path());  // 从配置读取CSV路径
    g_logger->info("创建 TdfMarketDataApi 实例");
    
    // 3. 创建组合API
    auto combined_api = std::make_shared<TradingMarketApi>(trading_api, market_api);
    g_logger->info("创建 TradingMarketApi 组合实例");
    
    // 4. 连接交易服务 -  使用配置
    g_logger->info("连接交易服务: " + config.get_trading_host() + ":" + std::to_string(config.get_trading_port()));
    bool trade_ok = trading_api->connect(
        config.get_config_section(),  // 使用配置段名称而非IP地址
        config.get_trading_port(),
        config.get_trading_account(),
        config.get_trading_password()
    );
    if (!trade_ok) {
        g_logger->error("交易服务连接失败");
        return;
    }
    g_logger->info("交易服务连接成功");
    
    // 2. 连接行情服务
    g_logger->info("连接行情服务: " + config.get_market_host() + ":" + std::to_string(config.get_market_port()));
    bool market_ok = market_api->connect(
        config.get_market_host(),
        config.get_market_port(),
        config.get_market_user(),
        config.get_market_password()
    );
    if (!market_ok) {
        g_logger->error("行情服务连接失败");
        return;
    }
    g_logger->info("行情服务连接成功");
    
    // 6. 订阅股票行情（使用CSV中实际存在的股票）
    std::vector<std::string> symbols = {"605287.SH", "600158.SH"};  // 德才股份、中体产业
    g_logger->info("订阅股票行情: 605287.SH, 600158.SH");
    market_api->subscribe(symbols);
    
    // 7. 模拟查询行情和下单
    g_logger->info("--- 模拟交易流程 ---");
    g_logger->info("等待TDF数据推送（5秒）...");
    
    // 等待数据推送并查看实时输出
    for (int i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "等待中... " << (i + 1) << "/5 秒" << std::endl;
    }
    
    // 查询数据
    auto snapshot = market_api->get_snapshot("605287.SH");  // 查询CSV中存在的股票
    
    // 调试：打印查询结果
    std::cout << "[调试] 查询 605287.SH 结果: valid=" << snapshot.valid 
              << ", last_price=" << snapshot.last_price 
              << ", symbol=" << snapshot.symbol << std::endl;
    
    if (snapshot.valid) {
        g_logger->info("查询行情成功: 605287.SH 最新价=" + std::to_string(snapshot.last_price));
        
        // 模拟下单（实际不执行）
        OrderRequest order;
        order.symbol = "605287.SH";
        order.volume = 100;
        order.price = snapshot.last_price;
        order.is_market = false;
        
        g_logger->info("准备下单: " + order.symbol + 
                      ", 数量=" + std::to_string(order.volume) + 
                      ", 价格=" + std::to_string(order.price));
        
        // TODO: 取消注释以实际下单
        // std::string order_id = combined_api->place_order(order);
        // if (!order_id.empty()) {
        //     g_logger->info("下单成功, order_id=" + order_id);
        // } else {
        //     g_logger->error("下单失败");
        // }
        
        g_logger->warn("下单功能未启用（等待交易配置）");
    } else {
        g_logger->warn("行情数据无效或未推送");
    }
    
    g_logger->info("基础使用示例完成");
    g_logger->flush();  // 示例结束时手动flush
    g_logger->clear_context();
}

void example_separate_connections(const ConfigReader& config) {
    g_logger->set_context("example_separate_connections");
    g_logger->info("=== 独立连接示例 ===");
    
    // 分别管理交易和行情连接
    auto trading_api = std::make_shared<SecTradingApi>();
    auto market_api = std::make_shared<TdfMarketDataApi>();
    market_api->set_csv_path(config.get_csv_path());  // 从配置读取CSV路径
    
    // 交易账号 - 使用配置
    g_logger->info("连接交易服务: " + config.get_trading_host());
    if (!trading_api->connect(config.get_config_section(), config.get_trading_port(), 
                             config.get_trading_account(), config.get_trading_password())) {
        g_logger->error("交易连接失败");
        return;
    }
    
    // 行情账号 - 使用配置
    g_logger->info("连接行情服务: " + config.get_market_host());
    if (!market_api->connect(config.get_market_host(), config.get_market_port(),
                            config.get_market_user(), config.get_market_password())) {
        g_logger->error("行情连接失败");
        return;
    }
    
    // 订阅股票行情
    std::vector<std::string> symbols = {"600000.SH", "000001.SZ", "600519.SH"};
    g_logger->info("订阅" + std::to_string(symbols.size()) + "只股票");
    market_api->subscribe(symbols);
    
    // 查询行情
    auto snapshot = market_api->get_snapshot("600000.SH");
    if (snapshot.valid) {
        g_logger->info("股票: " + snapshot.symbol + 
                      ", 最新价: " + std::to_string(snapshot.last_price) + 
                      ", 昨收: " + std::to_string(snapshot.pre_close));
        
        // 根据行情决定是否下单
        double change_pct = (snapshot.last_price / snapshot.pre_close - 1.0) * 100.0;
        if (change_pct > 5.0) {
            g_logger->warn("涨幅超过5% (" + std::to_string(change_pct) + "%), 触发卖出策略");
            
            OrderRequest order;
            order.symbol = snapshot.symbol;
            order.volume = 100;
            order.price = snapshot.bid_price1;  // 使用买一价
            order.is_market = false;
            
            g_logger->info("模拟下单: " + order.symbol + 
                          ", 数量=" + std::to_string(order.volume) + 
                          ", 价格=" + std::to_string(order.price));
            
            // TODO: 实际下单
            // trading_api->place_order(order);
        } else {
            g_logger->info("涨幅=" + std::to_string(change_pct) + "%, 未触发策略");
        }
    } else {
        g_logger->warn("行情数据无效");
    }
    
    g_logger->info("独立连接示例完成");
    g_logger->flush();
    g_logger->clear_context();
}

void example_market_data_only(const ConfigReader& config) {
    g_logger->set_context("example_market_data_only");
    g_logger->info("=== 纯行情使用示例 ===");
    
    // 只使用行情API，不需要交易功能
    auto market_api = std::make_shared<TdfMarketDataApi>();
    market_api->set_csv_path(config.get_csv_path());  // 从配置读取CSV路径
    
    // 连接行情服务 -  使用配置
    g_logger->info("连接TDF行情服务: " + config.get_market_host() + ":" + std::to_string(config.get_market_port()));
    if (!market_api->connect(config.get_market_host(), config.get_market_port(),
                            config.get_market_user(), config.get_market_password())) {
        g_logger->error("行情连接失败");
        return;
    }
    g_logger->info("行情连接成功");
    
    // 从CSV文件加载所有股票代码
    std::string csv_path = config.get_csv_path();
    std::vector<std::string> symbols = load_symbols_from_csv(csv_path);
    
    if (symbols.empty()) {
        g_logger->error("CSV文件中没有读取到任何股票，退出测试");
        return;
    }
    
    g_logger->info("订阅 " + std::to_string(symbols.size()) + " 只股票（来自CSV: " + csv_path + "）");
    market_api->subscribe(symbols);
    
    g_logger->info("等待行情数据推送...");
    std::this_thread::sleep_for(std::chrono::seconds(3));   // 等待3秒后检查
    
    g_logger->info("首次检查快照...");
    auto test_snap = market_api->get_snapshot("605287.SH");
    if (test_snap.valid) {
        g_logger->info("605287.SH 快照已收到: 涨停=" + std::to_string(test_snap.up_limit));
    } else {
        g_logger->warn("605287.SH 快照尚未收到，继续等待...");
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(12));  // 再等待12秒（总共15秒）
    
    // 查询所有订阅股票的快照
    g_logger->info("--- 行情快照 ---");
    for (const auto& symbol : symbols) {
        auto snapshot = market_api->get_snapshot(symbol);
        if (snapshot.valid) {
            double change = snapshot.last_price - snapshot.pre_close;
            double change_pct = (change / snapshot.pre_close) * 100.0;
            
            std::stringstream ss;
            ss << std::fixed << std::setprecision(2);
            ss << symbol << ": "
               << "最新=" << snapshot.last_price
               << ", 涨跌=" << std::showpos << change << std::noshowpos
               << ", 涨幅=" << std::showpos << change_pct << std::noshowpos << "%"
               << ", 涨停=" << snapshot.up_limit
               << ", 跌停=" << snapshot.down_limit;
            
            g_logger->info(ss.str());
        } else {
            g_logger->warn(symbol + ": 行情数据无效或未推送");
        }
    }
    
    // 清理
    g_logger->info("断开行情连接");
    market_api->disconnect();
    g_logger->info("纯行情示例完成");
    g_logger->flush();
    g_logger->clear_context();
}

void example_with_strategy(const ConfigReader& config) {
    g_logger->set_context("example_with_strategy");
    g_logger->info("=== 策略集成示例（IntradaySellStrategy）===");
    
    // 1. 创建API实例
    auto trading_api = std::make_shared<SecTradingApi>();
    auto market_api = std::make_shared<TdfMarketDataApi>();
    market_api->set_csv_path(config.get_csv_path());  // 从配置读取CSV路径
    auto combined_api = std::make_shared<TradingMarketApi>(trading_api, market_api);
    
    g_logger->info("创建API实例完成");
    
    // 2. 连接交易服务 -  使用配置
    g_logger->info("连接交易服务: " + config.get_trading_host() + ":" + std::to_string(config.get_trading_port()));
    bool trade_ok = trading_api->connect(
        config.get_config_section(),  // 使用配置段名称而非IP地址
        config.get_trading_port(),
        config.get_trading_account(),
        config.get_trading_password()
    );
    if (!trade_ok) {
        g_logger->error("交易服务连接失败");
        return;
    }
    g_logger->info("交易服务连接成功");
    
    // 3. 连接行情服务 -  使用配置
    g_logger->info("连接行情服务: " + config.get_market_host() + ":" + std::to_string(config.get_market_port()));
    bool market_ok = market_api->connect(
        config.get_market_host(),
        config.get_market_port(),
        config.get_market_user(),
        config.get_market_password()
    );
    if (!market_ok) {
        g_logger->error("行情服务连接失败");
        return;
    }
    g_logger->info("行情服务连接成功");
    
    // 4. 创建策略实例 - 使用配置
    g_logger->info("--- 创建盘中卖出策略 ---");
    std::string csv_path = config.get_csv_path();
    std::string account_id = config.get_account_id();
    
    IntradaySellStrategy strategy(combined_api.get(), csv_path, account_id);
    g_logger->info("策略实例创建完成 (CSV: " + csv_path + ", Account: " + account_id + ")");
    
    // 5. 初始化策略
    g_logger->info("初始化策略...");
    if (!strategy.init()) {
        g_logger->error("策略初始化失败");
        return;
    }
    g_logger->info("策略初始化成功");
    
    // 6. 模拟定时器执行（每3秒调用一次 on_timer）
    g_logger->info("--- 开始策略定时执行 ---");
    g_logger->info("模拟运行10次定时器（每次间隔3秒）");
    
    for (int i = 0; i < 10; i++) {
        g_logger->info("Timer #" + std::to_string(i + 1));
        
        // 调用策略定时器
        strategy.on_timer();
        
        // 打印策略状态
        strategy.print_status();
        
        // 等待3秒（模拟实际定时器间隔）
        if (i < 9) {  // 最后一次不等待
            g_logger->info("等待3秒...");
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
    
    g_logger->info("策略测试完成");
    g_logger->flush();
    g_logger->clear_context();
}

int main() {
    // 初始化日志（文件名会自动包含日期）
    std::cerr << "[main] Logger初始化..." << std::endl;
    g_logger.reset(new Logger("trading_test"));
    std::cerr << "[main] Logger初始化完成" << std::endl;
    
    g_logger->info("======================================");
    g_logger->info("SecTradingApi + TdfMarketDataApi 测试");
    g_logger->info("======================================");
    g_logger->info("");
    
    //  加载配置文件
    ConfigReader config;
    if (!config.load("config.json")) {
        g_logger->error("配置文件加载失败！请检查 config.json 是否存在");
        return 1;
    }
    
    // 验证配置
    std::string trading_host = config.get_trading_host();
    std::string account = config.get_trading_account();
    bool has_trading_config = !trading_host.empty() && !account.empty() && 
                              trading_host != "待填写交易服务器地址" && 
                              account != "待填写账号";
    
    if (!has_trading_config) {
        g_logger->warn("交易配置未填写，将只运行行情测试");
    }
    
    g_logger->info(" 配置加载成功:");
    if (has_trading_config) {
        g_logger->info("  交易服务器: " + trading_host + ":" + std::to_string(config.get_trading_port()));
    } else {
        g_logger->info("  交易服务器: <未配置>");
    }
    g_logger->info("  行情服务器: " + config.get_market_host() + ":" + std::to_string(config.get_market_port()));
    g_logger->info("  策略CSV: " + config.get_csv_path());
    g_logger->info("");
    
    try {
        // ============================================================
        // 智能模式：根据配置自动选择运行模式
        // ============================================================
        
        if (!has_trading_config) {
            // 【模式1】无交易配置 - 纯行情数据测试
            g_logger->info("========================================");
            g_logger->info("模式1: 纯行情数据测试（无需交易配置）");
            g_logger->info("========================================");
            example_market_data_only(config);
            g_logger->info("");
            
        } else {
            // 有交易配置 - 询问用户选择模式
            g_logger->warn("===== 检测到交易配置 =====");
            g_logger->warn("可用模式:");
            g_logger->warn("  [1] DRY-RUN模式 - 测试交易连接（跌停价买入后立即撤单，不会实际成交）");
            g_logger->warn("  [2] 生产模式 - 真实交易（执行实盘卖出策略）");
            g_logger->warn("请在代码中设置 RUN_MODE:");
            g_logger->warn("  const int RUN_MODE = 1;  // 1=DRY-RUN, 2=生产模式");
            g_logger->warn("============================");
            
            // *** 在这里设置运行模式 ***
            const int RUN_MODE = 2;  // 1=DRY-RUN测试, 2=真实交易
            
            if (RUN_MODE == 1) {
                // 【模式2】DRY-RUN模式 - 测试交易连接
                g_logger->info("========================================");
                g_logger->info("模式2: DRY-RUN测试模式");
                g_logger->info("========================================");
                g_logger->info("将使用跌停价买入后立即撤单（不会实际成交）");
                g_logger->info("用于测试交易API连接是否正常");
                g_logger->info("");
                
                // 创建API实例
                auto trading_api = std::make_shared<SecTradingApi>();
                auto market_api = std::make_shared<TdfMarketDataApi>();
                market_api->set_csv_path(config.get_csv_path());  // 从配置读取CSV路径
                auto combined_api = std::make_shared<TradingMarketApi>(trading_api, market_api);
                
                // 启用 DRY-RUN 模式
                trading_api->set_dry_run(true);
                
                // 连接交易服务
                g_logger->info("连接交易服务...");
                bool trade_ok = trading_api->connect(
                    config.get_config_section(),  // 使用配置段名称而非IP地址
                    config.get_trading_port(),
                    config.get_trading_account(),
                    config.get_trading_password()
                );
                if (!trade_ok) {
                    g_logger->error("交易服务连接失败！");
                    return 1;
                }
                g_logger->info("✓ 交易服务连接成功");
                
                // 连接行情服务
                g_logger->info("连接行情服务...");
                bool market_ok = market_api->connect(
                    config.get_market_host(),
                    config.get_market_port(),
                    config.get_market_user(),
                    config.get_market_password()
                );
                if (!market_ok) {
                    g_logger->error("行情服务连接失败！");
                    return 1;
                }
                g_logger->info("✓ 行情服务连接成功");
                
                // 创建策略并运行（DRY-RUN模式）
                g_logger->info("创建盘中卖出策略（DRY-RUN模式）...");
                IntradaySellStrategy strategy(combined_api.get(), 
                                              config.get_csv_path(), 
                                              config.get_account_id());
                if (!strategy.init()) {
                    g_logger->error("策略初始化失败！");
                    return 1;
                }
                g_logger->info("✓ 策略初始化成功");
                
                // 模拟运行3次定时器
                g_logger->info("--- 开始DRY-RUN测试 (运行3次) ---");
                for (int i = 0; i < 3; i++) {
                    g_logger->info("Timer #" + std::to_string(i + 1));
                    strategy.on_timer();
                    strategy.print_status();
                    if (i < 2) {
                        g_logger->info("等待3秒...");
                        std::this_thread::sleep_for(std::chrono::seconds(3));
                    }
                }
                
                g_logger->info("✓ DRY-RUN测试完成，交易API连接正常！");
                g_logger->info("");
                
            } else if (RUN_MODE == 2) {
                // 【模式3】生产模式 - 真实交易
                g_logger->warn("========================================");
                g_logger->warn("模式3: 生产模式 - 真实交易");
                g_logger->warn("========================================");
                g_logger->warn("策略将执行真实交易！");
                g_logger->warn("请确认以下配置正确:");
                g_logger->warn("  - CSV文件: " + config.get_csv_path());
                g_logger->warn("  - 交易账号: " + config.get_trading_account());
                g_logger->warn("  - 交易服务器: " + config.get_trading_host());
                g_logger->warn("如需取消，请在5秒内按Ctrl+C终止程序");
                g_logger->warn("========================================");
                
                // 倒计时确认
                for (int i = 5; i > 0; --i) {
                    g_logger->info("倒计时: " + std::to_string(i) + " 秒...");
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                
                g_logger->info("开始执行真实交易策略...");
                example_with_strategy(config);
                g_logger->info("");
                
            } else {
                g_logger->error("无效的 RUN_MODE 设置: " + std::to_string(RUN_MODE));
                g_logger->error("请设置 RUN_MODE = 1 (DRY-RUN) 或 2 (生产模式)");
                return 1;
            }
        }
        
    } catch (const std::exception& e) {
        g_logger->error("发生异常: " + std::string(e.what()));
        return 1;
    }
    
    g_logger->info("所有测试完成");
    return 0;
}
