#pragma once
// ============================================================================================
// The contract between the desktop UI and YOUR client program.
//
// The host (host/) opens the window and forwards every UI action to the ExchangeClient that
// makeClient() returns (client/my_client.cpp). You only ever see the C++ types below; the host
// converts them to and from what the UI needs, so there's no JSON or JavaScript on your side.
//
// Threading
//   Every method runs on a host worker thread, and several can run at once, so protect any
//   shared state (a std::mutex is fine; gRPC stubs are already thread-safe). Methods may block
//   (a gRPC round trip Houston -> NYC is ~48 ms); the window stays responsive meanwhile.
//
// Polling vs pushing
//   The UI polls getBook / getTrades / getOpenOrders / getAccount every 500 ms, so those should
//   return your latest cached state, not make a network call each time. For instant updates,
//   call the Publisher from your stream-reading thread whenever something changes.
//
// Errors
//   throw ob::ClientError{status, "message"} and the UI shows the message.
//   Status codes follow HTTP: 400 bad input, 404 unknown order, 409 conflict,
//   501 not implemented, 503 not connected.
//
// Units match order_book.h: prices are integer ticks (10012 = 100.12), sizes are integers,
// ids are uint64, timestamps are milliseconds since the Unix epoch.
// ============================================================================================

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ob {

enum class Side { BUY, SELL };
/** Same members, same order as `enum class Type` in order_book.h. */
enum class Type { GTC, FOK, MARKET, IOC, BOC, ICEBERG };
enum class Status { RESTING, PARTIALLY_FILLED, FILLED, CANCELLED, REJECTED };

struct NewOrderRequest {
    Side side = Side::BUY;
    Type type = Type::GTC;
    uint64_t price = 0;         // ticks; 0 for MARKET
    uint64_t size = 0;          // visible size (ICEBERG: the first slice)
    uint64_t reserve = 0;       // ICEBERG only: hidden quantity behind the slice
    uint64_t display_size = 0;  // ICEBERG only: slice size on each refill
};

/** Same fields as `struct Trade` in order_book.h, plus what the UI needs to show a tape. */
struct Trade {
    uint64_t seq = 0;            // index in the trade log, strictly increasing
    uint64_t resting_id = 0;
    uint64_t resting_price = 0;
    uint64_t trade_size = 0;
    uint64_t incoming_id = 0;
    Side aggressor = Side::BUY;  // side of the incoming order
    uint64_t ts_ms = 0;
};

struct Level {
    uint64_t price = 0;
    uint64_t size = 0;    // total visible size at this price
    uint64_t orders = 0;  // number of orders queued here
};

struct BookSnapshot {
    uint64_t seq = 0;          // book version; bump it on every change
    uint64_t ts_ms = 0;
    std::vector<Level> bids;   // best (highest) first
    std::vector<Level> asks;   // best (lowest) first
};

/** One order inside a price level's FIFO queue. */
struct QueueOrder {
    uint64_t id = 0;
    uint64_t size = 0;  // visible size
    uint64_t ts_ms = 0;
    bool mine = false;  // true for this user's orders
    bool iceberg = false;
};

struct LevelQueue {
    Side side = Side::BUY;
    uint64_t price = 0;
    std::vector<QueueOrder> orders;  // front of the queue (next to fill) first
};

struct TradesPage {
    std::vector<Trade> trades;  // oldest first
    int64_t next_seq = -1;      // seq of the last trade returned (or the `after` you got)
};

struct OpenOrder {
    uint64_t id = 0;
    Side side = Side::BUY;
    Type type = Type::GTC;
    uint64_t price = 0;
    uint64_t size = 0;           // remaining, visible + reserve
    uint64_t visible_size = 0;
    uint64_t original_size = 0;
    uint64_t filled_size = 0;
    uint64_t ts_ms = 0;
    Status status = Status::RESTING;
    std::optional<uint64_t> queue_position;  // 1-based place in its level's queue
    std::optional<uint64_t> size_ahead;      // visible size queued in front of it
};

/** modify_order (size) and modify_price (price). A size of 0 cancels. */
struct ModifyRequest {
    std::optional<uint64_t> size;
    std::optional<uint64_t> price;
};

struct Candle {
    int64_t time_sec = 0;  // bucket start, Unix seconds
    uint64_t open = 0, high = 0, low = 0, close = 0, volume = 0;
};

/** Spot account. Money is price ticks x quantity (cents at 2 price decimals). */
struct Account {
    int64_t cash = 0;
    int64_t cash_reserved = 0;      // locked by resting buys
    int64_t position = 0;
    int64_t position_reserved = 0;  // locked by resting sells
    std::optional<double> avg_cost; // ticks; empty when flat
    int64_t realized_pnl = 0;
    int64_t starting_equity = 0;
};

// ---- Match trace (optional): what the matching loop did, drives the replay window ----------

struct MatchEvent {
    enum class Kind { MATCH, ICEBERG_REFILL, LEVEL_CLEARED, REST, DROP, REJECT };
    Kind kind = Kind::MATCH;
    uint64_t resting_id = 0;         // MATCH, ICEBERG_REFILL
    uint64_t price = 0;              // MATCH, ICEBERG_REFILL, LEVEL_CLEARED, REST
    uint64_t qty = 0;                // MATCH
    uint64_t resting_remaining = 0;  // MATCH
    uint64_t size = 0;               // ICEBERG_REFILL, REST, DROP
    uint64_t reserve = 0;            // ICEBERG_REFILL
    uint64_t queue_position = 0;     // REST
    std::string reason;              // REJECT
};

struct TraceLevel {
    uint64_t price = 0;
    std::vector<QueueOrder> orders;
};

struct MatchTrace {
    uint64_t order_id = 0;
    Side side = Side::BUY;
    Type type = Type::GTC;
    uint64_t price = 0;
    uint64_t size = 0;                     // total, visible + reserve
    uint64_t ts_ms = 0;
    std::vector<TraceLevel> book_before;   // opposite side it could reach, before matching
    std::vector<MatchEvent> events;        // in the order they happened
};

struct OrderResponse {
    uint64_t assigned_id = 0;
    bool accepted = false;
    uint64_t filled_size = 0;
    uint64_t remaining_size = 0;       // left resting, visible + reserve
    Status status = Status::REJECTED;
    std::string reason;                // why, when !accepted
    std::vector<Trade> trades;         // fills this order produced on arrival
    std::optional<MatchTrace> trace;   // optional; enables the replay window
};

/** Throw this from any method to show an error in the UI. */
struct ClientError : std::runtime_error {
    int status;
    ClientError(int status_code, const std::string& message)
        : std::runtime_error(message), status(status_code) {}
};

/**
 * Pushes updates to the screen immediately. Safe to call from any thread (e.g. the one
 * reading your gRPC stream), as often as you like.
 */
class Publisher {
public:
    virtual ~Publisher() = default;
    virtual void book(const BookSnapshot& book) = 0;
    virtual void trades(const std::vector<Trade>& trades) = 0;  // oldest first
    virtual void openOrders(const std::vector<OpenOrder>& orders) = 0;
    virtual void account(const Account& account) = 0;
    /** Drives the Live / Offline indicator. */
    virtual void status(bool connected, const std::string& message = {}) = 0;
};

/**
 * Implement this in client/my_client.cpp. Everything has a safe default, so you can build it
 * up one method at a time: unimplemented market data shows as empty, unimplemented actions
 * show "not implemented" in the UI.
 */
class ExchangeClient {
public:
    virtual ~ExchangeClient() = default;

    /** Shown in the top bar tooltip. */
    virtual std::string name() const { return "C++ client"; }

    /** Called once on a background thread after the window opens: connect and start streams. */
    virtual void start(Publisher& publish) { (void)publish; }

    /** Called once when the window closes: stop streams and join your threads. */
    virtual void stop() {}

    // ---- orders ------------------------------------------------------------------------------
    virtual OrderResponse submitOrder(const NewOrderRequest& req) {
        (void)req;
        throw notImplemented("submitOrder");
    }
    virtual void cancelOrder(uint64_t id) {
        (void)id;
        throw notImplemented("cancelOrder");
    }
    /** Return the updated order, or std::nullopt if the change cancelled or filled it. */
    virtual std::optional<OpenOrder> modifyOrder(uint64_t id, const ModifyRequest& patch) {
        (void)id;
        (void)patch;
        throw notImplemented("modifyOrder");
    }

    // ---- market data (polled; return your latest cached state) --------------------------------
    virtual BookSnapshot getBook(int depth) {
        (void)depth;
        return {};
    }
    /** Trades with seq > after, oldest first, at most `limit`. No `after`: the latest `limit`. */
    virtual TradesPage getTrades(std::optional<uint64_t> after, int limit) {
        (void)after;
        (void)limit;
        return {};
    }
    virtual std::vector<OpenOrder> getOpenOrders() { return {}; }
    virtual LevelQueue getLevel(Side side, uint64_t price) { return {side, price, {}}; }
    /** Optional history for the chart; without it the chart builds bars from live trades. */
    virtual std::vector<Candle> getCandles(int interval_sec, int limit) {
        (void)interval_sec;
        (void)limit;
        throw notImplemented("getCandles");
    }

    // ---- account -----------------------------------------------------------------------------
    virtual Account getAccount() { throw notImplemented("getAccount"); }
    virtual Account resetAccount() { throw notImplemented("resetAccount"); }

protected:
    static ClientError notImplemented(const char* what) {
        return ClientError(501, std::string(what) + " isn't implemented in your client yet");
    }
};

/**
 * Defined in client/my_client.cpp. Return nullptr and the UI runs on its built-in simulator.
 */
std::unique_ptr<ExchangeClient> makeClient();

}  // namespace ob
