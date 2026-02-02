// Multi-module runner:
// - 1x SecTradingApi + 1x TdfMarketDataApi
// - Modules run on parallel threads (tick loops)
// - All trading calls are serialized via QueuedTradingApi
// - Market subscription is merged once at startup (TDF does not support runtime changes)
// - Order callbacks are routed by remark prefix:
//   * "qh2h_sell_"        -> Qh2hSellModule
//   * "qh2h_base_cancel_" -> BaseCancelModule
//   * (external orders / empty remark) -> BaseCancelModule (for monitoring)

#include "SecTradingApi.h"
#include "TdfMarketDataApi.h"
#include "ImprovedLogger.h"

#include "src/core/AppContext.h"
#include "src/core/ConfigReader.h"
#include "src/core/QueuedTradingApi.h"

#include "src/modules/BaseCancelModule.h"
#include "src/modules/Qh2hSellModule.h"
#include "src/modules/UsageExampleModule.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cctype>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <deque>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#ifndef MKDIR
#define MKDIR(dir) _mkdir(dir)
#endif
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef MKDIR
#define MKDIR(dir) mkdir(dir, 0755)
#endif
#endif

namespace {

std::atomic<bool>* g_stop_flag = nullptr;

void handle_signal(int) {
    if (g_stop_flag) {
        g_stop_flag->store(true);
    }
}

std::string trim_copy(const std::string& input) {
    size_t start = input.find_first_not_of(" \t\r\n\"");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = input.find_last_not_of(" \t\r\n\"");
    return input.substr(start, end - start + 1);
}

bool is_six_digit_code(const std::string& token) {
    if (token.size() != 6) {
        return false;
    }
    for (char ch : token) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

std::string extract_code_from_symbol(const std::string& symbol) {
    size_t dot = symbol.find('.');
    if (dot == std::string::npos) {
        return symbol;
    }
    return symbol.substr(0, dot);
}

std::string to_wind_symbol(const std::string& code) {
    if (!is_six_digit_code(code)) {
        return "";
    }
    if (code.rfind("00", 0) == 0 || code.rfind("30", 0) == 0) {
        return code + ".SZ";
    }
    if (code.rfind("60", 0) == 0 || code.rfind("68", 0) == 0) {
        return code + ".SH";
    }
    if (code.rfind("6", 0) == 0) {
        return code + ".SH";
    }
    return code + ".SZ";
}

bool pass_code_filter(const std::string& code,
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

void ensure_dir(const std::string& dir) {
    if (dir.empty()) {
        return;
    }
    MKDIR(dir.c_str());
}

std::string resolve_config_path() {
    const std::vector<std::string> candidates = {
        "config.json",
        "./config.json",
        "../config.json",
        "./result/config.json",
        "../result/config.json",
    };
    for (const auto& path : candidates) {
        std::ifstream file(path.c_str());
        if (file.good()) {
            return path;
        }
    }
    return "";
}

// Find latest *.csv by last write time in a directory, return full path.
std::string find_latest_csv_in_dir(const std::string& directory) {
#ifdef _WIN32
    WIN32_FIND_DATAA find_data;
    std::string search_path = directory + "\\*.csv";
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return "";
    }

    FILETIME latest_time = {0, 0};
    std::string latest_name;
    do {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (CompareFileTime(&find_data.ftLastWriteTime, &latest_time) > 0) {
            latest_time = find_data.ftLastWriteTime;
            latest_name = find_data.cFileName;
        }
    } while (FindNextFileA(hFind, &find_data) != 0);
    FindClose(hFind);

    if (latest_name.empty()) {
        return "";
    }
    return directory + "\\" + latest_name;
#else
    DIR* dir = opendir(directory.c_str());
    if (!dir) {
        return "";
    }

    std::string latest_name;
    time_t latest_time = 0;
    while (auto* entry = readdir(dir)) {
        std::string filename = entry->d_name;
        if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".csv") {
            continue;
        }
        std::string full_path = directory + "/" + filename;
        struct stat file_stat;
        if (stat(full_path.c_str(), &file_stat) != 0) {
            continue;
        }
        if (file_stat.st_mtime > latest_time) {
            latest_time = file_stat.st_mtime;
            latest_name = filename;
        }
    }
    closedir(dir);

    if (latest_name.empty()) {
        return "";
    }
    return directory + "/" + latest_name;
#endif
}

std::vector<std::string> load_symbols_from_csv(const std::string& csv_path) {
    std::vector<std::string> symbols;
    std::ifstream file(csv_path.c_str());
    if (!file.is_open()) {
        return symbols;
    }

    std::string line;
    bool first_line = true;
    while (std::getline(file, line)) {
        if (first_line) {
            first_line = false;
            continue;
        }
        if (line.empty()) {
            continue;
        }

        std::stringstream ss(line);
        std::string token;
        int col = 0;
        std::string code;
        while (std::getline(ss, token, ',')) {
            if (col == 2) {
                code = trim_copy(token);
                break;
            }
            col++;
        }
        if (!is_six_digit_code(code)) {
            continue;
        }
        std::string sym = to_wind_symbol(code);
        if (!sym.empty()) {
            symbols.push_back(sym);
        }
    }

    std::sort(symbols.begin(), symbols.end());
    symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
    return symbols;
}

// Find latest list file by YYYYMMDD prefix before '_' and extract symbols from it.
std::vector<std::string> load_buy_list_symbols(const std::string& dir,
                                               const std::string& min_code,
                                               const std::string& max_code,
                                               std::string* out_path) {
    std::vector<std::string> symbols;

    auto list_files = [&](const std::string& d) -> std::vector<std::string> {
        std::vector<std::string> files;
#ifdef _WIN32
        std::string pattern = d + "\\*";
        WIN32_FIND_DATAA data;
        HANDLE h = FindFirstFileA(pattern.c_str(), &data);
        if (h == INVALID_HANDLE_VALUE) {
            return files;
        }
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            files.push_back(data.cFileName);
        } while (FindNextFileA(h, &data) != 0);
        FindClose(h);
#else
        DIR* dp = opendir(d.c_str());
        if (!dp) {
            return files;
        }
        while (auto* ent = readdir(dp)) {
            if (ent->d_type == DT_DIR) {
                continue;
            }
            files.push_back(ent->d_name);
        }
        closedir(dp);
#endif
        return files;
    };

    auto parse_ymd = [](const std::string& token) -> int {
        if (token.size() != 8) {
            return 0;
        }
        for (char c : token) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return 0;
            }
        }
        return std::atoi(token.c_str());
    };

    int latest_date = 0;
    std::string latest_file;
    for (const auto& name : list_files(dir)) {
        size_t underscore = name.find('_');
        if (underscore == std::string::npos) {
            continue;
        }
        int date = parse_ymd(name.substr(0, underscore));
        if (date > latest_date) {
            latest_date = date;
            latest_file = name;
        }
    }
    if (latest_file.empty()) {
        if (out_path) {
            *out_path = "";
        }
        return symbols;
    }

#ifdef _WIN32
    std::string path = dir + "\\" + latest_file;
#else
    std::string path = dir + "/" + latest_file;
#endif
    if (out_path) {
        *out_path = path;
    }

    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        return symbols;
    }

    std::unordered_set<std::string> dedup;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        std::stringstream ss(line);
        std::string raw;
        std::string code;
        while (std::getline(ss, raw, ',')) {
            code = trim_copy(raw);
            if (code.size() >= 9 && code[6] == '.') {
                code = code.substr(0, 6);
            }
            if (is_six_digit_code(code)) {
                break;
            }
            code.clear();
        }
        if (!is_six_digit_code(code)) {
            continue;
        }
        if (!pass_code_filter(code, min_code, max_code)) {
            continue;
        }
        std::string sym = to_wind_symbol(code);
        if (sym.empty()) {
            continue;
        }
        if (dedup.insert(sym).second) {
            symbols.push_back(sym);
        }
    }

    std::sort(symbols.begin(), symbols.end());
    symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
    return symbols;
}

bool write_subscribe_csv(const std::vector<std::string>& symbols, const std::string& path) {
    std::ofstream out(path.c_str());
    if (!out.is_open()) {
        return false;
    }
    out << "idx,shortname,SYMBOL\n";
    for (size_t i = 0; i < symbols.size(); ++i) {
        std::string code = extract_code_from_symbol(symbols[i]);
        if (!is_six_digit_code(code)) {
            continue;
        }
        out << i << ",," << code << "\n";
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    auto main_logger = std::make_shared<ImprovedLogger>("runner", "./log", LogLevel::INFO);
    main_logger->info("========== multi-module runner start ==========");

    std::signal(SIGINT, handle_signal);
#ifdef SIGTERM
    std::signal(SIGTERM, handle_signal);
#endif

    std::string config_path = resolve_config_path();
    if (config_path.empty()) {
        main_logger->error("config.json not found in working directory");
        return 1;
    }

    ConfigReader config;
    if (!config.load(config_path)) {
        main_logger->error("failed to load config: " + config_path);
        return 1;
    }

    const std::string config_section = config.get_config_section().empty()
                                           ? config.get_trading_host()
                                           : config.get_config_section();
    const int trading_port = config.get_trading_port();
    const std::string trading_account = config.get_trading_account();
    const std::string trading_password = config.get_trading_password();

    if (config_section.empty() || trading_account.empty()) {
        main_logger->error("missing trading config_section/account");
        return 1;
    }

    const int enable_sell = config.get_module_sell();
    const int enable_base_cancel = config.get_module_base_cancel();
    const int enable_usage = config.get_module_usage_example();

    main_logger->info_f("[CONFIG] modules sell=%d base_cancel=%d usage_example=%d",
                        enable_sell, enable_base_cancel, enable_usage);

    const std::string strategy_account_id = config.get_account_id();
    const int hold_vol = static_cast<int>(config.get_strategy_hold_vol(300));
    const std::string code_min = config.get_code_min();
    const std::string code_max = config.get_code_max();

    const std::string usage_dir = config.get_usage_example_csv_dir();
    const std::string base_cancel_dir = config.get_base_cancel_order_dir();

    const double sell_to_mkt_ratio = config.get_strategy_sell_to_mkt_ratio(0.1);
    const double phase1_sell_ratio = config.get_strategy_phase1_sell_ratio(0.1);
    const double input_amt = config.get_strategy_input_amt(600000.0);

    auto trading_raw = std::make_shared<SecTradingApi>();
    auto trading = std::make_shared<QueuedTradingApi>(trading_raw);
    if (!trading->connect(config_section, trading_port, trading_account, trading_password)) {
        main_logger->error("trading connect failed");
        return 1;
    }
    main_logger->info("trading connected");

    auto positions = trading->query_positions();

    std::unordered_set<std::string> subscribe_set;

    if (enable_sell) {
        for (const auto& pos : positions) {
            std::string code = extract_code_from_symbol(pos.symbol);
            if (!pass_code_filter(code, code_min, code_max)) {
                continue;
            }
            if (pos.available > hold_vol) {
                subscribe_set.insert(pos.symbol);
            }
        }
    }

    if (enable_base_cancel) {
        for (const auto& pos : positions) {
            std::string code = extract_code_from_symbol(pos.symbol);
            if (!pass_code_filter(code, code_min, code_max)) {
                continue;
            }
            subscribe_set.insert(pos.symbol);
        }
        std::string list_path;
        auto buy_symbols = load_buy_list_symbols(base_cancel_dir.empty() ? "./data/base_cancel" : base_cancel_dir,
                                                 code_min, code_max, &list_path);
        for (const auto& s : buy_symbols) {
            subscribe_set.insert(s);
        }
        if (!list_path.empty()) {
            main_logger->info("[SUB] base_cancel list: " + list_path);
        }
    }

    std::string usage_csv_file;
    if (enable_usage) {
        const std::string dir = usage_dir.empty() ? "./data/usage" : usage_dir;
        usage_csv_file = find_latest_csv_in_dir(dir);
        if (usage_csv_file.empty()) {
            main_logger->warn("[SUB] usage_example csv dir has no csv: " + dir);
        } else {
            auto usage_symbols = load_symbols_from_csv(usage_csv_file);
            for (const auto& s : usage_symbols) {
                subscribe_set.insert(s);
            }
            main_logger->info("[SUB] usage_example csv: " + usage_csv_file);
        }
    }

    std::vector<std::string> subscribe_symbols(subscribe_set.begin(), subscribe_set.end());
    std::sort(subscribe_symbols.begin(), subscribe_symbols.end());

    ensure_dir("./data");
    const std::string subscribe_csv = "./data/subscribe_all.csv";
    if (!write_subscribe_csv(subscribe_symbols, subscribe_csv)) {
        main_logger->error("failed to write subscribe csv: " + subscribe_csv);
        return 1;
    }
    main_logger->info_f("[SUB] merged %zu symbols -> %s", subscribe_symbols.size(), subscribe_csv.c_str());

    auto market = std::make_shared<TdfMarketDataApi>();
    market->set_csv_path(subscribe_csv);
    if (!market->connect(config.get_market_host(), config.get_market_port(),
                         config.get_market_user(), config.get_market_password())) {
        main_logger->error("market connect failed");
        return 1;
    }
    main_logger->info("market connected");

    AppContext ctx;
    ctx.trading_raw = trading_raw;
    ctx.trading = trading;
    ctx.market = market;
    g_stop_flag = &ctx.stop;

    struct OrderEvent {
        OrderResult result;
        int type;
        OrderEvent() : result(), type(0) {}
        OrderEvent(const OrderResult& r, int t) : result(r), type(t) {}
    };
    std::mutex ev_mutex;
    std::condition_variable ev_cv;
    std::deque<OrderEvent> ev_queue;

    Qh2hSellModule* sell_module = nullptr;
    BaseCancelModule* base_cancel_module = nullptr;

    std::vector<std::unique_ptr<IModule>> modules;
    if (enable_sell) {
        modules.emplace_back(new Qh2hSellModule(trading_account, hold_vol, code_min, code_max));
        sell_module = static_cast<Qh2hSellModule*>(modules.back().get());
    }
    if (enable_base_cancel) {
        modules.emplace_back(new BaseCancelModule(trading_account, hold_vol, code_min, code_max,
                                                 base_cancel_dir.empty() ? "./data/base_cancel" : base_cancel_dir));
        base_cancel_module = static_cast<BaseCancelModule*>(modules.back().get());
    }
    if (enable_usage) {
        if (!usage_csv_file.empty()) {
            modules.emplace_back(new UsageExampleModule(usage_csv_file, strategy_account_id,
                                                        sell_to_mkt_ratio, phase1_sell_ratio, input_amt, hold_vol));
        } else {
            main_logger->warn("[INIT] usage_example enabled but csv file not found; module skipped");
        }
    }

    std::vector<std::thread> module_threads;
    module_threads.reserve(modules.size());

    trading_raw->set_order_callback([&](const OrderResult& result, int notify_type) {
        std::lock_guard<std::mutex> lock(ev_mutex);
        ev_queue.push_back(OrderEvent{result, notify_type});
        ev_cv.notify_one();
    });

    std::thread dispatcher([&]() {
        while (!ctx.stop.load()) {
            OrderEvent ev;
            {
                std::unique_lock<std::mutex> lock(ev_mutex);
                ev_cv.wait_for(lock, std::chrono::milliseconds(200), [&]() {
                    return ctx.stop.load() || !ev_queue.empty();
                });
                if (ctx.stop.load() && ev_queue.empty()) {
                    break;
                }
                if (ev_queue.empty()) {
                    continue;
                }
                ev = std::move(ev_queue.front());
                ev_queue.pop_front();
            }

            const std::string& remark = ev.result.remark;
            if (sell_module && remark.rfind("qh2h_sell_", 0) == 0) {
                sell_module->on_order_event(ctx, ev.result, ev.type);
                continue;
            }
            if (base_cancel_module && (remark.rfind("qh2h_base_cancel_", 0) == 0 || !ev.result.is_local)) {
                base_cancel_module->on_order_event(ctx, ev.result, ev.type);
                continue;
            }
        }
    });

    for (auto& mod : modules) {
        if (!mod->init(ctx)) {
            main_logger->error(std::string("[INIT] module init failed: ") + mod->name());
            continue;
        }

        module_threads.emplace_back([&ctx, module = mod.get()]() {
            const auto interval = module->tick_interval();
            auto next = std::chrono::steady_clock::now();
            while (!ctx.stop.load()) {
                module->tick(ctx);
                next += interval;
                auto now = std::chrono::steady_clock::now();
                if (next < now) {
                    next = now + interval;
                }
                std::this_thread::sleep_until(next);
            }
        });
    }

    main_logger->info("[RUN] modules started; Ctrl+C to stop");

    while (!ctx.stop.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    main_logger->warn("[STOP] stopping...");

    for (auto& t : module_threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    if (dispatcher.joinable()) {
        dispatcher.join();
    }

    market->disconnect();
    trading->disconnect();
    trading->shutdown();

    main_logger->info("[EXIT] done");
    return 0;
}
