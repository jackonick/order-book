cmake_minimum_required(VERSION 3.14)
project(order_book)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(orderbook main.cpp order_book.cpp)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(tests tests.cpp order_book.cpp)
target_link_libraries(tests GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(tests)


# --- Google Benchmark ---
FetchContent_Declare(
  benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

add_executable(benchmarks benchmark.cpp order_book.cpp)
target_link_libraries(benchmarks benchmark::benchmark)


# --- gRPC / Protobuf ---
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/exchange.proto")

set(PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/exchange.pb.cc")
set(PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/exchange.pb.h")
set(GRPC_SRCS  "${CMAKE_CURRENT_BINARY_DIR}/exchange.grpc.pb.cc")
set(GRPC_HDRS  "${CMAKE_CURRENT_BINARY_DIR}/exchange.grpc.pb.h")

add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
  COMMAND protobuf::protoc
  ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
       --cpp_out  "${CMAKE_CURRENT_BINARY_DIR}"
       -I "${CMAKE_CURRENT_SOURCE_DIR}"
       --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
       "${PROTO_FILE}"
  DEPENDS "${PROTO_FILE}"
)

add_library(exchange_proto ${PROTO_SRCS} ${GRPC_SRCS})
target_include_directories(exchange_proto PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(exchange_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)

add_executable(server server.cpp order_book.cpp)
target_link_libraries(server exchange_proto)

add_executable(client client.cpp)
target_link_libraries(client exchange_proto)
