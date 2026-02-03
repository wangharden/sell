#pragma once

#include "IModule.h"
#include "../core/MarketData.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ImprovedLogger;

class BaseCancelModule final : public IModule {
public:
    BaseCancelModule(std::string account_id,
                     int hold_vol,
                     std::string code_min,
                     std::string code_max,
                     std::string order_dir);

    const char* name() const override { return "qh2h_base_cancel"; }
    std::chrono::milliseconds tick_interval() const override { return std::chrono::seconds(1); }

    bool init(AppContext& ctx) override;
    void tick(AppContext& ctx) override;
    void on_order_event(AppContext& ctx, const OrderResult& result, int notify_type) override;

private:
    static int current_hhmmss();
    static bool time_in_range(int now, int start, int end);
    static double round_price(double value);

    static std::string trim_copy(const std::string& input);
    static bool is_six_digit_code(const std::string& token);
    static std::string extract_code_from_symbol(const std::string& symbol);
    static std::string extract_code_token(const std::string& raw);
    static std::vector<std::string> split_csv_line(const std::string& line);
    static std::string find_code_in_tokens(const std::vector<std::string>& tokens);
    static std::string to_symbol(const std::string& code);
    static bool pass_code_filter(const std::string& code,
                                 const std::string& min_code,
                                 const std::string& max_code);
    static double calc_limit_price(double pre_close, double ratio);

    static std::vector<std::string> list_files(const std::string& dir);
    static int parse_ymd(const std::string& token);
    static std::string find_latest_list_file(const std::string& dir);

    std::vector<std::string> load_buy_list_symbols(const std::string& dir, std::string* out_path);
    std::unordered_map<std::string, Position> build_position_map(const std::vector<Position>& positions) const;
    std::vector<std::string> extract_holding_symbols(const std::vector<Position>& positions) const;

    double resolve_zt_price(AppContext& ctx, const std::string& symbol);

    void do_base_buy(AppContext& ctx, int now);
    void do_pre_orders(AppContext& ctx, int now);
    void do_second_orders(AppContext& ctx, int now);
    void do_cancel(AppContext& ctx);
    void do_sell_non_list_positions(AppContext& ctx, int now);

    std::string account_id_;
    int hold_vol_ = 300;
    std::string code_min_;
    std::string code_max_;
    std::string order_dir_;

    std::shared_ptr<ImprovedLogger> logger_;

    bool buy_list_done_ = false;
    bool panqian_done_ = false;
    bool second_done_ = false;
    bool sell_non_list_done_ = false;
    int panqian_index_ = 0;

    std::vector<std::string> buy_symbols_;
    std::vector<std::string> holding_symbols_;
    std::string buy_list_path_;

    std::mutex state_mutex_;
    std::unordered_set<std::string> second_order_ids_;
    std::unordered_map<std::string, std::string> second_order_symbol_;
    std::unordered_map<std::string, std::string> second_order_by_symbol_;
    std::unordered_set<std::string> second_ready_;
    std::unordered_set<std::string> second_canceled_;
    std::unordered_map<std::string, double> zt_cache_;
    std::unordered_map<std::string, double> preclose_cache_;
};
