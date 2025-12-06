#pragma once
#include "../core/TradingMarketApi.h"
#include "../core/CsvConfig.h"
#include <string>
#include <vector>
#include <map>
#include <random>

/// @brief 竞价卖出策略（对应 qh2h竞价卖出.txt）
/// 时间窗口：09:20:05-09:24:58
/// 逻辑：
/// 1. 09:20:05-09:23:00: 检查行情是否正常
/// 2. 09:23:30-09:24:58 (Phase 1): 无条件卖出10%仓位，挂跌停价
/// 3. 09:23:40-09:24:45 (Phase 2): 根据条件卖出剩余仓位
///    - 连板(lb): 买1价>=昨收*1.07时，挂昨收*1.07价格
///    - 封死(fb): ask1成交额<1500万，买1价>=昨收*1.015时，挂昨收*1.015
///    - 炸板(zb): ask1成交额<300万，买1价>=昨收*1.01时，挂昨收*1.01
///    - 限制总卖出量不超过ask1的一定比例（sell_to_mkt_ratio）
///    - 每0.5秒，12.5%概率触发
class AuctionSellStrategy {
public:
    AuctionSellStrategy(
        TradingMarketApi* api,
        const std::string& csv_path,
        const std::string& account_id
    );
    
    ~AuctionSellStrategy() = default;
    
    /// @brief 初始化策略
    /// @return 是否成功
    bool init();
    
    /// @brief 定时回调
    void on_timer();
    
    /// @brief 打印策略状态
    void print_status() const;
    
private:
    TradingMarketApi* api_;
    std::string csv_path_;
    std::string account_id_;
    CsvConfig csv_config_;
    
    // 配置参数
    int64_t single_amt_ = 20000;       // 单笔金额
    int64_t rand_amt1_ = 40000;        // 随机金额1
    int64_t rand_amt2_ = 5000;         // 随机金额2
    int64_t hold_vol_ = 300;           // 底仓数量
    double sell_to_mkt_ratio_ = 0.0;   // 卖出量占市场ask1的比例限制（0表示不限制）
    
    // 状态变量
    int hangqin_check_ = 0;   // 行情检查标志
    int before_check_ = 0;    // 开盘前数据收集标志
    int kaipan_timer_ = 0;    // 开盘计时器
    
    // 随机数生成器
    std::mt19937 rng_;
    std::uniform_real_distribution<double> uniform_dist_;
    std::normal_distribution<double> normal_dist_;
    
    // ========== 私有方法 ==========
    
    /// @brief Phase 0: 检查行情是否正常 (09:20:05-09:23:00)
    void check_market_data();
    
    /// @brief Phase 1: 无条件卖出10%仓位 (09:23:30-09:25:00)
    void phase1_return1_sell();
    
    /// @brief Phase 2: 根据条件卖出剩余仓位 (09:23:40-09:24:45)
    void phase2_conditional_sell();
    
    /// @brief Phase 3: 涨停未封板处理与最后冲刺 (09:24:50-09:25:00)
    void phase3_final_sell();
    
    /// @brief 撤单处理 (09:25:13-09:25:23)
    void cancel_auction_orders();
    
    /// @brief 收集集合竞价数据 (09:26:00-09:28:10)
    void collect_auction_data();
    
    /// @brief 开盘后继续卖出 (09:29:55-09:30:40)
    void after_open_sell();
    
    /// @brief 获取当前时间 HHMMSS
    int get_current_time() const;
    
    /// @brief 获取当前日期 YYYYMMDD
    int get_current_date() const;
};
