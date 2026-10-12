#include "event_convert.h"
#include "exchange_service.h"
#include "order_book.h"
#include <atomic>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

struct Harness {
  ExchangeService service;
  std::unique_ptr<grpc::Server> server;
  std::unique_ptr<exchange::Exchange::Stub> stub;

  Harness() {
    grpc::ServerBuilder builder;
    builder.RegisterService(&service);
    server = builder.BuildAndStart();
    stub = exchange::Exchange::NewStub(
        server->InProcessChannel(grpc::ChannelArguments()));
  }

  ~Harness() {
    server->Shutdown();
    server->Wait();
  }

  uint64_t submit(uint32_t side, uint64_t price, uint64_t size) {
    exchange::NewOrderRequest request;
    request.set_side(side);
    request.set_order_type(0);
    request.set_price(price);
    request.set_size(size);

    exchange::OrderResponse response;
    grpc::ClientContext context;
    stub->SubmitOrder(&context, request, &response);
    return response.assigned_id();
  }
};

} // namespace

TEST(Subscribe, SnapshotThenFirstDeltaHasNoGap) {
  Harness h;

  for (int i = 0; i < 20; i++) {
    h.submit(i % 2, 100 + (i % 5), 10 + i);
  }

  std::atomic<bool> running{true};
  std::thread flow([&] {
    uint64_t n = 0;
    while (running) {
      h.submit(n % 2, 100 + (n % 5), 5 + (n % 7));
      n++;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  for (int round = 0; round < 10; round++) {
    exchange::SubscribeRequest request;
    request.set_depth(exchange::SubscribeRequest::DEPTH_L3);

    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientReader<exchange::MarketDataEvent>> reader(
        h.stub->Subscribe(&context, request));

    exchange::MarketDataEvent msg;
    bool have_header = false;
    uint64_t as_of = 0;
    uint64_t expected = 0;
    int deltas = 0;

    while (reader->Read(&msg)) {
      if (msg.snapshot()) {
        if (!have_header) {
          as_of = msg.seq();
          expected = as_of;
          have_header = true;
        }
        EXPECT_EQ(msg.seq(), as_of);
        continue;
      }

      EXPECT_EQ(msg.seq(), expected);
      expected++;
      deltas++;

      if (deltas == 5) {
        break;
      }
    }

    EXPECT_TRUE(have_header);
    EXPECT_EQ(deltas, 5);

    context.TryCancel();
    reader->Finish();
  }

  running = false;
  flow.join();
}

TEST(Subscribe, ClientBookMatchesServer) {
  Harness h;

  h.submit(0, 100, 50);
  h.submit(0, 100, 30);
  h.submit(0, 99, 40);
  h.submit(1, 101, 25);
  h.submit(1, 100, 60);

  exchange::SubscribeRequest request;
  request.set_depth(exchange::SubscribeRequest::DEPTH_L3);

  grpc::ClientContext context;
  std::unique_ptr<grpc::ClientReader<exchange::MarketDataEvent>> reader(
      h.stub->Subscribe(&context, request));

  OrderBook replica;
  std::mutex replica_mtx;

  std::thread consumer([&] {
    exchange::MarketDataEvent msg;
    while (reader->Read(&msg)) {
      if (msg.event_case() == exchange::MarketDataEvent::EVENT_NOT_SET) {
        continue;
      }

      std::lock_guard<std::mutex> lock(replica_mtx);
      replica.apply(from_proto(msg));
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  h.submit(1, 99, 10);
  h.submit(0, 103, 15);
  h.submit(1, 98, 200);

  std::string server_dump;
  std::string client_dump;
  bool matched = false;

  for (int i = 0; i < 200; i++) {
    server_dump = h.service.dump_book();

    {
      std::lock_guard<std::mutex> lock(replica_mtx);
      client_dump = replica.dump();
    }

    if (server_dump == client_dump) {
      matched = true;
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  context.TryCancel();
  consumer.join();
  reader->Finish();

  EXPECT_TRUE(matched);
  EXPECT_EQ(client_dump, server_dump);
}

TEST(Subscribe, DisconnectRemovesSubscriber) {
  Harness h;

  h.submit(0, 100, 10);

  exchange::SubscribeRequest request;
  grpc::ClientContext context;
  std::unique_ptr<grpc::ClientReader<exchange::MarketDataEvent>> reader(
      h.stub->Subscribe(&context, request));

  std::atomic<int> received{0};
  std::thread consumer([&] {
    exchange::MarketDataEvent msg;
    while (reader->Read(&msg)) {
      received++;
    }
  });

  for (int i = 0; i < 200 && h.service.subscriber_count() == 0; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_EQ(h.service.subscriber_count(), 1u);
  EXPECT_GT(received.load(), 0);

  context.TryCancel();
  consumer.join();
  reader->Finish();

  for (int i = 0; i < 200 && h.service.subscriber_count() != 0; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_EQ(h.service.subscriber_count(), 0u);

  h.submit(1, 105, 10);
}
