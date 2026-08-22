#include <benchmark/benchmark.h>
#include "order_book.h"
#include <random>
#include <iostream>

static void BM_insertion(benchmark::State& state) {

    std::mt19937 gen(42);
    std::uniform_int_distribution<uint64_t> price_dist(95, 105);
    std::uniform_int_distribution<uint64_t> size_dist(1, 1000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    std::uniform_int_distribution<uint64_t> price_dist_buys(30, 80);
    std::uniform_int_distribution<uint64_t> price_dist_sells(95, 105);

    std::vector<Order> orderVec;
    orderVec.reserve(10000);

    for (uint64_t i = 0; i < 10000; ++i){
        Order o;
        o.id = i * 2;
        o.timestamp = i;
        o.price = price_dist_buys(gen);
        o.size = size_dist(gen);
        o.side = Side::BUY;
        orderVec.push_back(o);

        Order o1;
        o1.id = i * 2 + 1;
        o1.timestamp = i;
        o1.price = price_dist_sells(gen);
        o1.size = size_dist(gen);
        o1.side = Side::SELL;
        orderVec.push_back(o1);
    }

    for (auto _ : state) {
        OrderBook book;
        for (auto& o : orderVec){
            book.add_order(o);
        }
        benchmark::DoNotOptimize(book);
    }
}
BENCHMARK(BM_insertion);


static void BM_match_heavy(benchmark::State& state) {

    std::mt19937 gen(42);
    std::uniform_int_distribution<uint64_t> price_dist(95, 105);
    std::uniform_int_distribution<uint64_t> size_dist(1, 1000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    std::uniform_int_distribution<uint64_t> price_dist_buys(95, 105);
    std::uniform_int_distribution<uint64_t> price_dist_sells(95, 99);

    std::vector<Order> orderVec;
    orderVec.reserve(10000);

    for (uint64_t i = 0; i < 10000; ++i){
        Order o;
        o.id = i * 2;
        o.timestamp = i;
        o.price = price_dist_buys(gen);
        o.size = size_dist(gen);
        o.side = Side::BUY;
        orderVec.push_back(o);

        Order o1;
        o1.id = i * 2 + 1;
        o1.timestamp = i;
        o1.price = price_dist_sells(gen);
        o1.size = size_dist(gen);
        o1.side = Side::SELL;
        orderVec.push_back(o1);
    }

    for (auto _ : state) {
        OrderBook book;
        for (auto& o : orderVec){
            book.add_order(o);
        }
        benchmark::DoNotOptimize(book);
    }
}
BENCHMARK(BM_match_heavy);


static void BM_cancels(benchmark::State& state) {

    std::mt19937 gen(42);
    std::uniform_int_distribution<uint64_t> price_dist(95, 105);
    std::uniform_int_distribution<uint64_t> size_dist(1, 1000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    std::uniform_int_distribution<uint64_t> price_dist_buys(40, 80);
    std::uniform_int_distribution<uint64_t> price_dist_sells(95, 80);

    std::vector<Order> orderVec;
    orderVec.reserve(10000);

    for (uint64_t i = 0; i < 10000; ++i){
        Order o;
        o.id = i * 2;
        o.timestamp = i;
        o.price = price_dist_buys(gen);
        o.size = size_dist(gen);
        o.side = Side::BUY;
        orderVec.push_back(o);

        Order o1;
        o1.id = i * 2 + 1;
        o1.timestamp = i;
        o1.price = price_dist_sells(gen);
        o1.size = size_dist(gen);
        o1.side = Side::SELL;
        orderVec.push_back(o1);
    }

    for (auto _ : state) {
        OrderBook book;
        for (auto& o : orderVec){
            book.add_order(o);
        }

        for (int i = 0; i < 1000; ++i){
            book.cancel_id(i);
        }
        benchmark::DoNotOptimize(book);
    }
}
BENCHMARK(BM_cancels);





BENCHMARK_MAIN();