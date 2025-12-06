#pragma once
#include "../core/TradingMarketApi.h"
#include <string>
#include <vector>
#include <map>
#include <random>

/// @brief 收盘卖出策略（对应 qh2h收盘卖出.txt）
/// 时间窗口：14:53:00-14:56:45
/// 逻辑：
/// 1. 每3秒触发一次
/// 2. 每次15%概率随机卖出
/// 3. 单笔金额：single_amt=30000, rand_amt1=50000, rand_amt2=5000
/// 4. 卖出完70%仓位约需10次（总共约80次机会）
/// 5. 卖出逻辑：
///    - 计算随机卖出数量
///    - 使用中间价（buy1+sell1)/2 向下取整
///    - 检查是否涨停（不卖）
///    - 更新sold_vol并下单
class CloseSellStrategy {
public:
    CloseSellStrategy(
        TradingMarketApi* api,
        const std::string& account_id
    );
    
    ~CloseSellStrategy() = default;
    
    /// @brief 初始化策略
    /// @return 是否成功
    bool init();
    
    /// @brief 定时回调（建议每3秒调用一次）
    void on_timer();
    
    /// @brief 打印策略状态
    void print_status() const;
    
private:
    TradingMarketApi* api_;
    std::string account_id_;
    
    // 配置参数
    int64_t single_amt_ = 30000;       // 单笔金额
    int64_t rand_amt1_ = 50000;        // 随机金额1
    int64_t rand_amt2_ = 5000;         // 随机金额2
    int64_t hold_vol_ = 300;           // 底仓数量
    double trigger_probability_ = 0.15; // 触发概率
    
    // 运行时数据: symbol -> sold_vol
    std::map<std::string, int64_t> sold_volumes_;
    
    // 运行时数据: symbol -> total_vol
    std::map<std::string, int64_t> total_volumes_;
    
    // 运行时数据: symbol -> remark
    std::map<std::string, std::string> remarks_;
    
    // 运行时数据: symbol -> callback flag
    std::map<std::string, int> callbacks_;
    
    // 阶段控制标志
    int phase2_cancel_done_ = 0;
    int phase3_test_sell_done_ = 0;
    int phase4_bulk_sell_done_ = 0;
    
    // 随机数生成器
    std::mt19937 rng_;
    std::uniform_real_distribution<double> uniform_dist_;
    std::normal_distribution<double> normal_dist_;
    
    // ========== 私有方法 ==========
    
    /// @brief Phase 1: 随机卖出 (14:53:00-14:56:45)
    void phase1_random_sell();
    
    /// @brief Phase 2: 撤单 (14:56:45-14:57:00)
    void phase2_cancel_orders();
    
    /// @brief Phase 3: 测试卖出 (14:57:20-14:57:50)
    void phase3_test_sell();
    
    /// @brief Phase 4: 大量卖出 (14:58:00-14:59:50)
    void phase4_bulk_sell();
    
    /// @brief 获取当前时间 HHMMSS
    int get_current_time() const;
};
