#include "exchange.grpc.pb.h"
#include "order_book.h"
#include <grpcpp/grpcpp.h>
#include <iostream>

static const char *reason_str(uint32_t code) {
  switch (static_cast<Reason>(code)) {
  case Reason::FOK_NOT_FILLED:
    return "FOK_NOT_FILLED";
  case Reason::IOC_NOT_FILLED:
    return "IOC_NOT_FILLED";
  case Reason::ACCEPTED:
    return "ACCEPTED";
  case Reason::BOC_CANCELLED:
    return "BOC_CANCELLED";
  case Reason::MODIFY_FAILED:
    return "MODIFY_FAILED";
  case Reason::MODIFY_ACCEPTED:
    return "MODIFY_ACCEPTED";
  case Reason::CANCEL_FAILED:
    return "CANCEL_FAILED";
  case Reason::CANCEL_ACCEPTED:
    return "CANCEL_ACCEPTED";
  case Reason::NOTHING_TO_MODIFY:
    return "NOTHING_TO_MODIFY";
  }
  return "UNKNOWN";
}

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
              << " status: " << response.accepted()
              << " filled size: " << response.filled_size()
              << " reason: " << reason_str(response.reason()) << "\n";
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
              << " status: " << response2.accepted()
              << " filled size: " << response2.filled_size()
              << " reason: " << reason_str(response2.reason()) << "\n";
  }

  else {
    std::cout << status2.error_message() << "\n";
  }

  exchange::NewModifyOrder modify;
  modify.set_id(response.assigned_id());
  modify.set_size(8);

  exchange::ModifyResponse modify_response;
  grpc::ClientContext context3;

  grpc::Status status3 =
      stub->SubmitModify(&context3, modify, &modify_response);

  if (status3.ok()) {
    std::cout << "id: " << modify_response.id()
              << " status: " << modify_response.accepted()
              << " reason: " << reason_str(modify_response.reason()) << "\n";
  }

  else {
    std::cout << status3.error_message() << "\n";
  }

  exchange::NewModifyOrder empty_modify;
  empty_modify.set_id(response.assigned_id());

  exchange::ModifyResponse empty_modify_response;
  grpc::ClientContext context4;

  grpc::Status status4 =
      stub->SubmitModify(&context4, empty_modify, &empty_modify_response);

  if (status4.ok()) {
    std::cout << "id: " << empty_modify_response.id()
              << " status: " << empty_modify_response.accepted()
              << " reason: " << reason_str(empty_modify_response.reason())
              << "\n";
  }

  else {
    std::cout << status4.error_message() << "\n";
  }

  return 0;
}
