#include <benchmark/benchmark.h>
#include "order_book.h"
#include <random>
#include <iostream>

static void BM_Something(benchmark::State& state) {

    std::mt19937 gen(42);
    std::uniform_int_distribution<uint64_t> price_dist(95, 105);
    std::uniform_int_distribution<uint64_t> size_dist(1, 1000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    std::vector<Order> orderVec;
    orderVec.reserve(10000);

    for (uint64_t i = 0; i < 10000; ++i){
        Order o;
        o.id = i;
        o.timestamp = i;
        o.price = price_dist(gen);
        o.size = size_dist(gen);
        o.side = (side_dist(gen) == 0) ? Side::BUY : Side::SELL;
        orderVec.push_back(o);
    }

    for (auto _ : state) {
        OrderBook book;
        for (auto& o : orderVec){
            book.add_order(o);
        }
        benchmark::DoNotOptimize(book);
    }
}
BENCHMARK(BM_Something);

BENCHMARK_MAIN();