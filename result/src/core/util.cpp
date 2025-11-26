#include "util.h"
#include <ctime>
#include <sstream>
#include <iomanip>
#include <chrono>

/// @brief 获取当前日期 YYYYMMDD
int get_current_date() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now_c);
#else
    localtime_r(&now_c, &local_tm);
#endif
    
    int year = local_tm.tm_year + 1900;
    int month = local_tm.tm_mon + 1;
    int day = local_tm.tm_mday;
    
    return year * 10000 + month * 100 + day;
}

/// @brief 获取当前时间 HH:MM:SS
std::string get_current_time() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now_c);
#else
    localtime_r(&now_c, &local_tm);
#endif
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << local_tm.tm_hour << ":"
        << std::setfill('0') << std::setw(2) << local_tm.tm_min << ":"
        << std::setfill('0') << std::setw(2) << local_tm.tm_sec;
    
    return oss.str();
}

/// @brief 比较时间字符串 time1 >= time2
bool time_ge(const std::string& time1, const std::string& time2) {
    return time1 >= time2;
}

/// @brief 比较时间字符串 time1 <= time2
bool time_le(const std::string& time1, const std::string& time2) {
    return time1 <= time2;
}

