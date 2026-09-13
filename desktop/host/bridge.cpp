#include "bridge.hpp"

#include <iostream>

#include <nlohmann/json.hpp>

#include "webview/webview.h"

#ifndef OB_APP_VERSION
#define OB_APP_VERSION "dev"
#endif

using nlohmann::json;

// ---- JSON <-> exchange_client.hpp -------------------------------------------------------------
// Field names match the UI's schemas (web/src/lib/api/schemas.ts and web/API.md). ids travel as
// strings because JSON numbers are doubles and can't hold every uint64 exactly.

namespace ob {

NLOHMANN_JSON_SERIALIZE_ENUM(Side, {{Side::BUY, "BUY"}, {Side::SELL, "SELL"}})
NLOHMANN_JSON_SERIALIZE_ENUM(Type, {{Type::GTC, "GTC"},
                                    {Type::FOK, "FOK"},
                                    {Type::MARKET, "MARKET"},
                                    {Type::IOC, "IOC"},
                                    {Type::BOC, "BOC"},
                                    {Type::ICEBERG, "ICEBERG"}})
NLOHMANN_JSON_SERIALIZE_ENUM(Status, {{Status::RESTING, "RESTING"},
                                      {Status::PARTIALLY_FILLED, "PARTIALLY_FILLED"},
                                      {Status::FILLED, "FILLED"},
                                      {Status::CANCELLED, "CANCELLED"},
                                      {Status::REJECTED, "REJECTED"}})

namespace {

std::string idString(uint64_t id) { return std::to_string(id); }

uint64_t parseId(const json& j) {
    if (j.is_string()) return std::stoull(j.get<std::string>());
    return j.get<uint64_t>();
}

std::optional<uint64_t> optionalUint(const json& obj, const char* key) {
    if (!obj.contains(key) || obj[key].is_null()) return std::nullopt;
    return obj[key].get<uint64_t>();
}

std::string dump(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

}  // namespace

void from_json(const json& j, NewOrderRequest& r) {
    j.at("side").get_to(r.side);
    j.at("order_type").get_to(r.type);
    r.price = j.value("price", uint64_t{0});
    r.size = j.at("size").get<uint64_t>();
    r.reserve = j.value("reserve", uint64_t{0});
    r.display_size = j.value("display_size", uint64_t{0});
}

void to_json(json& j, const Trade& t) {
    j = {{"seq", t.seq},
         {"resting_id", idString(t.resting_id)},
         {"incoming_id", idString(t.incoming_id)},
         {"resting_price", t.resting_price},
         {"trade_size", t.trade_size},
         {"aggressor", t.aggressor},
         {"ts", t.ts_ms}};
}

void to_json(json& j, const Level& l) { j = {{"price", l.price}, {"size", l.size}, {"orders", l.orders}}; }

void to_json(json& j, const BookSnapshot& b) {
    j = {{"seq", b.seq}, {"ts", b.ts_ms}, {"bids", b.bids}, {"asks", b.asks}};
}

void to_json(json& j, const QueueOrder& q) {
    j = {{"id", idString(q.id)}, {"size", q.size}, {"ts", q.ts_ms}, {"mine", q.mine}, {"iceberg", q.iceberg}};
}

void to_json(json& j, const LevelQueue& q) { j = {{"side", q.side}, {"price", q.price}, {"orders", q.orders}}; }

void to_json(json& j, const TradesPage& p) { j = {{"trades", p.trades}, {"next_seq", p.next_seq}}; }

void to_json(json& j, const OpenOrder& o) {
    j = {{"id", idString(o.id)},
         {"side", o.side},
         {"order_type", o.type},
         {"price", o.price},
         {"size", o.size},
         {"visible_size", o.visible_size},
         {"original_size", o.original_size},
         {"filled_size", o.filled_size},
         {"ts", o.ts_ms},
         {"status", o.status}};
    if (o.queue_position) j["queue_position"] = *o.queue_position;
    if (o.size_ahead) j["size_ahead"] = *o.size_ahead;
}

void to_json(json& j, const Candle& c) {
    j = {{"time", c.time_sec}, {"open", c.open}, {"high", c.high}, {"low", c.low}, {"close", c.close}, {"volume", c.volume}};
}

void to_json(json& j, const Account& a) {
    j = {{"cash", a.cash},
         {"cash_reserved", a.cash_reserved},
         {"position", a.position},
         {"position_reserved", a.position_reserved},
         {"avg_cost", a.avg_cost ? json(*a.avg_cost) : json(nullptr)},
         {"realized_pnl", a.realized_pnl},
         {"starting_equity", a.starting_equity}};
}

void to_json(json& j, const MatchEvent& e) {
    using K = MatchEvent::Kind;
    switch (e.kind) {
        case K::MATCH:
            j = {{"type", "MATCH"}, {"resting_id", idString(e.resting_id)}, {"price", e.price}, {"qty", e.qty}, {"resting_remaining", e.resting_remaining}};
            break;
        case K::ICEBERG_REFILL:
            j = {{"type", "ICEBERG_REFILL"}, {"resting_id", idString(e.resting_id)}, {"price", e.price}, {"size", e.size}, {"reserve", e.reserve}};
            break;
        case K::LEVEL_CLEARED:
            j = {{"type", "LEVEL_CLEARED"}, {"price", e.price}};
            break;
        case K::REST:
            j = {{"type", "REST"}, {"price", e.price}, {"size", e.size}, {"queue_position", e.queue_position}};
            break;
        case K::DROP:
            j = {{"type", "DROP"}, {"size", e.size}};
            break;
        case K::REJECT:
            j = {{"type", "REJECT"}, {"reason", e.reason}};
            break;
    }
}

void to_json(json& j, const TraceLevel& l) { j = {{"price", l.price}, {"orders", l.orders}}; }

void to_json(json& j, const MatchTrace& t) {
    j = {{"order_id", idString(t.order_id)},
         {"side", t.side},
         {"order_type", t.type},
         {"price", t.price},
         {"size", t.size},
         {"ts", t.ts_ms},
         {"book_before", t.book_before},
         {"events", t.events}};
}

void to_json(json& j, const OrderResponse& r) {
    j = {{"assigned_id", idString(r.assigned_id)},
         {"accepted", r.accepted},
         {"filled_size", r.filled_size},
         {"remaining_size", r.remaining_size},
         {"status", r.status},
         {"trades", r.trades}};
    if (!r.reason.empty()) j["reason"] = r.reason;
    if (r.trace) j["trace"] = *r.trace;
}

// ---- dispatch ---------------------------------------------------------------------------------

namespace {

json callClient(ExchangeClient* client, const std::string& method, const json& payload) {
    const json p = payload.is_object() ? payload : json::object();

    if (method == "hello") {
        return {{"client", client != nullptr}, {"name", client ? client->name() : std::string()}, {"version", OB_APP_VERSION}};
    }
    if (!client) throw ClientError(503, "No C++ client is wired in (makeClient() returned nullptr)");

    if (method == "health") return {{"ok", true}};
    if (method == "submitOrder") return client->submitOrder(p.get<NewOrderRequest>());
    if (method == "cancelOrder") {
        client->cancelOrder(parseId(p.at("id")));
        return nullptr;
    }
    if (method == "modifyOrder") {
        ModifyRequest patch{optionalUint(p, "size"), optionalUint(p, "price")};
        auto updated = client->modifyOrder(parseId(p.at("id")), patch);
        return updated ? json(*updated) : json(nullptr);
    }
    if (method == "getBook") return client->getBook(p.value("depth", 50));
    if (method == "getLevel") return client->getLevel(p.at("side").get<Side>(), p.at("price").get<uint64_t>());
    if (method == "getTrades") {
        std::optional<uint64_t> after;
        if (p.contains("after") && p["after"].is_number_integer() && p["after"].get<int64_t>() >= 0) {
            after = p["after"].get<uint64_t>();
        }
        return client->getTrades(after, p.value("limit", 100));
    }
    if (method == "getOpenOrders") return client->getOpenOrders();
    if (method == "getCandles") return client->getCandles(p.value("interval", 60), p.value("limit", 300));
    if (method == "getAccount") return client->getAccount();
    if (method == "resetAccount") return client->resetAccount();
    throw ClientError(404, "Unknown method: " + method);
}

json errorBody(int status, const std::string& message) { return {{"status", status}, {"message", message}}; }

}  // namespace

// ---- Bridge -----------------------------------------------------------------------------------

Bridge::Bridge(webview::webview& w, ExchangeClient* client)
    : w_(w), client_(client), pool_(std::make_unique<WorkerPool>(4)) {}

Bridge::~Bridge() { shutdown(); }

void Bridge::install() {
    w_.bind(
        "ob_call",
        [this](std::string seq, std::string request, void*) { handle(seq, request); },
        nullptr);
}

void Bridge::handle(const std::string& seq, const std::string& request) {
    pool_->post([this, seq, request] {
        int status = 0;
        std::string body;
        try {
            const json args = json::parse(request);  // [method, payload]
            const std::string method = args.at(0).get<std::string>();
            const json payload = args.size() > 1 ? args.at(1) : json();
            if (method == "subscribe") {
                subscribe();
                body = "null";
            } else {
                body = dump(callClient(client_, method, payload));
            }
        } catch (const ClientError& e) {
            status = 1;
            body = dump(errorBody(e.status, e.what()));
        } catch (const json::exception& e) {
            status = 1;
            body = dump(errorBody(400, std::string("Bad request or response field: ") + e.what()));
        } catch (const std::exception& e) {
            status = 1;
            body = dump(errorBody(500, e.what()));
        }
        if (alive_) w_.resolve(seq, status, body);
    });
}

void Bridge::startClient() {
    if (!client_) return;
    startThread_ = std::thread([this] {
        try {
            client_->start(*this);
        } catch (const std::exception& e) {
            std::cerr << "[orderbook-app] client start() threw: " << e.what() << "\n";
            status(false, std::string("start() failed: ") + e.what());
        }
    });
}

void Bridge::shutdown() {
    if (!alive_.exchange(false) && !pool_) return;
    if (startThread_.joinable()) startThread_.join();
    if (client_) {
        try {
            client_->stop();
        } catch (const std::exception& e) {
            std::cerr << "[orderbook-app] client stop() threw: " << e.what() << "\n";
        }
        client_ = nullptr;
    }
    pool_.reset();
}

void Bridge::subscribe() {
    std::map<std::string, std::string> replay;
    {
        std::lock_guard<std::mutex> lock(pushMutex_);
        subscribed_ = true;
        replay = latest_;
    }
    for (const auto& [kind, message] : replay) {
        (void)kind;
        pushRaw({}, message);
    }
}

void Bridge::pushRaw(const std::string& cacheKey, const std::string& message) {
    if (!alive_) return;
    {
        std::lock_guard<std::mutex> lock(pushMutex_);
        if (!cacheKey.empty()) latest_[cacheKey] = message;
        if (!subscribed_) return;  // the page isn't listening yet; subscribe() replays the latest
    }
    std::string js = "window.__obPush && window.__obPush(" + message + ");";
    w_.dispatch([this, js = std::move(js)] {
        if (alive_) w_.eval(js);
    });
}

void Bridge::book(const BookSnapshot& book) { pushRaw("book", dump({{"type", "book"}, {"data", book}})); }

void Bridge::trades(const std::vector<Trade>& trades) {
    // Not cached: the UI fetches missed trades by cursor on its next poll.
    pushRaw({}, dump({{"type", "trades"}, {"data", trades}}));
}

void Bridge::openOrders(const std::vector<OpenOrder>& orders) {
    pushRaw("orders", dump({{"type", "orders"}, {"data", orders}}));
}

void Bridge::account(const Account& account) { pushRaw("account", dump({{"type", "account"}, {"data", account}})); }

void Bridge::status(bool connected, const std::string& message) {
    json data = {{"connected", connected}};
    if (!message.empty()) data["message"] = message;
    pushRaw("status", dump({{"type", "status"}, {"data", data}}));
}

}  // namespace ob
