#include "exchange_service.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>

int main() {
  std::cout.setf(std::ios::unitbuf);

  std::string server_address{"0.0.0.0:9000"};

  ExchangeService service;
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();

  if (!server) {
    std::cerr << "failed to bind " << server_address
              << " (is another server already running?)\n";
    return 1;
  }

  std::cout << "listening on " << server_address << "\n";
  server->Wait();

  return 0;
}
