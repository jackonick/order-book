#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "exchange_client.hpp"
#include "webview/webview.h"
#include "worker_pool.hpp"

namespace ob {

/**
 * Connects the page to the ExchangeClient.
 *
 *   page -> C++   window.ob_call(method, payload) returns a Promise. Each call runs on a worker
 *                 thread, the payload is decoded into the structs in exchange_client.hpp, and
 *                 the result (or ClientError) is sent back as JSON.
 *   C++ -> page   The Bridge is the client's Publisher: every push becomes
 *                 window.__obPush({ type, data }), delivered on the UI thread.
 */
class Bridge final : public Publisher {
public:
    Bridge(webview::webview& w, ExchangeClient* client);
    ~Bridge() override;

    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    /** Bind window.ob_call. Call before navigating so it exists when the page loads. */
    void install();
    /** Run client->start(*this) on its own thread. */
    void startClient();
    /** Stop delivering to the page, stop the client, and join every thread. */
    void shutdown();

    // Publisher
    void book(const BookSnapshot& book) override;
    void trades(const std::vector<Trade>& trades) override;
    void openOrders(const std::vector<OpenOrder>& orders) override;
    void account(const Account& account) override;
    void status(bool connected, const std::string& message) override;

private:
    void handle(const std::string& seq, const std::string& request);
    /** Queue a serialized push; `cacheKey` keeps the latest of each kind for late subscribers. */
    void pushRaw(const std::string& cacheKey, const std::string& message);
    void subscribe();

    webview::webview& w_;
    ExchangeClient* client_;
    std::atomic<bool> alive_{true};
    std::unique_ptr<WorkerPool> pool_;
    std::thread startThread_;

    std::mutex pushMutex_;
    bool subscribed_ = false;
    std::map<std::string, std::string> latest_;  // push kind -> last message, replayed on subscribe
};

}  // namespace ob
