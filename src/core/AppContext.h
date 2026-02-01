#pragma once

#include "IMarketDataApi.h"
#include "ITradingApi.h"

#include <atomic>
#include <memory>
#include <mutex>

class SecTradingApi;

struct AppContext {
    std::shared_ptr<SecTradingApi> trading_raw;
    TradingApiPtr trading;
    std::shared_ptr<IMarketDataApi> market;

    std::atomic<bool> stop{false};

    // Market API has internal locks, but keep a coarse mutex for safety when
    // multiple modules call into snapshot/limits/history concurrently.
    std::mutex market_mutex;
};
