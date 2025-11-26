#include "SellStrategy.h"
#include <sstream>
#include <iostream>

void SellStrategy::init_default_strategy() {
    // 盘中卖出策略
    
    // fb: 封板未炸板
    strategy_["fb"][15e6][1.04] = {
        parse_window("112800-130200-0"),
        parse_window("103800-104200-0")
    };
    strategy_["fb"][0][1.015] = {
        parse_window("093000-093000-0")
    };
    strategy_["fb"][0][0] = {
        parse_window("105920-110040-0.66"),
        parse_window("142920-143040-0.33"),
        parse_window("150000-150000-0")
    };
    
    // hf: 回封 (封板后炸板又封回)
    strategy_["hf"][20e6][1.03] = {
        parse_window("112800-130200-0"),
        parse_window("104800-105200-0")
    };
    strategy_["hf"][0][1.03] = {
        parse_window("102900-103100-0.5"),
        parse_window("131400-131600-0")
    };
    strategy_["hf"][0][0] = {
        parse_window("142900-143100-0.5"),
        parse_window("143900-144100-0")
    };
    
    // zb: 炸板 (封板后炸板未回封)
    strategy_["zb"][3e6][1.04] = {
        parse_window("093000-093400-0")
    };
    strategy_["zb"][3e6][1] = {
        parse_window("150000-150000-0")
    };
    strategy_["zb"][3e6][0.97] = {
        parse_window("093900-094100-0.5"),
        parse_window("112900-130100-0")
    };
    strategy_["zb"][3e6][0] = {
        parse_window("142800-143200-0")
    };
    strategy_["zb"][0][1.01] = {
        parse_window("093000-093000-0")
    };
    strategy_["zb"][0][0.97] = {
        parse_window("105920-110040-0.66"),
        parse_window("144420-144540-0.33"),
        parse_window("150000-150000-0")
    };
    strategy_["zb"][0][0] = {
        parse_window("093030-093230-0.5"),
        parse_window("102400-102600-0")
    };
    
    // lb: 连板
    strategy_["lb"][0][1.07] = {
        parse_window("093000-093000-0")
    };
    strategy_["lb"][0][0] = {
        parse_window("150000-150000-0")
    };
}

std::vector<TimeWindow> SellStrategy::get_windows(
    const std::string& condition,
    double jjamt,
    double open_ratio
) const {
    auto cond_it = strategy_.find(condition);
    if (cond_it == strategy_.end()) {
        return {};
    }
    
    const auto& jjamt_map = cond_it->second;
    
    // 找到第一个 >= jjamt 的阈值（map已按降序排列）
    for (const auto& jjamt_pair : jjamt_map) {
        double jjamt_threshold = jjamt_pair.first;
        if (jjamt >= jjamt_threshold) {
            const auto& open_map = jjamt_pair.second;
            
            // 找到第一个 >= open_ratio 的阈值
            for (const auto& open_pair : open_map) {
                double open_threshold = open_pair.first;
                if (open_ratio >= open_threshold) {
                    return open_pair.second;
                }
            }
        }
    }
    
    return {};
}

TimeWindow SellStrategy::parse_window(const std::string& window_str) {
    // 格式: "start-end-keep", 例如 "093000-093400-0" 或 "105920-110040-0.66"
    std::stringstream ss(window_str);
    std::string start_str, end_str, keep_str;
    
    std::getline(ss, start_str, '-');
    std::getline(ss, end_str, '-');
    std::getline(ss, keep_str, '-');
    
    TimeWindow window;
    try {
        window.start_time = std::stoi(start_str);
        window.end_time = std::stoi(end_str);
        window.keep_position = std::stod(keep_str);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing window string: " << window_str 
                  << ", error: " << e.what() << std::endl;
    }
    
    return window;
}
