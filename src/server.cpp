#include "exchange.grpc.pb.h"
#include "order_book.h"
#include <chrono>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <mutex>

class serverclass final : public exchange::Exchange::Service {
  OrderBook book;
  uint64_t counter{0};
  std::mutex mtx;

  grpc::Status SubmitOrder(grpc::ServerContext *context,
                           const exchange::NewOrderRequest *request,
                           exchange::OrderResponse *response) override {
    std::lock_guard<std::mutex> lock(mtx);
    counter++;

    Order o;
    o.price = request->price();
    o.size = request->size();
    o.reserve = request->reserve();
    o.display_size = request->display_size();

    o.side = static_cast<Side>(request->side());
    o.type = static_cast<Type>(
        request
            ->order_type()); // make sure to confirm range for enum conversion

    auto now = std::chrono::steady_clock::now();
    uint64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      now.time_since_epoch())
                      .count();

    o.id = counter;
    o.timestamp = ts;

    Outcome outcome = book.add_order(o);

    response->set_assigned_id(outcome.assigned_id);
    response->set_accepted(outcome.reason == Reason::ACCEPTED);
    response->set_reason(static_cast<uint32_t>(outcome.reason));
    response->set_filled_size(outcome.quantity_filled);

    return grpc::Status::OK;
  }

  grpc::Status SubmitCancel(grpc::ServerContext *context,
                            const exchange::NewCancelRequest *request,
                            exchange::CancelResponse *response) override {
    std::lock_guard<std::mutex> lock(mtx);

    Outcome2 outcome = book.cancel_id(request->id());

    response->set_reason(static_cast<uint32_t>(outcome.reason));

    return grpc::Status::OK;
  }

  grpc::Status SubmitModify(grpc::ServerContext *context,
                            const exchange::NewModifyOrder *request,
                            exchange::ModifyResponse *response) override {

    std::lock_guard<std::mutex> lock(mtx);

    Outcome2 outcome;
    if (request->has_price()) {
      outcome = book.modify_price(request->id(), request->price());
    }

    if (request->has_size()) {
      outcome = book.modify_size(request->id(), request->size());
    }

    response->set_reason(static_cast<uint32_t>(outcome.reason));

    return grpc::Status::OK;
  }
};

int main() {
  std::string server_address{"0.0.0.0:9000"};

  serverclass sc;
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&sc);

  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  server->Wait();

  return 0;
}
