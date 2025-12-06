#pragma once

#include <string>
#include <unordered_map>
#include <vector>

/// @brief CSV配置中单个股票的参数
struct StockParams {
    std::string shortname;      // 股票简称
    std::string symbol;         // 股票代码（带.SH/.SZ后缀）
    std::string trading_date;   // 交易日期 YYYY-MM-DD
    
    // 持仓信息
    int64_t avail_vol = 0;      // 可用数量
    int64_t total_vol = 0;      // 总持仓
    
    // 状态标志
    int fb_flag = 0;            // 封板标志 (1=昨日封板未炸板)
    int zb_flag = 0;            // 炸板标志 (1=昨日封板后炸板)
    int second_flag = 0;        // 连板标志 (1=连板)
    
    // 价格信息
    double zt_price = 0.0;      // 涨停价
    double pre_close = 0.0;     // 前收盘价
    
    // 运行时状态（初始化时设置为0）
    int sell_flag = 0;          // 卖出完成标志
    int64_t sold_vol = 0;       // 已卖数量
    double jjamt = 0.0;         // 集合竞价金额
    double open_price = 0.0;    // 开盘价
    std::string remark;         // 订单备注
    int call_back = 0;          // 撤单标志
    int return1_sell = 0;       // 竞价阶段return1卖出标志
    int64_t total_sell = 0;     // 总卖出数量（竞价策略用）
    std::string userOrderId;    // 用户订单ID
    double dt_price = 0.0;      // 跌停价
    int limit_sell = 0;         // 涨停未封板半仓卖出标志
};

/// @brief CSV配置加载器
class CsvConfig {
public:
    CsvConfig() = default;
    
    /// @brief 从CSV文件加载配置
    /// @param csv_path CSV文件路径
    /// @return 成功返回true
    bool load_from_file(const std::string& csv_path);
    
    /// @brief 获取股票参数
    /// @param symbol 股票代码
    /// @return 如果存在返回参数指针，否则返回nullptr
    StockParams* get_stock(const std::string& symbol);
    const StockParams* get_stock(const std::string& symbol) const;
    
    /// @brief 获取所有股票代码列表
    std::vector<std::string> get_all_symbols() const;
    
    /// @brief 获取股票数量
    size_t size() const { return stocks_.size(); }
    
    /// @brief 清空配置
    void clear() { stocks_.clear(); }
    
private:
    // symbol -> StockParams
    std::unordered_map<std::string, StockParams> stocks_;
    
    /// @brief 解析单行CSV数据
    std::vector<std::string> parse_line(const std::string& line);
    
    /// @brief 添加.SH/.SZ后缀
    std::string normalize_symbol(const std::string& raw_symbol);
};
