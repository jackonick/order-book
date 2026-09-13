#include "exchange.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>


int main() {
    auto channel = grpc::CreateChannel("localhost:9000", grpc::InsecureChannelCredentials());
    auto stub = exchange::Exchange::NewStub(channel);

    exchange::NewOrderRequest request;

    request.set_side(0);
    request.set_price(100);
    
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




    return 0;
}