# Order Book

A limit order book matching engine written from scratch in C++17, using only the standard library.

## What it is

An order book is the core data structure behind every electronic exchange: it holds all the resting buy and sell orders for an instrument and decides which ones trade against each other. This project implements one — including the matching logic — with no external dependencies.

It models a **price–time priority** matching engine, the same rule most real exchanges use:

- **Price priority** — better-priced orders match first (the highest bid and the lowest ask sit at the top of the book).
- **Time priority** — at the same price, the order that arrived first matches first (FIFO).

## What it does

- Accepts incoming orders via `add_order`, each tagged BUY or SELL with a price, size, id, timestamp, and type.
- **Matches aggressively** — an incoming order immediately crosses against the best resting orders on the opposite side while the prices overlap, recording a trade for each fill.
- **Handles partial fills** — an order can be filled by several smaller resting orders, or only partly filled itself.
- **Rests the remainder** — whatever size isn't filled is placed on the book at its limit price, waiting for a future counter-order (order types can change this — see below).
- **Walks multiple price levels** — a large marketable order consumes the best level, then the next, until it's filled or runs out of crossing prices.
- **Cancels by id** — `cancel_id` removes a resting order from either side of the book by its id, cleaning up the price level if it becomes empty.
- **Modifies by id** — `modify_order` changes a resting order's size (modifying to size 0 cancels it), and `modify_price` changes an order's price by cancelling and re-adding it at the new level, so it correctly loses time priority.
- **Reports the BBO** — `printBbo` shows the best bid, best ask, and the spread between them, handling the case where a side is empty.
- **Shows market depth** — `printDepth` lists the top N price levels on each side with the total resting size at each.
- **Logs trades** — executed trades are written to `trades.csv` via `printTrade`.
- `print()` dumps the current state of both sides of the book.

`main()` runs a short scripted scenario that exercises the core cases (resting an order, partial fill, resting the leftover, and walking two ask levels) so you can watch the book evolve in the console output.

## Order types

Every order carries a **type** that controls how it behaves when it can't fully match. The type lives on the order itself and defaults to GTC.

- **GTC (Good-Til-Cancelled)** — the default. A standard limit order: matches whatever crosses, then rests the remainder on the book until it's filled or cancelled.
- **IOC (Immediate-Or-Cancel)** — matches whatever it can right now, then cancels the remainder instead of resting it.
- **Market** — ignores its limit price and matches against the best available prices until it's filled or the book runs dry; it never rests a remainder.
- **FOK (Fill-Or-Kill)** — fills the entire order immediately or does nothing at all. Before trading, it scans the opposite side to confirm there's enough liquidity to fill completely; if there isn't, the order is rejected and nothing changes.

## How it's built

| Concern | Choice | Why |
| --- | --- | --- |
| Bids | `std::map<uint64_t, std::deque<Order>, std::greater<uint64_t>>` | Ordered by price descending → the best (highest) bid is always `begin()` |
| Asks | `std::map<uint64_t, std::deque<Order>>` | Ordered by price ascending → the best (lowest) ask is always `begin()` |
| Orders at one price | `std::deque<Order>` | FIFO queue → preserves time priority, with O(1) push-back / pop-front |
| Trade log | `std::vector<Trade>` | Append-only record of every fill, exported to CSV |

Keying an ordered map by price gives O(log n) access to the best price level, and the deque at each level supplies the time-priority queue for free. The best bid and best ask are therefore always at `begin()` of their respective maps, which is what makes the matching loop simple and fast.

Order management (cancel and modify) locates an order through a single shared `id_searcher` helper that walks the price levels and scans each level's deque for a matching id, returning a pointer to the order (or `nullptr` if it isn't there). Centralising the lookup keeps the individual operations short and avoids repeating the same search in every method — O(n) for now, with a fast `id → location` index noted as a later optimization.

## Build & run

The project builds with **CMake**. Its test suite uses **GoogleTest**, which CMake pulls in automatically via `FetchContent` — there's nothing to install by hand. The matching engine itself depends only on the C++17 standard library.

Configure and build:

```
cmake -S . -B build
cmake --build build
```

Run the demo scenario:

```
./build/orderbook              # Linux / macOS
.\build\Debug\orderbook.exe    # Windows
```

Run the tests:

```
ctest --test-dir build
```

To compile just the engine with a plain compiler, no CMake:

```
g++ -std=c++17 main.cpp order_book.cpp -o order_book
./order_book
```

## Testing

The engine is covered by a GoogleTest suite (`tests.cpp`) that pins down its core behaviors:

- an unmatched order rests on the book
- a crossing order produces a trade
- **price–time priority** — at one price, the older order fills first
- **cancel** — a resting order is removed from either side by id
- **modify** — a resize changes an order's size, a modify-to-zero cancels it, and a price change re-rests the order
- **order types** — a market order consumes liquidity without resting; a fill-or-kill order fills completely or leaves the book untouched

Observability getters (`bid_levels`, `ask_levels`, `trade_count`, `size_getter`, `price_getter`) exist so tests can assert on the book's state directly.

## Files

- `order_book.h` — the `Order` struct, `Side` and `Type` enums, `Trade` struct, and the `OrderBook` class interface.
- `order_book.cpp` — the matching engine and order-management implementation (`add_order`, `cancel_id`, `modify_order`, `modify_price`, `printBbo`, `printDepth`, `print`, and the getters).
- `main.cpp` — the entry point; runs the demo scenario.
- `tests.cpp` — the GoogleTest suite.
- `CMakeLists.txt` — build configuration, including the GoogleTest fetch.

## Why this project exists

Building an order book by hand is a foundational exercise in market microstructure — it makes concrete how orders actually match, fill, and rest on an exchange rather than treating the book as a black box. It's also a practical way to reason about C++ container trade-offs: choosing the structure that gives the right complexity (O(1) vs O(log n)) for each access pattern. This is the "V1" groundwork — now with order management, book queries, and multiple order types — for more complete matching-engine and trading-system work down the line.
