#include "event_convert.h"
#include "exchange.grpc.pb.h"
#include "order_book.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>

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

static int run_subscribe(exchange::Exchange::Stub &stub) {
  std::cout.setf(std::ios::unitbuf);

  exchange::SubscribeRequest request;
  request.set_depth(exchange::SubscribeRequest::DEPTH_L3);

  grpc::ClientContext context;
  std::unique_ptr<grpc::ClientReader<exchange::MarketDataEvent>> reader(
      stub.Subscribe(&context, request));

  OrderBook book;
  exchange::MarketDataEvent msg;
  uint64_t as_of = 0;
  uint64_t snapshot_orders = 0;
  bool first_delta = true;

  while (reader->Read(&msg)) {
    if (msg.snapshot()) {
      as_of = msg.seq();

      if (msg.event_case() == exchange::MarketDataEvent::EVENT_NOT_SET) {
        std::cout << "connected, snapshot as of seq " << as_of << "\n";
        continue;
      }

      book.apply(from_proto(msg));
      snapshot_orders++;
      std::cout << "snapshot order id " << msg.add().id() << " side "
                << (msg.add().side() == exchange::SIDE_BUY ? "BUY" : "SELL")
                << " price " << msg.add().price() << " size "
                << msg.add().size() << "\n";
      continue;
    }

    if (first_delta) {
      std::cout << "snapshot complete: " << snapshot_orders
                << " resting orders, waiting for events\n";
      first_delta = false;
    }

    book.apply(from_proto(msg));
    std::cout << "event seq " << msg.seq() << "\n";
  }

  grpc::Status status = reader->Finish();
  std::cout << book.dump();

  if (!status.ok()) {
    std::cout << status.error_message() << "\n";
    return 1;
  }

  return 0;
}

int main(int argc, char **argv) {
  std::cout.setf(std::ios::unitbuf);

  std::string target = argc > 1 ? argv[1] : "localhost:9000";
  auto channel =
      grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
  auto stub = exchange::Exchange::NewStub(channel);

  if (argc > 2 && std::string(argv[2]) == "subscribe") {
    return run_subscribe(*stub);
  }

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
