#include "BaseCancelModule.h"

#include "../core/util.h"
#include "itpdk/itpdk_dict.h"
#include "ImprovedLogger.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace {
constexpr const char* kStrategyName = "qh2h_base_cancel";

constexpr int kBatchSize = 100;
constexpr int kBatchSleepMs = 1000;
constexpr int kPanqianBatchSize = 150;
}

BaseCancelModule::BaseCancelModule(std::string account_id,
                                   int hold_vol,
                                   std::string code_min,
                                   std::string code_max,
                                   std::string order_dir)
    : account_id_(std::move(account_id)),
      hold_vol_(hold_vol),
      code_min_(std::move(code_min)),
      code_max_(std::move(code_max)),
      order_dir_(std::move(order_dir)) {}

bool BaseCancelModule::init(AppContext& ctx) {
    logger_ = std::make_shared<ImprovedLogger>("qh2h_base_cancel", "./log", LogLevel::INFO);
    logger_->info("========== qh2h_base_cancel module init ==========");

    if (!ctx.trading || !ctx.market || !ctx.trading_raw) {
        logger_->error("[INIT] missing trading/market api in context");
        return false;
    }

    if (order_dir_.empty()) {
        order_dir_ = "./data/base_cancel";
    }
    logger_->info("[INIT] order_dir=" + order_dir_);

    buy_symbols_ = load_buy_list_symbols(order_dir_, &buy_list_path_);
    if (buy_list_path_.empty()) {
        logger_->warn("[INIT] no buy list csv found in " + order_dir_);
    } else if (buy_symbols_.empty()) {
        logger_->warn("[INIT] buy list csv loaded but 0 symbols: " + buy_list_path_);
    } else {
        logger_->info_f("[INIT] loaded %zu buy symbols from %s",
                        buy_symbols_.size(), buy_list_path_.c_str());
    }

    auto positions = ctx.trading->query_positions();
    holding_symbols_ = extract_holding_symbols(positions);
    logger_->info_f("[INIT] holding symbols: %zu", holding_symbols_.size());

    buy_list_done_ = false;
    panqian_done_ = false;
    second_done_ = false;
    panqian_index_ = 0;

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        second_order_ids_.clear();
        second_order_symbol_.clear();
        second_order_by_symbol_.clear();
        second_ready_.clear();
        second_canceled_.clear();
        zt_cache_.clear();
        preclose_cache_.clear();
    }

    return true;
}

void BaseCancelModule::tick(AppContext& ctx) {
    if (ctx.stop.load()) {
        return;
    }

    int now = current_hhmmss();

    // 14:54-14:55 底仓买入
    if (!buy_list_done_ && time_in_range(now, 145400, 145500)) {
        buy_list_done_ = true;
        do_base_buy(ctx, now);
    }

    // 09:10:20-09:17:00 盘前委托
    if (!panqian_done_ && time_in_range(now, 91020, 91700)) {
        do_pre_orders(ctx, now);
    }

    // 09:24:20-09:24:50 排撤第二单
    if (!second_done_ && time_in_range(now, 92420, 92450)) {
        do_second_orders(ctx, now);
        second_done_ = true;
    }

    // 09:29:00-14:55:00 盘中撤单
    if (time_in_range(now, 92900, 145500)) {
        do_cancel(ctx);
    }
}

void BaseCancelModule::on_order_event(AppContext& ctx, const OrderResult& result, int notify_type) {
    (void)ctx;

    // 排撤触发只需要“委托推送”(nType=NOTIFY_PUSH_ORDER)。
    // 外部单出现就撤第二单：不必等待成交推送。
    if (notify_type != NOTIFY_PUSH_ORDER) {
        return;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);

    // Only external orders trigger cancel; ignore local orders and the second order itself.
    if (result.is_local || second_order_ids_.count(result.order_id) > 0) {
        return;
    }

    // Sell + limit (OrderType==0) + 100 shares + limit-up price.
    if (result.side != 1 || result.order_type != 0 || result.volume != 100) {
        return;
    }

    std::string symbol = result.symbol;
    auto zt_it = zt_cache_.find(symbol);
    if (zt_it == zt_cache_.end()) {
        std::string code = extract_code_from_symbol(symbol);
        std::string alt = to_symbol(code);
        if (!alt.empty()) {
            symbol = alt;
            zt_it = zt_cache_.find(symbol);
        }
    }
    if (zt_it == zt_cache_.end()) {
        return;
    }

    if (std::abs(result.price - zt_it->second) < 0.01) {
        auto it = second_order_by_symbol_.find(symbol);
        if (it != second_order_by_symbol_.end()) {
            const std::string& second_order_id = it->second;
            if (second_canceled_.count(second_order_id) == 0) {
                second_ready_.insert(second_order_id);
                logger_->info("[CALLBACK] external " + symbol + " order=" + result.order_id +
                              " trigger cancel second=" + second_order_id);
            }
        }
    }
}

int BaseCancelModule::current_hhmmss() {
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

bool BaseCancelModule::time_in_range(int now, int start, int end) {
    return now >= start && now < end;
}

double BaseCancelModule::round_price(double value) {
    return std::round(value * 100.0) / 100.0;
}

std::string BaseCancelModule::trim_copy(const std::string& input) {
    size_t start = input.find_first_not_of(" \t\r\n\"");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = input.find_last_not_of(" \t\r\n\"");
    return input.substr(start, end - start + 1);
}

bool BaseCancelModule::is_six_digit_code(const std::string& token) {
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

std::string BaseCancelModule::extract_code_from_symbol(const std::string& symbol) {
    size_t dot = symbol.find('.');
    if (dot == std::string::npos) {
        return symbol;
    }
    return symbol.substr(0, dot);
}

std::string BaseCancelModule::extract_code_token(const std::string& raw) {
    std::string token = trim_copy(raw);
    if (token.size() >= 9 && token[6] == '.') {
        token = token.substr(0, 6);
    }
    if (is_six_digit_code(token)) {
        return token;
    }
    return "";
}

std::vector<std::string> BaseCancelModule::split_csv_line(const std::string& line) {
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, ',')) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string BaseCancelModule::find_code_in_tokens(const std::vector<std::string>& tokens) {
    for (const auto& raw : tokens) {
        std::string code = extract_code_token(raw);
        if (!code.empty()) {
            return code;
        }
    }
    return "";
}

std::string BaseCancelModule::to_symbol(const std::string& code) {
    if (!is_six_digit_code(code)) {
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

bool BaseCancelModule::pass_code_filter(const std::string& code,
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

double BaseCancelModule::calc_limit_price(double pre_close, double ratio) {
    if (pre_close <= 0.0 || ratio <= 0.0) {
        return 0.0;
    }
    return round_price(pre_close * (1.0 + ratio));
}

std::vector<std::string> BaseCancelModule::list_files(const std::string& dir) {
    std::vector<std::string> files;
#ifdef _WIN32
    std::string pattern = dir + "\\*";
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
    DIR* dp = opendir(dir.c_str());
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
}

int BaseCancelModule::parse_ymd(const std::string& token) {
    // Support both YYYYMMDD and YYYY-MM-DD (legacy naming used in sell2).
    if (token.size() == 8) {
        for (char c : token) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return 0;
            }
        }
        return std::atoi(token.c_str());
    }

    if (token.size() == 10 && token[4] == '-' && token[7] == '-') {
        for (size_t i = 0; i < token.size(); ++i) {
            if (i == 4 || i == 7) {
                continue;
            }
            if (!std::isdigit(static_cast<unsigned char>(token[i]))) {
                return 0;
            }
        }
        int year = std::atoi(token.substr(0, 4).c_str());
        int month = std::atoi(token.substr(5, 2).c_str());
        int day = std::atoi(token.substr(8, 2).c_str());
        if (year <= 0 || month <= 0 || day <= 0) {
            return 0;
        }
        return year * 10000 + month * 100 + day;
    }

    return 0;
}

std::string BaseCancelModule::find_latest_list_file(const std::string& dir) {
    // Prefer selecting the latest list csv by file modification time, instead of relying on date parsing
    // from the filename. This avoids missing formats like "2026-02-02_list.csv" and matches ops behavior.
#ifdef _WIN32
    WIN32_FIND_DATAA find_data;
    std::string search_path = dir + "\\*.csv";
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return "";
    }

    FILETIME best_list_time = {0, 0};
    FILETIME best_any_time = {0, 0};
    std::string best_list_name;
    std::string best_any_name;

    do {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        std::string name = find_data.cFileName;
        const bool is_list = (name.find("_list") != std::string::npos);

        if (is_list) {
            if (best_list_name.empty() || CompareFileTime(&find_data.ftLastWriteTime, &best_list_time) > 0) {
                best_list_time = find_data.ftLastWriteTime;
                best_list_name = std::move(name);
            }
        }

        if (best_any_name.empty() || CompareFileTime(&find_data.ftLastWriteTime, &best_any_time) > 0) {
            best_any_time = find_data.ftLastWriteTime;
            best_any_name = std::move(name);
        }
    } while (FindNextFileA(hFind, &find_data) != 0);

    FindClose(hFind);

    const std::string& picked = best_list_name.empty() ? best_any_name : best_list_name;
    if (picked.empty()) {
        return "";
    }
    return dir + "\\" + picked;
#else
    DIR* dp = opendir(dir.c_str());
    if (!dp) {
        return "";
    }

    std::string best_list_name;
    std::string best_any_name;
    time_t best_list_time = 0;
    time_t best_any_time = 0;

    while (auto* ent = readdir(dp)) {
        if (ent->d_type == DT_DIR) {
            continue;
        }
        std::string name = ent->d_name;
        if (name.size() < 4 || name.substr(name.size() - 4) != ".csv") {
            continue;
        }

        std::string full_path = dir + "/" + name;
        struct stat file_stat;
        if (stat(full_path.c_str(), &file_stat) != 0) {
            continue;
        }

        const bool is_list = (name.find("_list") != std::string::npos);
        if (is_list && (best_list_name.empty() || file_stat.st_mtime > best_list_time)) {
            best_list_time = file_stat.st_mtime;
            best_list_name = name;
        }
        if (best_any_name.empty() || file_stat.st_mtime > best_any_time) {
            best_any_time = file_stat.st_mtime;
            best_any_name = name;
        }
    }
    closedir(dp);

    const std::string& picked = best_list_name.empty() ? best_any_name : best_list_name;
    if (picked.empty()) {
        return "";
    }
    return dir + "/" + picked;
#endif
}

std::vector<std::string> BaseCancelModule::load_buy_list_symbols(const std::string& dir, std::string* out_path) {
    std::vector<std::string> symbols;
    std::string file_path = find_latest_list_file(dir);
    if (out_path) {
        *out_path = file_path;
    }
    if (file_path.empty()) {
        logger_->error("[BUY] no list csv found in " + dir);
        return symbols;
    }

    std::ifstream file(file_path.c_str());
    if (!file.is_open()) {
        logger_->error("[BUY] failed to open list file: " + file_path);
        return symbols;
    }

    std::unordered_set<std::string> dedup;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        std::vector<std::string> tokens = split_csv_line(line);
        std::string code = find_code_in_tokens(tokens);
        if (code.empty()) {
            continue;
        }
        if (!pass_code_filter(code, code_min_, code_max_)) {
            continue;
        }
        std::string symbol = to_symbol(code);
        if (symbol.empty()) {
            continue;
        }
        if (dedup.insert(symbol).second) {
            symbols.push_back(symbol);
        }
    }
    return symbols;
}

std::unordered_map<std::string, Position> BaseCancelModule::build_position_map(const std::vector<Position>& positions) const {
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

std::vector<std::string> BaseCancelModule::extract_holding_symbols(const std::vector<Position>& positions) const {
    std::vector<std::string> symbols;
    for (const auto& pos : positions) {
        std::string code = extract_code_from_symbol(pos.symbol);
        if (!pass_code_filter(code, code_min_, code_max_)) {
            continue;
        }
        symbols.push_back(pos.symbol);
    }
    return symbols;
}

double BaseCancelModule::resolve_zt_price(AppContext& ctx, const std::string& symbol) {
    std::pair<double, double> limits;
    {
        std::lock_guard<std::mutex> lock(ctx.market_mutex);
        limits = ctx.market->get_limits(symbol);
    }
    double zt = round_price(limits.first);
    if (zt > 0.0) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        zt_cache_[symbol] = zt;
    }
    return zt;
}

void BaseCancelModule::do_base_buy(AppContext& ctx, int now) {
    if (buy_symbols_.empty()) {
        logger_->warn("[BUY] buy list empty, skipping");
        return;
    }

    auto pos_map = build_position_map(ctx.trading->query_positions());
    int buy_count = 0;

    for (const auto& symbol : buy_symbols_) {
        int64_t current = 0;
        auto it = pos_map.find(symbol);
        if (it != pos_map.end()) {
            current = it->second.total;
        }
        if (current >= hold_vol_) {
            continue;
        }

        int64_t vol = hold_vol_ - current;
        vol = to_lot(vol, 100);
        if (vol <= 0) {
            continue;
        }

        if (buy_count > 0 && buy_count % kBatchSize == 0) {
            logger_->info_f("[BUY] batch sleep 1s (%d orders)", buy_count);
            std::this_thread::sleep_for(std::chrono::milliseconds(kBatchSleepMs));
        }

        double buy_price = 0.0;
        {
            std::lock_guard<std::mutex> lock(ctx.market_mutex);
            auto limits = ctx.market->get_limits(symbol);
            double low_limit = round_price(limits.second);
            if (low_limit > 0.0) {
                buy_price = low_limit;
            } else {
                MarketSnapshot snap = ctx.market->get_snapshot(symbol);
                if (snap.valid && snap.pre_close > 0.0) {
                    buy_price = round_price(snap.pre_close * 0.9);
                } else {
                    logger_->warn("[BUY] " + symbol + " no low_limit/pre_close, skip");
                    continue;
                }
            }
        }

        OrderRequest req;
        req.account_id = account_id_;
        req.symbol = symbol;
        req.side = OrderSide::Buy;
        req.price = buy_price;
        req.volume = vol;
        req.is_market = true;
        req.remark = std::string(kStrategyName) + "_base_buy_" + symbol + "_" + std::to_string(now);

        std::string order_id = ctx.trading->place_order(req);
        if (!order_id.empty()) {
            buy_count++;
            logger_->info_f("[BUY] %s vol=%lld price=%.2f order=%s",
                            symbol.c_str(), static_cast<long long>(vol), buy_price, order_id.c_str());
        }
    }

    logger_->info_f("[BUY] done, total %d orders", buy_count);
}

void BaseCancelModule::do_pre_orders(AppContext& ctx, int now) {
    auto pos_map = build_position_map(ctx.trading->query_positions());
    int placed = 0;

    for (size_t idx = static_cast<size_t>(panqian_index_); idx < holding_symbols_.size(); ++idx) {
        if (panqian_index_ >= 270 && now < 91500) {
            break;
        }
        const std::string& symbol = holding_symbols_[idx];
        panqian_index_++;

        auto it = pos_map.find(symbol);
        if (it == pos_map.end() || it->second.available < 100) {
            continue;
        }

        double zt = 0.0;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto zt_it = zt_cache_.find(symbol);
            if (zt_it != zt_cache_.end()) {
                zt = zt_it->second;
            }
        }

        if (zt <= 0.0) {
            std::pair<double, double> limits;
            MarketSnapshot snap;
            {
                std::lock_guard<std::mutex> lock(ctx.market_mutex);
                limits = ctx.market->get_limits(symbol);
                snap = ctx.market->get_snapshot(symbol);
            }
            zt = round_price(limits.first);
            if (zt <= 0.0 && snap.valid && snap.pre_close > 0.0) {
                std::string code = extract_code_from_symbol(symbol);
                double ratio = (code.rfind("30", 0) == 0 || code.rfind("68", 0) == 0) ? 0.20 : 0.10;
                zt = calc_limit_price(snap.pre_close, ratio);
                std::lock_guard<std::mutex> lock(state_mutex_);
                preclose_cache_[symbol] = snap.pre_close;
            }
            if (zt > 0.0) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                zt_cache_[symbol] = zt;
            }
        }

        if (zt <= 0.0) {
            continue;
        }

        OrderRequest req;
        req.account_id = account_id_;
        req.symbol = symbol;
        req.side = OrderSide::Sell;
        req.price = zt;
        req.volume = 100;
        req.is_market = false;
        req.remark = std::string(kStrategyName) + "_pre_" + symbol + "_" + std::to_string(now);

        std::string order_id = ctx.trading->place_order(req);
        if (!order_id.empty()) {
            logger_->info_f("[PRE] %s zt=%.2f order=%s", symbol.c_str(), zt, order_id.c_str());
        }

        placed++;
        if (placed % kPanqianBatchSize == 0) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    if (panqian_index_ >= static_cast<int>(holding_symbols_.size())) {
        panqian_done_ = true;
        logger_->info("[PRE] done");
    }
}

void BaseCancelModule::do_second_orders(AppContext& ctx, int now) {
    auto pos_map = build_position_map(ctx.trading->query_positions());
    int queue_count = 0;

    for (const auto& symbol : holding_symbols_) {
        auto it = pos_map.find(symbol);
        if (it == pos_map.end() || it->second.available < 100) {
            continue;
        }

        double zt = 0.0;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto zt_it = zt_cache_.find(symbol);
            if (zt_it != zt_cache_.end()) {
                zt = zt_it->second;
            }
        }

        if (zt <= 0.0) {
            std::pair<double, double> limits;
            {
                std::lock_guard<std::mutex> lock(ctx.market_mutex);
                limits = ctx.market->get_limits(symbol);
            }
            zt = round_price(limits.first);
            if (zt <= 0.0) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                auto pc_it = preclose_cache_.find(symbol);
                if (pc_it != preclose_cache_.end() && pc_it->second > 0.0) {
                    std::string code = extract_code_from_symbol(symbol);
                    double ratio = (code.rfind("30", 0) == 0 || code.rfind("68", 0) == 0) ? 0.20 : 0.10;
                    zt = calc_limit_price(pc_it->second, ratio);
                }
            }
            if (zt > 0.0) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                zt_cache_[symbol] = zt;
            }
        }

        if (zt <= 0.0) {
            continue;
        }

        if (queue_count > 0 && queue_count % kBatchSize == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kBatchSleepMs));
        }

        OrderRequest req;
        req.account_id = account_id_;
        req.symbol = symbol;
        req.side = OrderSide::Sell;
        req.price = zt;
        req.volume = 100;
        req.is_market = false;
        req.remark = std::string(kStrategyName) + "_queue_" + symbol + "_" + std::to_string(now);

        std::string order_id = ctx.trading->place_order(req);
        if (!order_id.empty()) {
            queue_count++;
            std::lock_guard<std::mutex> lock(state_mutex_);
            second_order_ids_.insert(order_id);
            second_order_symbol_[order_id] = symbol;
            second_order_by_symbol_[symbol] = order_id;
            logger_->info_f("[QUEUE] %s zt=%.2f order=%s", symbol.c_str(), zt, order_id.c_str());
        }
    }

    logger_->info_f("[QUEUE] done, total %d orders", queue_count);
}

void BaseCancelModule::do_cancel(AppContext& ctx) {
    std::vector<std::string> to_cancel;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (const auto& order_id : second_ready_) {
            if (second_order_ids_.count(order_id) == 0) {
                continue;
            }
            if (second_canceled_.count(order_id) == 0) {
                to_cancel.push_back(order_id);
            }
        }
    }

    for (const auto& order_id : to_cancel) {
        if (ctx.trading->cancel_order(order_id)) {
            std::string symbol = "unknown";
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                second_canceled_.insert(order_id);
                auto it = second_order_symbol_.find(order_id);
                if (it != second_order_symbol_.end()) {
                    symbol = it->second;
                }
            }
            logger_->info("[CANCEL] " + symbol + " order=" + order_id);
        }
    }
}

