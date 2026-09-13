// A fake-market ExchangeClient, used to exercise the whole host <-> UI path before the real
// client exists (run with OB_DEMO_CLIENT=1). It's also a reference for the interface:
// a background thread keeps cached state current and pushes it through the Publisher, while
// the polled getters just return that cache under a mutex.
//
// It is NOT a matching engine: marketable orders fill completely at the touch, everything
// else rests until the random-walking book crosses it.
#include "demo_client.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <thread>

namespace ob {
namespace {

uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

class DemoClient final : public ExchangeClient {
public:
    std::string name() const override { return "Demo client (host/demo_client.cpp)"; }

    void start(Publisher& publish) override {
        publish_ = &publish;
        running_ = true;
        thread_ = std::thread([this] { loop(); });
        publish.status(true);
    }

    void stop() override {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    OrderResponse submitOrder(const NewOrderRequest& req) override {
        if (req.size == 0) throw ClientError(400, "size must be > 0");
        if (req.type != Type::MARKET && req.price == 0) throw ClientError(400, "price must be > 0");

        std::lock_guard<std::mutex> lock(mutex_);
        OrderResponse res;
        res.assigned_id = nextId_++;
        const uint64_t total = req.size + req.reserve;
        const bool buy = req.side == Side::BUY;
        const uint64_t touch = buy ? bestAsk() : bestBid();
        const bool crosses = req.type == Type::MARKET || (buy ? req.price >= touch : req.price <= touch);

        if (req.type == Type::BOC && crosses) {
            res.reason = "Post-only order would cross the book";
            return res;  // accepted = false, status = REJECTED
        }
        res.accepted = true;
        if (crosses) {
            res.trades.push_back(recordTrade(0, res.assigned_id, touch, total, req.side));
            settle(req.side, touch, total);
            res.filled_size = total;
            res.status = Status::FILLED;
        } else if (req.type == Type::IOC || req.type == Type::FOK || req.type == Type::MARKET) {
            res.status = Status::CANCELLED;
        } else {
            OpenOrder o;
            o.id = res.assigned_id;
            o.side = req.side;
            o.type = req.type;
            o.price = req.price;
            o.size = o.original_size = total;
            o.visible_size = req.size;
            o.ts_ms = nowMs();
            open_.push_back(o);
            res.remaining_size = total;
            res.status = Status::RESTING;
        }
        return res;
    }

    void cancelOrder(uint64_t id) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(open_.begin(), open_.end(), [&](const OpenOrder& o) { return o.id == id; });
        if (it == open_.end()) throw ClientError(404, "Order " + std::to_string(id) + " not found");
        open_.erase(it);
    }

    BookSnapshot getBook(int depth) override {
        std::lock_guard<std::mutex> lock(mutex_);
        BookSnapshot b = book_;
        if (depth >= 0) {
            b.bids.resize(std::min<size_t>(b.bids.size(), static_cast<size_t>(depth)));
            b.asks.resize(std::min<size_t>(b.asks.size(), static_cast<size_t>(depth)));
        }
        return b;
    }

    TradesPage getTrades(std::optional<uint64_t> after, int limit) override {
        std::lock_guard<std::mutex> lock(mutex_);
        TradesPage page;
        const size_t n = trades_.size();
        size_t start = after ? static_cast<size_t>(*after + 1)
                             : (n > static_cast<size_t>(limit) ? n - static_cast<size_t>(limit) : 0);
        for (size_t i = start; i < n && page.trades.size() < static_cast<size_t>(limit); ++i) page.trades.push_back(trades_[i]);
        page.next_seq = page.trades.empty() ? (after ? static_cast<int64_t>(*after) : static_cast<int64_t>(n) - 1)
                                            : static_cast<int64_t>(page.trades.back().seq);
        return page;
    }

    std::vector<OpenOrder> getOpenOrders() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return open_;
    }

    Account getAccount() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return accountLocked();
    }

private:
    uint64_t bestBid() const { return mid_ - 1; }
    uint64_t bestAsk() const { return mid_ + 1; }

    Trade recordTrade(uint64_t restingId, uint64_t incomingId, uint64_t price, uint64_t size, Side aggressor) {
        Trade t{trades_.size(), restingId, price, size, incomingId, aggressor, nowMs()};
        trades_.push_back(t);
        return t;
    }

    void settle(Side side, uint64_t price, uint64_t qty) {
        const int64_t notional = static_cast<int64_t>(price * qty);
        if (side == Side::BUY) {
            cash_ -= notional;
            position_ += static_cast<int64_t>(qty);
        } else {
            cash_ += notional;
            position_ -= static_cast<int64_t>(qty);
        }
    }

    Account accountLocked() const {
        Account a;
        a.cash = cash_;
        a.position = position_;
        for (const auto& o : open_) {
            if (o.side == Side::BUY) a.cash_reserved += static_cast<int64_t>(o.price * o.size);
            else a.position_reserved += static_cast<int64_t>(o.size);
        }
        a.avg_cost = 10000.0;
        a.starting_equity = 10'000'000 + 1'000 * 10'000;
        return a;
    }

    void rebuildBook() {
        book_.seq++;
        book_.ts_ms = nowMs();
        book_.bids.clear();
        book_.asks.clear();
        std::uniform_int_distribution<uint64_t> size(5, 400), count(1, 6);
        for (uint64_t d = 0; d < 25; ++d) {
            book_.bids.push_back({bestBid() - d, size(rng_), count(rng_)});
            book_.asks.push_back({bestAsk() + d, size(rng_), count(rng_)});
        }
    }

    void loop() {
        std::uniform_int_distribution<int> step(-1, 1), coin(0, 3);
        while (running_) {
            std::vector<Trade> fresh;
            BookSnapshot book;
            std::vector<OpenOrder> open;
            Account account;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                mid_ = static_cast<uint64_t>(static_cast<int64_t>(mid_) + step(rng_));
                rebuildBook();
                if (coin(rng_) == 0) {
                    Side aggressor = coin(rng_) < 2 ? Side::BUY : Side::SELL;
                    fresh.push_back(recordTrade(0, 0, aggressor == Side::BUY ? bestAsk() : bestBid(), 1 + coin(rng_) * 10, aggressor));
                }
                // Resting orders the book has moved through get filled as maker.
                for (auto it = open_.begin(); it != open_.end();) {
                    bool hit = it->side == Side::BUY ? bestAsk() <= it->price : bestBid() >= it->price;
                    if (!hit) {
                        ++it;
                        continue;
                    }
                    fresh.push_back(recordTrade(it->id, 0, it->price, it->size, it->side == Side::BUY ? Side::SELL : Side::BUY));
                    settle(it->side, it->price, it->size);
                    it = open_.erase(it);
                }
                book = book_;
                open = open_;
                account = accountLocked();
            }
            // Push outside the lock: the Publisher only queues work for the UI thread.
            publish_->book(book);
            if (!fresh.empty()) publish_->trades(fresh);
            publish_->openOrders(open);
            publish_->account(account);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    Publisher* publish_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::mt19937_64 rng_{42};
    uint64_t mid_ = 10000;
    uint64_t nextId_ = 1;
    BookSnapshot book_;
    std::vector<Trade> trades_;
    std::vector<OpenOrder> open_;
    int64_t cash_ = 10'000'000;  // $100,000.00 in ticks x qty
    int64_t position_ = 1'000;
};

}  // namespace

std::unique_ptr<ExchangeClient> makeDemoClient() { return std::make_unique<DemoClient>(); }

}  // namespace ob
