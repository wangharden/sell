#pragma once

#include "../core/AppContext.h"
#include "../core/Order.h"

#include <chrono>

class IModule {
public:
    virtual ~IModule() = default;

    virtual const char* name() const = 0;
    virtual std::chrono::milliseconds tick_interval() const = 0;

    virtual bool init(AppContext& ctx) = 0;
    virtual void tick(AppContext& ctx) = 0;
    virtual void on_order_event(AppContext& ctx, const OrderResult& result, int notify_type) = 0;
};

