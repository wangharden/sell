#pragma once
#include <random>
#include <chrono>
#include <cstdint>

/// @brief 可注入种子的随机数生成器（用于单测可重复性与生产环境随机性）
/// 使用 std::mt19937_64 作为引擎，支持均匀分布与正态分布
class RNG {
public:
    /// @brief 构造函数
    /// @param seed 随机种子；若为 0 则使用高精度时钟自动生成
    explicit RNG(uint64_t seed = 0) {
        if (seed == 0) {
            seed = static_cast<uint64_t>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
        }
        rng_.seed(seed);
    }

    /// @brief 生成 [0, 1) 均匀分布随机数
    /// @return 浮点数随机值
    double uni() {
        return uni_dist_(rng_);
    }

    /// @brief 生成正态分布随机数 N(mu, sigma)
    /// @param mu 均值（默认 0.0）
    /// @param sigma 标准差（默认 1.0）
    /// @return 正态分布随机值
    double normal(double mu = 0.0, double sigma = 1.0) {
        std::normal_distribution<double> d(mu, sigma);
        return d(rng_);
    }

    /// @brief 生成 [min_val, max_val] 区间内的均匀整数
    /// @param min_val 最小值（包含）
    /// @param max_val 最大值（包含）
    /// @return 整数随机值
    int64_t uniform_int(int64_t min_val, int64_t max_val) {
        std::uniform_int_distribution<int64_t> d(min_val, max_val);
        return d(rng_);
    }

    /// @brief 暴露底层引擎（供高级用户/自定义分布使用）
    /// @return std::mt19937_64 引用
    std::mt19937_64& engine() { return rng_; }

private:
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> uni_dist_{0.0, 1.0};
};
