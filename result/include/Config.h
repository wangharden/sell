#pragma once
#include <string>
#include <memory>
#include <map>
#include <vector>

/// @brief 简化的配置接口（避免在头文件暴露 JSON 依赖）
class Config {
public:
    virtual ~Config() = default;

    /// @brief 获取字符串配置
    virtual std::string get_string(const std::string& key, 
                                     const std::string& default_val = "") const = 0;

    /// @brief 获取整数配置
    virtual int64_t get_int(const std::string& key, int64_t default_val = 0) const = 0;

    /// @brief 获取浮点配置
    virtual double get_double(const std::string& key, double default_val = 0.0) const = 0;

    /// @brief 获取布尔配置
    virtual bool get_bool(const std::string& key, bool default_val = false) const = 0;

    /// @brief 检查键是否存在
    virtual bool has(const std::string& key) const = 0;

    /// @brief 获取子配置
    virtual std::shared_ptr<Config> get_sub(const std::string& key) const = 0;
};

using ConfigPtr = std::shared_ptr<Config>;

/// @brief 配置管理器（负责加载与热更新）
class ConfigManager {
public:
    /// @brief 从文件加载配置
    /// @param path JSON 文件路径
    /// @return 配置对象
    static ConfigPtr load_from_file(const std::string& path);

    /// @brief 从字符串加载配置
    /// @param json_str JSON 字符串
    /// @return 配置对象
    static ConfigPtr load_from_string(const std::string& json_str);
};
