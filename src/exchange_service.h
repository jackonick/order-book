#pragma once
#include "exchange.grpc.pb.h"
#include "order_book.h"
#include <cstddef>
#include <cstdint>
#include <grpcpp/grpcpp.h>
#include <mutex>
#include <string>
#include <vector>

class ExchangeService final : public exchange::Exchange::Service {
public:
  grpc::Status SubmitOrder(grpc::ServerContext *context,
                           const exchange::NewOrderRequest *request,
                           exchange::OrderResponse *response) override;

  grpc::Status SubmitCancel(grpc::ServerContext *context,
                            const exchange::NewCancelRequest *request,
                            exchange::CancelResponse *response) override;

  grpc::Status SubmitModify(grpc::ServerContext *context,
                            const exchange::NewModifyOrder *request,
                            exchange::ModifyResponse *response) override;

  grpc::Status
  Subscribe(grpc::ServerContext *context,
            const exchange::SubscribeRequest *request,
            grpc::ServerWriter<exchange::MarketDataEvent> *writer) override;

  std::string dump_book();
  std::size_t subscriber_count();

private:
  struct Subscriber {
    grpc::ServerWriter<exchange::MarketDataEvent> *writer = nullptr;
    bool alive = true;
  };

  void broadcast_locked();

  OrderBook book;
  uint64_t counter{0};
  std::size_t published{0};
  std::vector<Subscriber *> subs;
  std::mutex mtx;
};
