#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

/// @brief ceil_round: 向上取到指定小数位（与 Python 版本行为一致）
/// @param x 输入浮点数
/// @param ndigits 小数位数（默认 2）
/// @return 向上取整后的浮点数
inline double ceil_round(double x, int ndigits = 2) {
    double factor = std::pow(10.0, ndigits);
    return std::ceil(x * factor) / factor;
}

/// @brief 保证按 lot（如 100 股）对齐，向下取整到整手
/// @param qty 原始数量
/// @param lot 最小交易单位（默认 100）
/// @return 对齐后的数量
inline int64_t to_lot(int64_t qty, int64_t lot = 100) {
    if (qty <= 0 || lot <= 0) return 0;
    return (qty / lot) * lot;
}

/// @brief 限定值在 [min_val, max_val] 区间内
template<typename T>
inline T clamp(T val, T min_val, T max_val) {
    return std::min(std::max(val, min_val), max_val);
}
