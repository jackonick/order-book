// ============================================================================================
// YOUR client. The desktop app calls these methods; you implement them with gRPC calls to the
// server in NYC. Everything outside client/ and include/ is the host; you shouldn't need it.
//
// Suggested order:
//   1. Return a MyClient from makeClient() (bottom of this file) and rebuild. The top bar
//      switches to "C++ CLIENT" and the UI now talks to this class instead of the simulator.
//   2. start(): open the gRPC channel, then publish status(true).
//   3. submitOrder(): call your SubmitOrder RPC and map the reply back.
//   4. A stream-reading thread that keeps book_ / trades_ up to date and pushes them through
//      publish_ so the screen updates immediately.
//   5. cancelOrder / modifyOrder / getOpenOrders / getAccount as your server grows them.
//
// Anything you don't override keeps a safe default (see exchange_client.hpp).
// host/demo_client.cpp is a working example of the whole interface against a fake market.
// ============================================================================================
#include "exchange_client.hpp"

#include <mutex>

namespace ob {
namespace {

class MyClient final : public ExchangeClient {
public:
    std::string name() const override { return "My client"; }

    void start(Publisher& publish) override {
        publish_ = &publish;
        // TODO: create the channel once and keep it open for the app's lifetime, e.g.
        //   channel_ = grpc::CreateChannel("<vps-ip>:50051", credentials);
        //   stub_ = exchange::Exchange::NewStub(channel_);
        // TODO: start a thread that reads your market-data stream and, per update,
        //   locks mutex_, updates book_, and calls publish_->book(book_).
        publish.status(false, "MyClient::start() isn't written yet");
    }

    void stop() override {
        // TODO: cancel your streams (ClientContext::TryCancel) and join your threads.
    }

    OrderResponse submitOrder(const NewOrderRequest& req) override {
        // TODO: copy req into your exchange::NewOrderRequest, call stub_->SubmitOrder(...),
        //   and copy the reply (assigned_id, accepted, filled_size, ...) into an OrderResponse.
        (void)req;
        throw ClientError(501, "MyClient::submitOrder isn't written yet");
    }

    BookSnapshot getBook(int depth) override {
        (void)depth;
        std::lock_guard<std::mutex> lock(mutex_);
        return book_;  // kept current by your stream thread
    }

private:
    Publisher* publish_ = nullptr;
    std::mutex mutex_;
    BookSnapshot book_;
};

}  // namespace

std::unique_ptr<ExchangeClient> makeClient() {
    // nullptr = the UI keeps its built-in simulator (what you see today).
    // When you start on the client, switch to:
    //     return std::make_unique<MyClient>();
    return nullptr;
}

}  // namespace ob
