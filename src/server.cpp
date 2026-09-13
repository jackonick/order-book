#include "exchange.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include "order_book.h"
#include <iostream>

class serverclass final : public exchange::Exchange::Service {
    OrderBook book;
    uint64_t counter{ 0 };

    grpc::Status SubmitOrder(grpc::ServerContext* context,
        const exchange::NewOrderRequest* request,
        exchange::OrderResponse* response) override {
        
        counter++;

        std::cout << "side: " << request->side()
            << "type: " << request->order_type()
            << "price: " << request->price()
            << "size: " << request->size()
            << "reserve size: " << request->reserve()
            << "display size: " << request->display_size() << "\n";

        response->set_assigned_id(counter);
        response->set_accepted(true);
        response->set_filled_size(0);

        return grpc::Status::OK;
    }
};


int main() {
    std::string server_address{ "0.0.0.0:9000" };

    serverclass sc;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&sc);

    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    server->Wait();

    return 0;
}
