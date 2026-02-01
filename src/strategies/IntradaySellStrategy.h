#pragma once

#include "../core/TradingMarketApi.h"
#include "../core/CsvConfig.h"
#include "../core/SellStrategy.h"
#include "../core/rng.h"
#include <string>
#include <unordered_map>

/// @brief 盘中卖出策略（复现txt qh2h盘中卖出.txt逻辑）
class IntradaySellStrategy {
public:
    /// @brief 构造函数
    /// @param api 交易和行情组合API接口
    /// @param csv_path CSV配置文件路径
    /// @param account_id 账号ID
    /// @param input_amt 基准金额（用于单笔金额/随机区间）
    IntradaySellStrategy(
        TradingMarketApi* api,
        const std::string& csv_path,
        const std::string& account_id,
        int64_t hold_vol,
        double input_amt
    );
    
    /// @brief 初始化策略（对应txt中的init函数）
    /// - 加载CSV配置
    /// - 查询持仓
    /// - 下载历史行情（如需要）
    bool init();
    
    /// @brief 定时执行主循环（对应txt中的myHandlebar，每3秒调用一次）
    /// - Phase 1 (09:26:00-11:28:10): 收集集合竞价数据
    /// - Phase 2 (09:30:03-14:48:55): 执行卖出
    /// - Phase 3 (14:49:00-14:51:00): 撤单
    void on_timer();
    
    /// @brief 获取当前统计信息
    void print_status() const;
    
private:
    TradingMarketApi* api_;
    std::string csv_path_;
    std::string account_id_;
    
    CsvConfig csv_config_;
    SellStrategy sell_strategy_;
    RNG rng_;
    
    // 全局参数（对应txt中的全局变量）
    double single_amt_ = 0.0;       // input_amt * 0.025
    double rand_amt1_ = 0.0;        // input_amt * 0.02
    double rand_amt2_ = 5000.0;     // txt line 18
    int64_t hold_vol_ = 300;        // 保留仓位 txt line 19
    
    // 运行时状态
    int before_check_ = 0;          // txt line 21: 集合竞价数据收集标志
    int cancel_attempts_ = 0;
    int cancel_attempt_date_ = 0;

    // 09:26 采样一次：竞价阶段结束后的可用仓位基准（用于 keep_position 判断）
    std::unordered_map<std::string, int64_t> base_avail_after_auction_;
    bool base_captured_ = false;
    
    /// @brief Phase 1: 收集集合竞价数据 (09:26:00)
    void collect_auction_data();
    
    /// @brief Phase 2: 执行卖出 (09:30:03-14:48:55)
    void execute_sell();
    
    /// @brief Phase 3: 撤单 (14:49:00-14:51:00)
    void cancel_orders();
    
    /// @brief 单个股票卖出逻辑（对应txt中的sell_order函数）
    /// @param symbol 股票代码
    /// @param keep_position 保留仓位比例
    /// @param current_time 当前时间 HHMMSS
    void sell_order(
        const std::string& symbol,
        double keep_position,
        int current_time
    );
    
    /// @brief 获取当前时间 HHMMSS格式
    int get_current_time() const;
    
    /// @brief 获取当前日期 YYYYMMDD格式
    int get_current_date() const;
    
    /// @brief 判断卖出条件类型
    /// @return "fb", "hf", "zb", "lb" 或空字符串
    std::string determine_condition(const StockParams& params) const;
};
