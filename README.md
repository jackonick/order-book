# Order Book

A limit order book matching engine written from scratch in C++17. The engine itself uses only the standard library.

## Performance

The engine is measured with Google Benchmark and profiled with perf, both against a Release build. The benchmarks generate a large set of random orders and push them through a fresh book, so the numbers reflect real mixed activity rather than a single isolated call.

The headline result is cancellation. The first version of cancel searched the whole book for a matching id, which meant a single cancel could touch every resting order. Profiling a workload dominated by cancels confirmed this was the bottleneck. Replacing the search with an index from order id to its location turned cancellation into a direct lookup instead of a full scan.

Cancelling 1,000 orders on a book holding 20,000 orders:

* Before the index: about 67.5 ms
* After the index: about 5.9 ms
* Roughly 11.5 times faster

A separate change reserved space for the trade log up front so it stops reallocating as trades pile up. That alone cut order processing time by about 18 percent.

Current median times over ten runs, on a book of 20,000 orders:

* Pure insertion with no matching: about 0.67 ms
* Matching heavy: about 0.73 ms
* Cancelling 1,000 orders: about 5.9 ms

## What it is

An order book is the core structure behind an electronic exchange. It holds every resting buy and sell order for an instrument and decides which ones trade against each other. This project builds one, matching logic included.

It follows price then time priority, the rule most real exchanges use. Better priced orders match first, so the highest bid and the lowest ask sit at the top of the book. Among orders at the same price, the one that arrived first matches first.

## What it does

* Accepts orders through add_order, each carrying a side, price, size, id, timestamp, and type.
* Matches an incoming order against the best resting orders on the opposite side while their prices overlap, recording a trade for each fill.
* Handles partial fills, where an order is filled by several smaller ones, or is only partly filled itself.
* Rests whatever size is left over at its limit price, waiting for a future counter order. Order types can change this.
* Clears several price levels in one pass when an order is large enough to consume the best level and keep going.
* Cancels a resting order by id and removes the price level if it becomes empty.
* Changes a resting order's size with modify_order, or its price with modify_price. A size of zero cancels the order, and a price change moves it to the new level, where it gives up its place in line.
* Reports the best bid, best ask, and the spread with printBbo, and handles an empty side without breaking.
* Lists the busiest price levels on each side with printDepth, showing the total resting size at each.
* Writes every executed trade to trades.csv.
* Prints the full state of both sides with print.

## Order types

Each order carries a type that decides what happens when it cannot fully match. The type lives on the order and defaults to a standard limit order.

* GTC, good til cancelled. The default. Matches what it can, then rests the remainder until it fills or is cancelled.
* IOC, immediate or cancel. Matches what it can right now and drops the rest instead of resting it.
* Market. Ignores its price limit, takes the best prices available until it is filled or the book runs out, and never rests.
* FOK, fill or kill. Fills completely or does nothing. It checks the opposite side for enough liquidity first, and rejects the order if there is not enough.
* BOC, book or cancel, also called post only. Only ever rests. If the order would trade immediately it is rejected, which guarantees it adds liquidity rather than taking it.

## How it's built

The two sides of the book are ordered maps keyed by price. Bids sort from high to low and asks from low to high, so the best price on each side is always the first entry. That is what keeps the matching loop simple, since the best bid and best ask are found without searching.

Each price level holds a double ended queue of orders in arrival order. New orders join the back and fills come off the front, which preserves time priority for free.

Cancellation and single order lookups go through a hash map from order id to that order's location. This is what makes cancel a direct lookup rather than a walk across every price level. The index is kept in step with the book whenever an order rests, fills, or is cancelled.

Executed trades are appended to a vector that serves as the running trade log.

## Building and running

The project builds with CMake. The test suite uses GoogleTest and the benchmarks use Google Benchmark, both pulled in automatically through FetchContent, so there is nothing to install by hand.

Configure and build:

```
cmake -S . -B build
cmake --build build
```

Run the demo:

```
./build/orderbook
```

Run the tests:

```
ctest --test-dir build
```

Run the benchmarks:

```
./build/benchmarks
```

For meaningful benchmark numbers, build in Release so optimizations are on:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Testing

A GoogleTest suite covers the core behavior:

* an unmatched order rests
* a crossing order produces a trade
* older orders at the same price fill first
* cancel removes an order from either side by id
* modify changes size, cancels on a size of zero, and moves an order to a new price
* order types behave correctly, including market orders that never rest and fill or kill orders that either fill fully or leave the book untouched

Small accessor functions expose counts and fields so tests can check the book's state directly.

## Files

* order_book.h holds the Order struct, the Side and Type enums, the Trade struct, and the OrderBook interface.
* order_book.cpp holds the matching engine and every operation on the book.
* main.cpp runs the demo.
* tests.cpp holds the GoogleTest suite.
* benchmark.cpp holds the Google Benchmark workloads.
* CMakeLists.txt configures the build and fetches GoogleTest and Google Benchmark.

## Why this project exists

Building an order book by hand shows how orders actually match, fill, and rest on an exchange instead of leaving the book as a black box. It is also a direct way to feel the trade offs between C++ containers, since each part of the book needs a structure with the right cost for the way it is used. This is the groundwork for larger matching engine and trading system work later on.
