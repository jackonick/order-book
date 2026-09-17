#include "exchange.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>

int main() {
  auto channel =
      grpc::CreateChannel("localhost:9000", grpc::InsecureChannelCredentials());
  auto stub = exchange::Exchange::NewStub(channel);

  exchange::NewOrderRequest request;

  request.set_side(0);
  request.set_order_type(0);
  request.set_price(100);
  request.set_size(10);

  exchange::OrderResponse response;
  grpc::ClientContext context;

  grpc::Status status = stub->SubmitOrder(&context, request, &response);

  if (status.ok()) {
    std::cout << "id: " << response.assigned_id()
              << "status: " << response.accepted()
              << "filled size: " << response.filled_size() << "\n";
  }

  else {
    std::cout << status.error_message() << "\n";
  }

  request.set_side(1);
  request.set_order_type(0);
  request.set_price(100);
  request.set_size(6);

  exchange::OrderResponse response2;
  grpc::ClientContext context2;

  grpc::Status status2 = stub->SubmitOrder(&context2, request, &response2);

  if (status2.ok()) {
    std::cout << "id: " << response2.assigned_id()
              << "status: " << response2.accepted()
              << "filled size: " << response2.filled_size() << "\n";
  }

  else {
    std::cout << status.error_message() << "\n";
  }

  return 0;
}
