// 使用示例：SecTradingApi + TdfMarketDataApi + IntradaySellStrategy
// 演示完整的交易、行情和策略功能

#include "SecTradingApi.h"
#include "TdfMarketDataApi.h"
#include "TradingMarketApi.h"
#include "../src/strategies/IntradaySellStrategy.h"  // 使用strategies目录中的版本
#include "../src/strategies/AuctionSellStrategy.h"
#include "../src/strategies/CloseSellStrategy.h"
#include "../src/core/ConfigReader.h"  // 配置文件读取器
#include "TeeStream.h"

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
#include <atomic>
#include <csignal>
#include <fstream>
#include<iostream>
#ifdef _WIN32
#include <direct.h>  // Windows: _mkdir
#include <windows.h>  // Windows: FindFirstFile, FindNextFile
#define MKDIR(dir) _mkdir(dir)
#else
#include <sys/stat.h>  // Linux: mkdir
#include <dirent.h>   // Linux: opendir, readdir
#include <unistd.h>   // Linux: getpid
#define MKDIR(dir) mkdir(dir, 0755)
#endif

// 辅助函数：查找目录中最新的CSV文件
std::string find_latest_csv(const std::string& directory = ".") {
    std::string latest_csv;
    
#ifdef _WIN32
    // Windows 实现
    WIN32_FIND_DATAA find_data;
    std::string search_path = directory + "\\*.csv";
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        std::cerr << "[CSV查找] 未找到CSV文件: " << directory << std::endl;
        return "";
    }
    
    FILETIME latest_time = {0, 0};
    do {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            // 比较文件修改时间
            if (CompareFileTime(&find_data.ftLastWriteTime, &latest_time) > 0) {
                latest_time = find_data.ftLastWriteTime;
                latest_csv = find_data.cFileName;
            }
        }
    } while (FindNextFileA(hFind, &find_data) != 0);
    
    FindClose(hFind);
#else
    // Linux 实现
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        std::cerr << "[CSV查找] 无法打开目录: " << directory << std::endl;
        return "";
    }
    
    struct dirent* entry;
    time_t latest_time = 0;
    
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".csv") {
            std::string full_path = directory + "/" + filename;
            struct stat file_stat;
            if (stat(full_path.c_str(), &file_stat) == 0) {
                if (file_stat.st_mtime > latest_time) {
                    latest_time = file_stat.st_mtime;
                    latest_csv = filename;
                }
            }
        }
    }
    
    closedir(dir);
#endif
    
    if (!latest_csv.empty()) {
        std::cout << "[CSV查找] 找到最新CSV文件: " << latest_csv << std::endl;
        return latest_csv;
    } else {
        std::cerr << "[CSV查找] 目录中没有CSV文件: " << directory << std::endl;
        return "";
    }
}

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
    Logger(const std::string& log_name = "trading_test_data") {
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
static std::atomic<bool> g_running{true};
static std::unique_ptr<TeeStream> g_tee_cout;
static std::unique_ptr<TeeStream> g_tee_cerr;
static std::ofstream g_all_log;

static std::string get_date_yyyymmdd() {
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

static int get_process_id() {
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

static void ensure_log_dir(const std::string& dir) {
    MKDIR(dir.c_str());
}

void handle_termination_signal(int /*signal*/) {
    g_running.store(false);
}

std::string resolve_config_path() {
    const std::vector<std::string> candidates = {
        "config.json",
        "./config.json",
        "./result/config.json",
        "../config.json",
        "../result/config.json"
    };
    for (const auto& path : candidates) {
        std::ifstream f(path);
        if (f.good()) {
            std::cerr << "[Config] 使用配置文件: " << path << std::endl;
            return path;
        }
    }
    return "";
}


void example_market_data_only(const ConfigReader& config, const std::string& csv_path) {
    g_logger->set_context("example_market_data_only");
    g_logger->info("=== 纯行情使用示例（含逐笔成交）===");
    
    // 只使用行情API，不需要交易功能
    auto market_api = std::make_shared<TdfMarketDataApi>();
    market_api->set_csv_path(csv_path);  // 使用实际的CSV路径
    
    // 设置逐笔成交回调 - 打印每笔成交
    std::atomic<int> transaction_count{0};
    const int max_print = 100;  // 最多打印100条逐笔，避免刷屏
    
    market_api->set_transaction_callback([&transaction_count, max_print](const TransactionData& td) {
        int count = transaction_count.fetch_add(1);
        if (count < max_print) {
            // 解析时间
            int time_raw = td.timestamp;
            int hour = time_raw / 10000000;
            int minute = (time_raw / 100000) % 100;
            int second = (time_raw / 1000) % 100;
            int ms = time_raw % 1000;
            
            // 买卖方向
            const char* bs_str = (td.bsf_flag == 1) ? "B" : 
                                 (td.bsf_flag == 2) ? "S" : "-";
            
            char buf[256];
            snprintf(buf, sizeof(buf), 
                "[逐笔成交] %s %02d:%02d:%02d.%03d 价格=%.2f 量=%d 额=%.0f %s",
                td.symbol.c_str(), hour, minute, second, ms,
                td.price, td.volume, td.turnover, bs_str);
            std::cout << buf << std::endl;
        } else if (count == max_print) {
            std::cout << "[逐笔成交] ... 已打印 " << max_print 
                      << " 条，后续省略 ..." << std::endl;
        }
    });
    
    // 连接行情服务 -  使用配置
    g_logger->info("连接TDF行情服务: " + config.get_market_host() + ":" + std::to_string(config.get_market_port()));
    if (!market_api->connect(config.get_market_host(), config.get_market_port(),
                            config.get_market_user(), config.get_market_password())) {
        g_logger->error("行情连接失败");
        return;
    }
    g_logger->info("行情连接成功");
    
    // 从CSV文件加载所有股票代码
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
    g_logger->info("--- 逐笔成交统计 ---");
    g_logger->info("共收到 " + std::to_string(transaction_count.load()) + " 条逐笔成交数据");
    g_logger->info("断开行情连接");
    market_api->disconnect();
    g_logger->info("纯行情示例完成");
    g_logger->flush();
    g_logger->clear_context();
}

void example_with_strategy(const ConfigReader& config, const std::string& csv_path) {
    g_logger->set_context("example_with_strategy");
    g_logger->info("=== 策略集成示例（竞价+盘中+收盘）===");
    if (!g_running.load()) {
        g_logger->warn("检测到终止信号，取消策略启动");
        g_logger->clear_context();
        return;
    }
    
    // 1. 创建API实例
    auto trading_api = std::make_shared<SecTradingApi>();
    auto market_api = std::make_shared<TdfMarketDataApi>();
    market_api->set_csv_path(csv_path);  // 使用实际的CSV路径
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
    g_logger->info("--- 创建竞价/盘中/收盘策略 ---");
    std::string account_id = config.get_account_id();
    double sell_to_mkt_ratio = config.get_strategy_sell_to_mkt_ratio();
    double phase1_sell_ratio = config.get_strategy_phase1_sell_ratio();
    double input_amt = config.get_strategy_input_amt();
    int64_t hold_vol = config.get_strategy_hold_vol();
    
    IntradaySellStrategy intraday_strategy(combined_api.get(), csv_path, account_id, hold_vol, input_amt);
    AuctionSellStrategy auction_strategy(combined_api.get(), csv_path, account_id, sell_to_mkt_ratio, phase1_sell_ratio, hold_vol);
    CloseSellStrategy close_strategy(combined_api.get(), account_id, hold_vol);
    g_logger->info("策略实例创建完成 (CSV: " + csv_path + ", Account: " + account_id + ")");
    
    // 5. 初始化策略
    g_logger->info("初始化盘中策略...");
    if (!intraday_strategy.init()) {
        g_logger->error("盘中策略初始化失败");
        return;
    }
    g_logger->info("初始化竞价策略...");
    if (!auction_strategy.init()) {
        g_logger->error("竞价策略初始化失败");
        return;
    }
    g_logger->info("初始化收盘策略...");
    if (!close_strategy.init()) {
        g_logger->error("收盘策略初始化失败");
        return;
    }
    g_logger->info("全部策略初始化成功");
    
    // 6. 进入实时循环
    g_logger->info("--- 启动策略定时循环（Ctrl+C 可安全退出）---");
    const auto timer_interval = std::chrono::seconds(1);
    const auto status_interval = std::chrono::minutes(1);
    auto last_status = std::chrono::steady_clock::now();
    
    while (g_running.load()) {
        intraday_strategy.on_timer();
        auction_strategy.on_timer();
        close_strategy.on_timer();
        
        auto now = std::chrono::steady_clock::now();
        if (now - last_status >= status_interval) {
            g_logger->info("--- 策略状态快照 ---");
            intraday_strategy.print_status();
            auction_strategy.print_status();
            close_strategy.print_status();
            last_status = now;
        }
        
        std::this_thread::sleep_for(timer_interval);
    }
    
    g_logger->info("检测到终止信号，开始整理状态");
    intraday_strategy.print_status();
    auction_strategy.print_status();
    close_strategy.print_status();
    g_logger->info("断开交易与行情连接...");
    market_api->disconnect();
    trading_api->disconnect();
    g_logger->info("策略循环结束");
    g_logger->flush();
    g_logger->clear_context();
}

int main() {
    // 统一输出到文件 + 控制台
    const std::string log_dir = "./log";
    ensure_log_dir(log_dir);
    const std::string log_name_base = "trading_test_data_pid" + std::to_string(get_process_id());
    const std::string all_log_path = log_dir + "/" + log_name_base + "_" + get_date_yyyymmdd() + ".log";
    g_all_log.open(all_log_path.c_str(), std::ios::app);
    if (g_all_log.is_open()) {
        g_tee_cout.reset(new TeeStream(std::cout, g_all_log.rdbuf()));
        g_tee_cerr.reset(new TeeStream(std::cerr, g_all_log.rdbuf()));
    }

    // 初始化日志（文件名会自动包含日期）
    std::cerr << "[main] Logger初始化..." << std::endl;
    g_logger.reset(new Logger(log_name_base));
    std::cerr << "[main] Logger初始化完成" << std::endl;

    std::signal(SIGINT, handle_termination_signal);
#ifdef SIGTERM
    std::signal(SIGTERM, handle_termination_signal);
#endif
#ifdef SIGBREAK
    std::signal(SIGBREAK, handle_termination_signal);
#endif
    g_running.store(true);
    
    g_logger->info("======================================");
    g_logger->info("SecTradingApi + TdfMarketDataApi 测试");
    g_logger->info("======================================");
    g_logger->info("");
    
    //  加载配置文件
    auto config_path = resolve_config_path();
    if (config_path.empty()) {
        g_logger->error("未找到配置文件，请确认工作目录或手动指定 config.json 路径");
        return 1;
    }

    ConfigReader config;
    if (!config.load(config_path)) {
        g_logger->error("配置文件加载失败！请检查 " + config_path + " 是否存在且格式正确");
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
    
    // 检查并自动查找最新CSV文件
    std::string csv_path = config.get_csv_path();
    bool csv_valid = false;
    
    // 检查配置中的CSV文件是否存在
    if (!csv_path.empty()) {
        std::ifstream csv_test(csv_path);
        csv_valid = csv_test.good();
        csv_test.close();
    }
    
    // 如果配置的CSV不存在或为空，自动查找最新的CSV
    if (!csv_valid) {
        g_logger->warn("配置的CSV文件不存在或未配置: " + csv_path);
        g_logger->info("正在自动查找CSV文件...");
        
        // 尝试多个目录查找CSV文件
        std::vector<std::string> search_dirs = {".", "./result", "../", "../result"};
        std::string latest_csv;
        for (const auto& dir : search_dirs) {
            latest_csv = find_latest_csv(dir);
            if (!latest_csv.empty()) {
                // 如果不是当前目录，需要加上目录前缀
                if (dir != ".") {
                    csv_path = dir + "/" + latest_csv;
                } else {
                    csv_path = latest_csv;
                }
                g_logger->info("✓ 在目录 " + dir + " 找到CSV: " + csv_path);
                break;
            }
        }
        
        if (latest_csv.empty()) {
            g_logger->error("未找到任何CSV文件，程序无法继续");
            return 1;
        }
    } else {
        g_logger->info("使用配置文件中指定的CSV: " + csv_path);
    }
    
    g_logger->info(" 配置加载成功:");
    if (has_trading_config) {
        g_logger->info("  交易服务器: " + trading_host + ":" + std::to_string(config.get_trading_port()));
    } else {
        g_logger->info("  交易服务器: <未配置>");
    }
    g_logger->info("  行情服务器: " + config.get_market_host() + ":" + std::to_string(config.get_market_port()));
    g_logger->info("  策略CSV: " + csv_path);
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
            example_market_data_only(config, csv_path);
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
                market_api->set_csv_path(csv_path);  // 使用实际的CSV路径
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
                int64_t hold_vol = config.get_strategy_hold_vol();
                double input_amt = config.get_strategy_input_amt();
                IntradaySellStrategy strategy(combined_api.get(), 
                                              csv_path, 
                                              config.get_account_id(),
                                              hold_vol,
                                              input_amt);
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
                g_logger->warn("  - CSV文件: " + csv_path);
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
                example_with_strategy(config, csv_path);
                if (!g_running.load()) {
                    g_logger->warn("检测到终止信号，程序已按请求停止");
                    return 0;
                }
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
    
    if (!g_running.load()) {
        g_logger->warn("检测到终止信号，提前结束程序");
        return 0;
    }

    g_logger->info("所有测试完成");
    return 0;
}
