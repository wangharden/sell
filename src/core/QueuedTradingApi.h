#pragma once

#include "ITradingApi.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

/// @brief Single-threaded wrapper for any ITradingApi implementation.
///
/// All trading calls are executed on one worker thread to avoid SDK
/// thread-safety issues when modules run concurrently.
class QueuedTradingApi final : public ITradingApi {
public:
    explicit QueuedTradingApi(std::shared_ptr<ITradingApi> inner);
    ~QueuedTradingApi() override;

    QueuedTradingApi(const QueuedTradingApi&) = delete;
    QueuedTradingApi& operator=(const QueuedTradingApi&) = delete;

    bool connect(const std::string& host, int port,
                 const std::string& user, const std::string& password) override;
    void disconnect() override;
    bool is_connected() const override;

    std::string place_order(const OrderRequest& req) override;
    bool cancel_order(const std::string& order_id) override;
    std::vector<Position> query_positions() override;
    std::vector<OrderResult> query_orders() override;

    void shutdown();

private:
    template <typename Func>
    auto submit(Func func) const -> std::future<decltype(func())> {
        using ResultT = decltype(func());
        auto task = std::make_shared<std::packaged_task<ResultT()>>(std::move(func));
        auto future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                std::promise<ResultT> promise;
                promise.set_exception(std::make_exception_ptr(
                    std::runtime_error("QueuedTradingApi is stopping")));
                return promise.get_future();
            }
            tasks_.emplace_back([task]() { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

    void worker_loop();

    std::shared_ptr<ITradingApi> inner_;

    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    mutable std::deque<std::function<void()>> tasks_;
    bool stopping_ = false;
    std::thread worker_;
};

