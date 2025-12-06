#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 导出宏 ====================
#ifdef _WIN32
    #ifdef SELL_STRATEGY_EXPORTS
        #define SELL_C_API __declspec(dllexport)
    #else
        #define SELL_C_API __declspec(dllimport)
    #endif
#else
    #define SELL_C_API __attribute__((visibility("default")))
#endif

// ==================== 句柄类型 ====================
typedef void* SellStrategyHandle;

// ==================== 初始化/销毁 ====================

/// @brief 创建策略引擎
SELL_C_API SellStrategyHandle sell_strategy_create();

/// @brief 销毁策略引擎
SELL_C_API void sell_strategy_destroy(SellStrategyHandle handle);

/// @brief 初始化引擎（连接行情和交易API）
/// @return 0成功，非0失败
SELL_C_API int sell_strategy_initialize(
    SellStrategyHandle handle,
    const char* tdf_host,
    int tdf_port,
    const char* tdf_user,
    const char* tdf_password,
    const char* trade_config_key,
    const char* trade_account,
    const char* trade_password
);

/// @brief 加载CSV配置
/// @return 0成功，非0失败
SELL_C_API int sell_strategy_load_config(
    SellStrategyHandle handle,
    const char* csv_path
);

// ==================== 策略控制 ====================

/// @brief 启动策略
/// @param strategy_type "intraday"/"auction"/"close"
/// @return 0成功，非0失败
SELL_C_API int sell_strategy_start(
    SellStrategyHandle handle,
    const char* strategy_type
);

/// @brief 停止策略
SELL_C_API void sell_strategy_stop(SellStrategyHandle handle);

/// @brief 手动触发一次策略执行
SELL_C_API void sell_strategy_trigger(SellStrategyHandle handle);

// ==================== 查询接口 ====================

/// @brief 查询持仓数量
SELL_C_API int sell_strategy_get_position_count(SellStrategyHandle handle);

/// @brief 查询持仓信息
/// @param index 索引
/// @param symbol [out] 股票代码（需提前分配64字节）
/// @param total [out] 总持仓
/// @param available [out] 可用
SELL_C_API int sell_strategy_get_position(
    SellStrategyHandle handle,
    int index,
    char* symbol,
    int64_t* total,
    int64_t* available
);

/// @brief 查询订单数量
SELL_C_API int sell_strategy_get_order_count(SellStrategyHandle handle);

/// @brief 查询订单信息
/// @param index 索引
/// @param order_id [out] 订单号（需提前分配64字节）
/// @param symbol [out] 股票代码（需提前分配64字节）
/// @param volume [out] 委托数量
/// @param filled_volume [out] 成交数量
/// @param price [out] 委托价格
/// @param status [out] 状态 0=待成交,1=部分成交,2=全部成交,3=已撤销,4=已拒绝
SELL_C_API int sell_strategy_get_order(
    SellStrategyHandle handle,
    int index,
    char* order_id,
    char* symbol,
    int64_t* volume,
    int64_t* filled_volume,
    double* price,
    int* status
);

/// @brief 获取行情快照
/// @param symbol 股票代码
/// @param last_price [out] 最新价
/// @param bid_price1 [out] 买一价
/// @param ask_price1 [out] 卖一价
/// @param bid_volume1 [out] 买一量
/// @param ask_volume1 [out] 卖一量
/// @return 0成功，非0失败
SELL_C_API int sell_strategy_get_snapshot(
    SellStrategyHandle handle,
    const char* symbol,
    double* last_price,
    double* bid_price1,
    double* ask_price1,
    int64_t* bid_volume1,
    int64_t* ask_volume1
);

// ==================== 订阅接口 ====================

/// @brief 订阅股票行情
/// @param symbols 股票代码数组（以分号分隔，如"600000.SH;000001.SZ"）
/// @return 0成功，非0失败
SELL_C_API int sell_strategy_subscribe(
    SellStrategyHandle handle,
    const char* symbols
);

// ==================== 工具函数 ====================

/// @brief 设置日志级别
/// @param level "DEBUG"/"INFO"/"WARN"/"ERROR"
SELL_C_API void sell_strategy_set_log_level(
    SellStrategyHandle handle,
    const char* level
);

/// @brief 获取最后错误信息
/// @param buffer [out] 错误信息缓冲区
/// @param buffer_size 缓冲区大小
SELL_C_API void sell_strategy_get_last_error(
    SellStrategyHandle handle,
    char* buffer,
    int buffer_size
);

// ==================== 快速启动函数 ====================

/// @brief 快速启动盘中策略（阻塞运行）
/// @return 0成功，非0失败
SELL_C_API int sell_strategy_quick_start_intraday(
    const char* csv_path,
    const char* account_id
);

/// @brief 快速启动竞价策略（阻塞运行）
SELL_C_API int sell_strategy_quick_start_auction(
    const char* csv_path,
    const char* account_id
);

/// @brief 快速启动收盘策略（阻塞运行）
SELL_C_API int sell_strategy_quick_start_close(
    const char* csv_path,
    const char* account_id
);

#ifdef __cplusplus
}
#endif
