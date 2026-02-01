#include "QueuedTradingApi.h"

#include <utility>

QueuedTradingApi::QueuedTradingApi(std::shared_ptr<ITradingApi> inner)
    : inner_(std::move(inner)) {
    worker_ = std::thread([this]() { worker_loop(); });
}

QueuedTradingApi::~QueuedTradingApi() {
    shutdown();
}

bool QueuedTradingApi::connect(const std::string& host, int port,
                              const std::string& user, const std::string& password) {
    return submit([this, host, port, user, password]() {
        return inner_->connect(host, port, user, password);
    }).get();
}

void QueuedTradingApi::disconnect() {
    submit([this]() { inner_->disconnect(); }).get();
}

bool QueuedTradingApi::is_connected() const {
    return submit([this]() { return inner_->is_connected(); }).get();
}

std::string QueuedTradingApi::place_order(const OrderRequest& req) {
    return submit([this, req]() { return inner_->place_order(req); }).get();
}

bool QueuedTradingApi::cancel_order(const std::string& order_id) {
    return submit([this, order_id]() { return inner_->cancel_order(order_id); }).get();
}

std::vector<Position> QueuedTradingApi::query_positions() {
    return submit([this]() { return inner_->query_positions(); }).get();
}

std::vector<OrderResult> QueuedTradingApi::query_orders() {
    return submit([this]() { return inner_->query_orders(); }).get();
}

void QueuedTradingApi::shutdown() {
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        worker = std::move(worker_);
    }
    cv_.notify_all();
    if (worker.joinable()) {
        worker.join();
    }
}

void QueuedTradingApi::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) {
                break;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        task();
    }
}

