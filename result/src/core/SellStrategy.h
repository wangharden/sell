#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>

/// @brief 时间窗口配置
struct TimeWindow {
    int start_time;         // 开始时间 HHMMSS (例如 093000 表示 09:30:00)
    int end_time;           // 结束时间 HHMMSS
    double keep_position;   // 保留仓位比例 [0, 1]
    
    TimeWindow() : start_time(0), end_time(0), keep_position(0.0) {}
    TimeWindow(int start, int end, double keep) 
        : start_time(start), end_time(end), keep_position(keep) {}
};

/// @brief 卖出策略配置
/// 结构：条件 -> 集合竞价金额阈值 -> 开盘比例阈值 -> 时间窗口列表
/// 对应txt中的：sell_strategy[condition][jjamt_level][open_ratio] = ['start-end-keep', ...]
class SellStrategy {
public:
    SellStrategy() { init_default_strategy(); }
    
    /// @brief 获取指定条件下的卖出时间窗口
    /// @param condition 卖出条件 ("fb", "hf", "zb", "lb")
    /// @param jjamt 集合竞价金额
    /// @param open_ratio 开盘价/前收盘价
    /// @return 时间窗口列表
    std::vector<TimeWindow> get_windows(
        const std::string& condition,
        double jjamt,
        double open_ratio
    ) const;
    
private:
    // condition -> (jjamt_threshold -> (open_ratio_threshold -> time_windows))
    using OpenRatioMap = std::map<double, std::vector<TimeWindow>, std::greater<double>>;
    using JjamtMap = std::map<double, OpenRatioMap, std::greater<double>>;
    using StrategyMap = std::map<std::string, JjamtMap>;
    
    StrategyMap strategy_;
    
    /// @brief 初始化默认策略（从txt复制）
    void init_default_strategy();
    
    /// @brief 解析时间窗口字符串 "start-end-keep"
    TimeWindow parse_window(const std::string& window_str);
};
