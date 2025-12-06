#include "Config.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

/// @brief JSON 配置实现
class JsonConfig : public Config {
public:
    explicit JsonConfig(const json& j) : data_(j) {}

    std::string get_string(const std::string& key, 
                           const std::string& default_val) const override {
        if (data_.contains(key) && data_[key].is_string()) {
            return data_[key].get<std::string>();
        }
        return default_val;
    }

    int64_t get_int(const std::string& key, int64_t default_val) const override {
        if (data_.contains(key) && data_[key].is_number_integer()) {
            return data_[key].get<int64_t>();
        }
        return default_val;
    }

    double get_double(const std::string& key, double default_val) const override {
        if (data_.contains(key) && data_[key].is_number()) {
            return data_[key].get<double>();
        }
        return default_val;
    }

    bool get_bool(const std::string& key, bool default_val) const override {
        if (data_.contains(key) && data_[key].is_boolean()) {
            return data_[key].get<bool>();
        }
        return default_val;
    }

    bool has(const std::string& key) const override {
        return data_.contains(key);
    }

    ConfigPtr get_sub(const std::string& key) const override {
        if (data_.contains(key) && data_[key].is_object()) {
            return std::make_shared<JsonConfig>(data_[key]);
        }
        return std::make_shared<JsonConfig>(json::object());
    }

private:
    json data_;
};

ConfigPtr ConfigManager::load_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + path);
    }

    json j;
    try {
        file >> j;
    } catch (const json::exception& e) {
        throw std::runtime_error("Failed to parse JSON: " + std::string(e.what()));
    }

    return std::make_shared<JsonConfig>(j);
}

ConfigPtr ConfigManager::load_from_string(const std::string& json_str) {
    json j;
    try {
        j = json::parse(json_str);
    } catch (const json::exception& e) {
        throw std::runtime_error("Failed to parse JSON: " + std::string(e.what()));
    }

    return std::make_shared<JsonConfig>(j);
}
