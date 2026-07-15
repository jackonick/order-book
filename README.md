# Order Book

A limit order book matching engine written from scratch in C++20, using only the standard library.

## What it is

An **order book** is the core data structure behind every electronic exchange: it holds all the
resting buy and sell orders for an instrument and decides which ones trade against each other.
This project implements one — including the matching logic — with no external dependencies.

It models a **price–time priority** matching engine, the same rule most real exchanges use:

1. **Price priority** — better-priced orders match first (the highest bid and the lowest ask sit at the top of the book).
2. **Time priority** — at the same price, the order that arrived first matches first (FIFO).

## What it does

- Accepts incoming limit orders via `add_order`, each tagged `BUY` or `SELL` with a price, size, id, and timestamp.
- **Matches aggressively** — an incoming order immediately crosses against the best resting orders on the
  opposite side while the prices overlap, printing a `TRADE` line for each fill.
- Handles **partial fills** — an order can be filled by several smaller resting orders, or only partly filled itself.
- **Rests the remainder** — whatever size isn't filled is placed on the book at its limit price, waiting for a future counter-order.
- **Walks multiple price levels** — a large marketable order consumes the best level, then the next, until it's filled or runs out of crossing prices.
- `print()` dumps the current state of both sides of the book.

`main()` runs a short scripted scenario that exercises each of these cases (resting an order, partial fill,
resting the leftover, and walking two ask levels) so you can watch the book evolve in the console output.

## How it's built

| Concern | Choice | Why |
|---|---|---|
| Bids | `std::map<uint64_t, std::deque<Order>, std::greater<uint64_t>>` | Ordered by price **descending** → the best (highest) bid is always `begin()` |
| Asks | `std::map<uint64_t, std::deque<Order>>` | Ordered by price **ascending** → the best (lowest) ask is always `begin()` |
| Orders at one price | `std::deque<Order>` | FIFO queue → preserves time priority, with O(1) push-back / pop-front |

Keying an ordered `map` by price gives **O(log n)** access to the best price level, and the `deque` at each
level supplies the time-priority queue for free. The best bid and best ask are therefore always at `begin()`
of their respective maps, which is what makes the matching loop simple and fast.

## Build & run

This is a Visual Studio C++ project (MSBuild, C++20, platform toolset v145). There are no external dependencies.

**Visual Studio:** open `Order Book V1.slnx`, choose a configuration (Debug/Release, x64), then Build and Run.

**Any C++20 compiler:**

```sh
g++ -std=c++20 order_book.cpp -o order_book
./order_book
```

## Files

- `order_book.h` — the `Order` struct, `Side` enum, and the `OrderBook` class interface.
- `order_book.cpp` — the matching engine implementation and the demo `main()`.

## Why this project exists

Building an order book by hand is a foundational exercise in **market microstructure** — it makes concrete how
orders actually match, fill, and rest on an exchange rather than treating the book as a black box. It's also a
practical way to reason about **C++ container trade-offs**: choosing the structure that gives the right
complexity (O(1) vs O(log n)) for each access pattern. This is the "V1" groundwork for more complete
matching-engine and trading-system work down the line.
