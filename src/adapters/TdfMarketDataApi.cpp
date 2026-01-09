#include "TdfMarketDataApi.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cctype>
#include <cmath>

// Windows API for Sleep
#ifdef _WIN32
#include <windows.h>
#endif

// TDF 头文件 - 使用相对于项目根目录的路径
// 假设TDF SDK在项目外部，需要在CMakeLists.txt中配置include路径
// 或者拷贝到项目的third_party目录
#include "TDFAPI.h"
#include "TDFAPIStruct.h"

// 静态实例 map（回调用）
static std::map<THANDLE, TdfMarketDataApi*> g_instance_map;
static std::mutex g_instance_mutex;

// 辅助函数：将TDF时间格式转换为字符串
static std::string TimeToString(int nTime) {
    int hour = nTime / 10000000;
    int minute = (nTime / 100000) % 100;
    int second = (nTime / 1000) % 100;
    int ms = nTime % 1000;
    
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", hour, minute, second, ms);
    return std::string(buf);
}

static int NormalizeToHhmmss(int tdf_time) {
    if (tdf_time <= 0) {
        return 0;
    }
    if (tdf_time > 235959) {
        return tdf_time / 1000;
    }
    return tdf_time;
}

static bool TryParseHhmmss(const std::string& time_str, int& out_hhmmss) {
    std::string digits;
    digits.reserve(time_str.size());
    for (char ch : time_str) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            digits.push_back(ch);
        }
    }
    if (digits.empty()) {
        return false;
    }

    long long raw_value = 0;
    try {
        raw_value = std::stoll(digits);
    } catch (const std::exception&) {
        return false;
    }

    if (digits.size() > 6) {
        raw_value /= 1000;  // HHMMSSmmm -> HHMMSS
    }
    if (raw_value <= 0 || raw_value > 235959) {
        return false;
    }

    out_hhmmss = static_cast<int>(raw_value);
    return true;
}

static double RoundToPrice(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::round(value * 100.0) / 100.0;
}

static std::string ExtractNumericCode(const std::string& wind_code) {
    auto pos = wind_code.find('.');
    if (pos == std::string::npos) {
        return wind_code;
    }
    return wind_code.substr(0, pos);
}

static bool ContainsSTToken(const char* raw, size_t len) {
    if (!raw) {
        return false;
    }
    std::string buffer;
    buffer.reserve(len);
    for (size_t i = 0; i < len && raw[i] != '\0'; ++i) {
        unsigned char ch = static_cast<unsigned char>(raw[i]);
        buffer.push_back(static_cast<char>(std::toupper(ch)));
    }
    return buffer.find("ST") != std::string::npos;
}

static bool IsStSecurity(const TDF_MARKET_DATA& data) {
    if (ContainsSTToken(data.chPrefix, sizeof(data.chPrefix))) {
        return true;
    }
    if (data.pCodeInfo && ContainsSTToken(data.pCodeInfo->chName, sizeof(data.pCodeInfo->chName))) {
        return true;
    }
    return false;
}

static double DeduceLimitRatio(const std::string& wind_code, const TDF_MARKET_DATA& data) {
    std::string numeric_code = ExtractNumericCode(wind_code);
    if (!numeric_code.empty()) {
        if (numeric_code.rfind("30", 0) == 0 || numeric_code.rfind("68", 0) == 0) {
            return 0.20;  // 创业板、科创板
        }
    }
    if (IsStSecurity(data)) {
        return 0.05;   // ST 股票 5% 涨跌幅
    }
    return 0.10;      // 默认 10%
}

static std::pair<double, double> BuildLimitFallback(double pre_close, double ratio) {
    if (pre_close <= 0.0 || ratio <= 0.0) {
        return {0.0, 0.0};
    }
    double up = RoundToPrice(pre_close * (1.0 + ratio));
    double down = RoundToPrice(pre_close * (1.0 - ratio));
    if (down < 0.0) {
        down = 0.0;
    }
    return {up, down};
}

TdfMarketDataApi::TdfMarketDataApi() 
    : tdf_handle_(nullptr), is_connected_(false), port_(0) {}

TdfMarketDataApi::~TdfMarketDataApi() {
    disconnect();
}

bool TdfMarketDataApi::connect(const std::string& host, int port,
                               const std::string& user, 
                               const std::string& password) {
    if (is_connected_) {
        std::cerr << "已连接" << std::endl;
        return false;
    }
    
    host_ = host;
    port_ = port;
    user_ = user.empty() ? "test" : user;
    password_ = password.empty() ? "test" : password;
    
    // 🔧 从成员变量获取 CSV 路径（通过 set_csv_path 设置）
    // 如果未设置，使用默认路径
    std::string csv_path = csv_path_.empty() ? "./config.csv" : csv_path_;
    subscription_list_ = GenerateSubscriptionList(csv_path);
    std::cout << "[TDF订阅] CSV: " << csv_path << std::endl;
    std::cout << "[TDF订阅] 股票列表: " << subscription_list_ << std::endl;
    
    // 设置TDF日志路径（在环境设置之前）
    TDF_SetLogPath("./log");
    
    // 环境设置（从 PDF）
    TDF_SetEnv(TDF_ENVIRON_HEART_BEAT_INTERVAL, 10);
    TDF_SetEnv(TDF_ENVIRON_MISSED_BEART_COUNT, 3);
    TDF_SetEnv(TDF_ENVIRON_OPEN_TIME_OUT, 30);
    
    // 配置（从 PDF）
    TDF_OPEN_SETTING_EXT settings;
    memset(&settings, 0, sizeof(settings));
    
    strncpy(settings.siServer[0].szIp, host_.c_str(), sizeof(settings.siServer[0].szIp) - 1);
    snprintf(settings.siServer[0].szPort, sizeof(settings.siServer[0].szPort), "%d", port_);
    strncpy(settings.siServer[0].szUser, user_.c_str(), sizeof(settings.siServer[0].szUser) - 1);
    strncpy(settings.siServer[0].szPwd, password_.c_str(), sizeof(settings.siServer[0].szPwd) - 1);
    settings.nServerNum = 1;
    
    // 回调
    settings.pfnMsgHandler = OnDataReceived;
    settings.pfnSysMsgNotify = OnSystemMessage;
    
    // 订阅：实时，市场 SZ/SH，只快照 + 逐笔
    settings.nTime = 0;
    settings.szMarkets = "SZ-2-0;SH-2-0";
    // 使用成员变量，保证指针有效
    settings.szSubScriptions = subscription_list_.c_str();
    // nTypeFlags: 0表示只要行情快照，DATA_TYPE_TRANSACTION(0x2)表示要逐笔成交
    settings.nTypeFlags = DATA_TYPE_TRANSACTION;  // 快照自动推送 + 逐笔成交
    
    TDF_ERR err = TDF_ERR_SUCCESS;
    tdf_handle_ = TDF_OpenExt(&settings, &err);
    
    if (err != TDF_ERR_SUCCESS) {
        // 重试（像测试代码）
        int retry = 0;
        while (err == TDF_ERR_NETWORK_ERROR && retry < 3) {
            retry++;
            std::cout << "重试 " << retry << "/3..." << std::endl;
            #ifdef _WIN32
            Sleep(3000);  // Windows: Sleep单位是毫秒
            #else
            std::this_thread::sleep_for(std::chrono::seconds(3));
            #endif
            tdf_handle_ = TDF_OpenExt(&settings, &err);
        }
        if (err != TDF_ERR_SUCCESS) {
            std::cerr << "连接失败: " << err << std::endl;
            return false;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(g_instance_mutex);
        g_instance_map[tdf_handle_] = this;
    }
    
    is_connected_ = true;
    std::cout << "连接成功" << std::endl;
    return true;
}

void TdfMarketDataApi::disconnect() {
    if (tdf_handle_) {
        TDF_Close(tdf_handle_);
        {
            std::lock_guard<std::mutex> lock(g_instance_mutex);
            g_instance_map.erase(tdf_handle_);
        }
        tdf_handle_ = nullptr;
    }
    is_connected_ = false;
}

bool TdfMarketDataApi::is_connected() const {
    return is_connected_;
}

MarketSnapshot TdfMarketDataApi::get_snapshot(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = snapshot_cache_.find(symbol);
    return (it != snapshot_cache_.end()) ? it->second : MarketSnapshot{};
}

std::pair<double, double> TdfMarketDataApi::get_limits(const std::string& symbol) {
    MarketSnapshot snap = get_snapshot(symbol);
    return {snap.high_limit, snap.low_limit};
}

std::pair<double, double> TdfMarketDataApi::get_auction_data(
    const std::string& symbol, const std::string& date, const std::string& end_time) {
    (void)date;

    int end_hhmmss = 0;
    if (!TryParseHhmmss(end_time, end_hhmmss)) {
        return {0.0, 0.0};
    }

    std::lock_guard<std::mutex> lock(cache_mutex_);

    double open_price = 0.0;

    // 优先使用快照：包含 nOpen 和累计成交额 iTurnover（集合竞价阶段策略需要的字段）
    auto snap_it = snapshot_cache_.find(symbol);
    if (snap_it != snapshot_cache_.end()) {
        const MarketSnapshot& snap = snap_it->second;
        if (snap.valid && snap.open > 0.0) {
            open_price = snap.open;
        }

        // 仅当快照时间 <= end_time 时，快照的 turnover 才能代表 end_time 时刻的累计成交额
        int snap_hhmmss = NormalizeToHhmmss(snap.timestamp);
        if (snap.valid && snap_hhmmss > 0 && snap_hhmmss <= end_hhmmss) {
            return {open_price, static_cast<double>(snap.turnover)};
        }
    }

    return {open_price, 0.0};
}

bool TdfMarketDataApi::subscribe(const std::vector<std::string>& symbols) {
    // TDF 不支持动态订阅；需重连。简单返回 true
    return true;
}

bool TdfMarketDataApi::unsubscribe(const std::vector<std::string>& symbols) {
    return true;
}

std::vector<MarketSnapshot> TdfMarketDataApi::get_history_ticks(
    const std::string& symbol,
    const std::string& start_time,
    const std::string& end_time
) {
    // TDF API 不直接支持历史tick查询
    // 这里返回空列表，实际应用中需要：
    // 1. 使用TDF回放功能（nTime参数）
    // 2. 或者从本地缓存的tick数据中过滤
    std::vector<MarketSnapshot> result;
    std::cerr << "Warning: get_history_ticks not implemented for TDF" << std::endl;
    return result;
}

// 回调
void TdfMarketDataApi::OnDataReceived(THANDLE hTdf, TDF_MSG* pMsgHead) {
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    auto it = g_instance_map.find(hTdf);
    if (it != g_instance_map.end()) {
        TdfMarketDataApi* instance = it->second;
        if (pMsgHead->nDataType == MSG_DATA_MARKET) {
            instance->HandleMarketData(pMsgHead);
        } else if (pMsgHead->nDataType == MSG_DATA_TRANSACTION) {
            instance->HandleTransactionData(pMsgHead);
        }
    }
}

void TdfMarketDataApi::OnSystemMessage(THANDLE hTdf, TDF_MSG* pSysMsg) {
    std::lock_guard<std::mutex> lock(g_instance_mutex);
    auto it = g_instance_map.find(hTdf);
    if (it != g_instance_map.end()) {
        it->second->HandleSystemMessage(pSysMsg);
    }
}

void TdfMarketDataApi::HandleSystemMessage(TDF_MSG* pSysMsg) {
    if (!pSysMsg) return;
    
    switch (pSysMsg->nDataType) {
        case MSG_SYS_CONNECT_RESULT: {
            TDF_CONNECT_RESULT* pResult = (TDF_CONNECT_RESULT*)pSysMsg->pData;
            if (pResult && pResult->nConnResult) {
                std::cout << "[TDF系统] 连接成功: " << pResult->szIp << ":" << pResult->szPort << std::endl;
            }
            break;
        }
        case MSG_SYS_LOGIN_RESULT: {
            TDF_LOGIN_RESULT* pResult = (TDF_LOGIN_RESULT*)pSysMsg->pData;
            if (pResult && pResult->nLoginResult) {
                std::cout << "[TDF系统] 登录成功: " << pResult->szInfo << std::endl;
            }
            break;
        }
        case MSG_SYS_CODETABLE_RESULT: {
            std::cout << "[TDF系统] 代码表接收完成，开始接收行情..." << std::endl;
            break;
        }
    }
}

void TdfMarketDataApi::HandleMarketData(TDF_MSG* pMsgHead) {
    if (!pMsgHead || !pMsgHead->pData) return;
    unsigned int count = pMsgHead->pAppHead->nItemCount;
    TDF_MARKET_DATA* pMarket = (TDF_MARKET_DATA*)pMsgHead->pData;
    
    // 注释掉频繁的回调日志，减少输出
    // std::cout << "[TDF回调] 收到 " << count << " 条行情数据" << std::endl;
    
    std::lock_guard<std::mutex> lock(cache_mutex_);
    for (unsigned int i = 0; i < count; ++i) {
        std::string symbol = pMarket[i].szWindCode;
        
        // 过滤：只处理6位股票代码（排除可转债、基金等）
        bool is_stock = false;
        if (symbol.length() >= 9) {  // 格式：600000.SH 或 000001.SZ
            std::string code = symbol.substr(0, 6);
            // 沪市股票：60xxxx, 68xxxx
            // 深市股票：00xxxx, 30xxxx
            if ((code[0] == '6' && (code[1] == '0' || code[1] == '8')) ||
                (code[0] == '0' && code[1] == '0') ||
                (code[0] == '3' && code[1] == '0')) {
                is_stock = true;
            }
        }
        
        // 只缓存股票数据，跳过可转债、基金等
        if (!is_stock) {
            continue;
        }
        
        MarketSnapshot& snap = snapshot_cache_[symbol];
        snap.valid = true;
        snap.symbol = symbol;
        
        // 时间信息（HHMMSSmmm格式，如93015000表示09:30:15.000）
        snap.timestamp = pMarket[i].nTime;
        
        // 基础价格（TDF价格字段单位是10000，需要除以10000转为元）
        snap.pre_close = pMarket[i].nPreClose / 10000.0;
        snap.open = pMarket[i].nOpen / 10000.0;
        snap.high = pMarket[i].nHigh / 10000.0;
        snap.low = pMarket[i].nLow / 10000.0;
        snap.last_price = pMarket[i].nMatch / 10000.0;  // nMatch是最新成交价
        
        // 涨跌停价格（TDF价格字段单位是10000，需要除以10000转为元）
        double high_limit = pMarket[i].nHighLimited / 10000.0;
        double low_limit = pMarket[i].nLowLimited / 10000.0;

        if (high_limit <= 0.0 || low_limit <= 0.0) {
            double ratio = DeduceLimitRatio(symbol, pMarket[i]);
            auto fallback_limits = BuildLimitFallback(snap.pre_close, ratio);
            if (high_limit <= 0.0) {
                high_limit = fallback_limits.first;
            }
            if (low_limit <= 0.0) {
                low_limit = fallback_limits.second;
            }
        }

        snap.high_limit = high_limit;
        snap.low_limit = low_limit;
        
        // 同时设置别名字段（保持兼容性）
        snap.up_limit = snap.high_limit;
        snap.down_limit = snap.low_limit;
        
        // 五档买盘（从买一到买五）
        snap.bid_price1 = pMarket[i].nBidPrice[0] / 10000.0;
        snap.bid_price2 = pMarket[i].nBidPrice[1] / 10000.0;
        snap.bid_price3 = pMarket[i].nBidPrice[2] / 10000.0;
        snap.bid_price4 = pMarket[i].nBidPrice[3] / 10000.0;
        snap.bid_price5 = pMarket[i].nBidPrice[4] / 10000.0;
        
        snap.bid_volume1 = pMarket[i].nBidVol[0];
        snap.bid_volume2 = pMarket[i].nBidVol[1];
        snap.bid_volume3 = pMarket[i].nBidVol[2];
        snap.bid_volume4 = pMarket[i].nBidVol[3];
        snap.bid_volume5 = pMarket[i].nBidVol[4];
        
        // 五档卖盘（从卖一到卖五）
        snap.ask_price1 = pMarket[i].nAskPrice[0] / 10000.0;
        snap.ask_price2 = pMarket[i].nAskPrice[1] / 10000.0;
        snap.ask_price3 = pMarket[i].nAskPrice[2] / 10000.0;
        snap.ask_price4 = pMarket[i].nAskPrice[3] / 10000.0;
        snap.ask_price5 = pMarket[i].nAskPrice[4] / 10000.0;
        
        snap.ask_volume1 = pMarket[i].nAskVol[0];
        snap.ask_volume2 = pMarket[i].nAskVol[1];
        snap.ask_volume3 = pMarket[i].nAskVol[2];
        snap.ask_volume4 = pMarket[i].nAskVol[3];
        snap.ask_volume5 = pMarket[i].nAskVol[4];
        
        // 成交信息
        snap.volume = pMarket[i].iVolume;
        snap.turnover = pMarket[i].iTurnover;
    }
}

void TdfMarketDataApi::HandleTransactionData(TDF_MSG* pMsgHead) {
    if (!pMsgHead || !pMsgHead->pData) return;
    unsigned int count = pMsgHead->pAppHead->nItemCount;
    TDF_TRANSACTION* pTrans = (TDF_TRANSACTION*)pMsgHead->pData;
    
    std::lock_guard<std::mutex> lock(cache_mutex_);
    for (unsigned int i = 0; i < count; ++i) {
        std::string symbol = pTrans[i].szWindCode;
        int tick_hhmmss = NormalizeToHhmmss(pTrans[i].nTime);
        if (tick_hhmmss <= 0) {
            continue;
        }

        bool is_stock = true;
        if (symbol.length() >= 9) {
            std::string code = symbol.substr(0, 6);
            is_stock = false;
            if ((code[0] == '6' && (code[1] == '0' || code[1] == '8')) ||
                (code[0] == '0' && code[1] == '0') ||
                (code[0] == '3' && code[1] == '0')) {
                is_stock = true;
            }
        }
        if (!is_stock) {
            continue;
        }

        // 如果设置了回调，调用回调函数
        if (transaction_callback_) {
            TransactionData td;
            td.symbol = symbol;
            td.timestamp = pTrans[i].nTime;
            td.price = pTrans[i].nPrice / 10000.0;
            td.volume = pTrans[i].nVolume;
            td.turnover = static_cast<double>(pTrans[i].nTurnover);
            td.bsf_flag = pTrans[i].nBSFlag;
            td.function_code = pTrans[i].chFunctionCode;
            transaction_callback_(td);
        }

        // 内置的调试日志（保留少量样本）
        if (!auction_tick_logged_ && tick_hhmmss >= 91500 && tick_hhmmss <= 92700) {
            double price = pTrans[i].nPrice / 10000.0;
            double amount_wan = static_cast<double>(pTrans[i].nTurnover) / 10000.0;
            std::cout << "[TDF] auction tick " << symbol
                      << " " << TimeToString(pTrans[i].nTime)
                      << " price=" << price
                      << " vol=" << pTrans[i].nVolume
                      << " amt_wan=" << amount_wan
                      << std::endl;
            auction_tick_logged_ = true;
        }

        if (tick_hhmmss >= 93000 && continuous_tick_logged_ < 10) {
            double price = pTrans[i].nPrice / 10000.0;
            double amount_wan = static_cast<double>(pTrans[i].nTurnover) / 10000.0;
            std::cout << "[TDF] continuous tick " << symbol
                      << " " << TimeToString(pTrans[i].nTime)
                      << " price=" << price
                      << " vol=" << pTrans[i].nVolume
                      << " amt_wan=" << amount_wan
                      << std::endl;
            continuous_tick_logged_++;
        }

        // 不再提前退出，让回调能处理所有数据
        // if (auction_tick_logged_ && continuous_tick_logged_ >= 10) {
        //     break;
        // }
    }
}

std::string TdfMarketDataApi::GenerateSubscriptionList(const std::string& csv_path) {
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        std::cerr << "[TDF错误] 无法打开CSV: " << csv_path << std::endl;
        return "";
    }
    
    std::string line, sub_list;
    bool header = true;
    while (std::getline(file, line)) {
        if (header) { header = false; continue; }
        if (line.empty()) continue;
        
        std::stringstream ss(line);
        std::string token;
        int col = 0;
        std::string symbol;
        
        while (std::getline(ss, token, ',')) {
            col++;
            if (col == 3) {  // SYMBOL 在第 3 列（0-based: col=2, 1-based: col=3）
                // 去除首尾空格
                token.erase(0, token.find_first_not_of(" \t\r\n"));
                token.erase(token.find_last_not_of(" \t\r\n") + 1);
                symbol = token;
                break;
            }
        }
        
        if (symbol.empty() || symbol.length() != 6) continue;
        
        // 转换为 Wind 代码格式：6 开头 -> .SH，其他 -> .SZ
        std::string windCode = symbol;
        if (symbol[0] == '6') {
            windCode += ".SH";
        } else {
            windCode += ".SZ";
        }
        
        if (!sub_list.empty()) sub_list += ";";
        sub_list += windCode;
    }
    
    std::cout << "[TDF订阅] 从CSV读取 " << (sub_list.empty() ? 0 : std::count(sub_list.begin(), sub_list.end(), ';') + 1) << " 只股票" << std::endl;
    return sub_list;
}
