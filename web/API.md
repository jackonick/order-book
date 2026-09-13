# REST API contract

This is the HTTP interface the frontend expects from the engine's server. The frontend
already speaks it: `src/lib/api/rest.ts` sends these requests, `src/lib/api/schemas.ts`
validates the responses, and `src/lib/api/routes.ts` is the route table. The mock backend
implements the same interface, so the UI behaves identically once a real server exists.

Everything below is served under a base path, `/api` by default (`VITE_API_URL`).

**Desktop app.** The C++ desktop host (`../desktop/`) uses the same shapes without HTTP:
each route below is one `ob_call` method (`submitOrder`, `cancelOrder`, `modifyOrder`,
`getBook`, `getLevel`, `getTrades`, `getOpenOrders`, `getCandles`, `getAccount`,
`resetAccount`) handled by `include/exchange_client.hpp`. Its C++ structs map 1:1 onto the
JSON documented here.

## Conventions

| Thing  | Rule |
| ------ | ---- |
| Prices | Integer **ticks** (the engine's `uint64_t price`). With 2 decimals, `10012` = 100.12. |
| Sizes  | Integers. |
| IDs    | `uint64` sent as **JSON strings** (`"1367"`). JSON numbers are doubles and are only exact up to 2^53, so large ids silently lose their low bits. The client also accepts numbers. |
| Times  | Milliseconds since the Unix epoch. |
| Enums  | Strings: `side` is `BUY` / `SELL`; `order_type` is `GTC` / `FOK` / `MARKET` / `IOC` / `BOC` / `ICEBERG` (same members as `enum class Type`). |
| Errors | Non-2xx status with body `{ "error": "human readable message" }`. |

Status codes the UI understands: `400` bad input, `404` unknown / no longer resting order,
`409` conflict (e.g. a reprice that would make a post-only order cross), `5xx` server fault.

## Endpoints

### `GET /health`
Liveness check. Any 2xx means up. Body optional.

### `POST /orders` → `add_order`
Request (`NewOrderRequest`, mirrors `exchange.proto`):
```json
{
  "side": "BUY",
  "order_type": "GTC",
  "price": 10012,
  "size": 100,
  "reserve": 0,
  "display_size": 0
}
```
- `price` is ignored for `MARKET` (the UI sends 0).
- `ICEBERG`: `size` is the first visible slice, `reserve` the hidden remainder,
  `display_size` the slice size used on each refill. A 1,000 lot showing 100 at a time is
  `size: 100, reserve: 900, display_size: 100`.
- The **server assigns the id** (the proto's `assigned_id`); clients never send one.

Response 200 (`OrderResponse`, the proto's fields plus extras):
```json
{
  "assigned_id": "1367",
  "accepted": true,
  "filled_size": 40,
  "remaining_size": 60,
  "status": "PARTIALLY_FILLED",
  "trades": [
    { "seq": 812, "resting_id": "1301", "incoming_id": "1367", "resting_price": 10011,
      "trade_size": 40, "aggressor": "BUY", "ts": 1789152285576 }
  ]
}
```
`status` is `RESTING | PARTIALLY_FILLED | FILLED | CANCELLED | REJECTED`. For an IOC or
market order whose remainder was dropped, use `CANCELLED` (with `filled_size` > 0 if it
partly filled). A business-rule rejection (post-only would cross, FOK short of liquidity) is
**still HTTP 200** with `accepted: false` and a `reason`. Use `400` only for malformed input.

`status`, `remaining_size` and `trades` are optional. If you omit them the client derives
`status` from `accepted` / `remaining_size` / `filled_size`.

### `DELETE /orders/{id}` → `cancel_id`
`204` (or `200` with any body) on success. `404` if the id isn't resting.

### `PATCH /orders/{id}` → `modify_order` / `modify_price`
```json
{ "size": 50 }        // modify_order: keeps queue position, 0 cancels
{ "price": 10005 }    // modify_price: cancel + re-add, loses priority, may trade
```
Both may be sent together (apply size, then price). Respond with the updated `OpenOrder`
(below), or `204` / `null` if the change cancelled or fully filled the order.

### `GET /orders` → open orders
Array of the caller's resting orders:
```json
[{
  "id": "1367", "side": "BUY", "order_type": "GTC", "price": 10012,
  "size": 60, "visible_size": 60, "original_size": 100, "filled_size": 40,
  "ts": 1789152285576, "status": "PARTIALLY_FILLED"
}]
```
`size` is what's left in total (visible + iceberg reserve). `visible_size` is what's shown on
the book.

### `GET /book?depth=N` → L2 snapshot (`printDepth` as data)
```json
{
  "seq": 48213,
  "ts": 1789152285600,
  "bids": [{ "price": 10011, "size": 346, "orders": 3 }, ...],
  "asks": [{ "price": 10012, "size": 129, "orders": 1 }, ...]
}
```
Both sides best-first. `size` is the **visible** total at the level (iceberg reserve stays
hidden, as on a real exchange). `seq` is a book version that increments on every change.
`orders` is optional.

### `GET /trades?after={seq}&limit={n}` → trade tape (`Trades` vector)
```json
{ "trades": [ { "seq": 813, ... }, { "seq": 814, ... } ], "next_seq": 814 }
```
- `seq` = the trade's index in the engine's `Trades` vector. It already increases strictly.
- Without `after`: the latest `limit` trades. With `after`: trades with `seq > after`, oldest
  first, at most `limit`.
- `next_seq` = the last `seq` returned (or `after` unchanged if none). The client passes it
  back next time, so each poll only carries new trades.
- `aggressor` = side of the incoming order. The UI colors the tape with it.

### `GET /candles?interval={sec}&limit={n}` → OHLCV (optional)
```json
[{ "time": 1789152280, "open": 10010, "high": 10014, "low": 10009, "close": 10012, "volume": 873 }]
```
`time` is the bucket start in **seconds**. Oldest first. If the route is missing (404) the
chart starts empty and builds bars from live trades.

### `GET /book/level?side={BUY|SELL}&price={ticks}` → one level's queue (L3)
```json
{
  "side": "BUY",
  "price": 10011,
  "orders": [
    { "id": "1301", "size": 40,  "ts": 1789152281000, "mine": false, "iceberg": false },
    { "id": "1367", "size": 100, "ts": 1789152285576, "mine": true,  "iceberg": false }
  ]
}
```
The `std::deque<Order>` at `bids[price]` / `asks[price]`, front first. `size` is visible size.
`mine` marks the caller's orders. An empty or missing level returns `orders: []`, not 404.
Polled while a level is selected in the book.

### Queue fields on `GET /orders`
Each open order may also carry `queue_position` (1-based index in its deque) and
`size_ahead` (sum of visible size in front of it). Both are optional. The Queue column shows
"—" without them.

### Match trace on `POST /orders`
`OrderResponse.trace` (optional) records what the matching loop did, so the UI can replay it
step by step (the Matches tab, and the visualizer that opens after each order):
```json
"trace": {
  "order_id": "1367", "side": "BUY", "order_type": "GTC", "price": 10014, "size": 400,
  "ts": 1789152285576,
  "book_before": [
    { "price": 10012, "orders": [ { "id": "1301", "size": 40, "ts": 0, "mine": false, "iceberg": false } ] },
    { "price": 10013, "orders": [ ... ] }
  ],
  "events": [
    { "type": "MATCH", "resting_id": "1301", "price": 10012, "qty": 40, "resting_remaining": 0 },
    { "type": "LEVEL_CLEARED", "price": 10012 },
    { "type": "ICEBERG_REFILL", "resting_id": "1290", "price": 10013, "size": 50, "reserve": 150 },
    { "type": "REST", "price": 10014, "size": 120, "queue_position": 1 }
  ]
}
```
- `book_before`: the opposite-side levels the order can reach, copied **before** matching,
  best first. Stop once their total size covers the order, and include the first level beyond
  the limit (if there is one) for context. A handful of levels and a few dozen orders per level
  is plenty.
- `events`, in the order they happen:
  `MATCH` (one per `Trade`), `ICEBERG_REFILL` (slice refilled to the back of the queue),
  `LEVEL_CLEARED` (level erased from the map), then exactly one of `REST` / `DROP`, or a
  single `REJECT` if the order never reached the loop (post-only crossing, FOK short, risk).

### `GET /account` → balances and P&L
```json
{
  "cash": 10000000, "cash_reserved": 250000,
  "position": 1000, "position_reserved": 0,
  "avg_cost": 10003.5, "realized_pnl": 1250, "starting_equity": 20000000
}
```
A spot account: no borrowing, no shorting. Quote amounts (`cash`, `realized_pnl`,
`starting_equity`) are **price ticks × quantity**, i.e. cents here. `avg_cost` is in
fractional ticks, `null` when flat.

- Reserved: a resting buy locks `price × remaining`, a resting sell locks `remaining`.
- Settlement per fill: a buy does `avg = (avg·pos + price·qty) / (pos + qty)`, `pos += qty`,
  `cash -= price·qty`. A sell does `realized += (price − avg)·qty`, `pos -= qty`,
  `cash += price·qty`.
- The UI marks to the last trade: equity = cash + position × mark, and unrealized =
  (mark − avg_cost) × position.

If the route is missing, the rest of the app keeps working; the Account tab says it's unavailable.

### `POST /account/reset` (simulator convenience)
Cancel the caller's resting orders and restore starting balances. Returns the new `Account`.

### Risk checks
Checked **before** `add_order`, on free balance (total − reserved):
- Buy limit / IOC / FOK / iceberg: needs `price × total size` of free cash (the worst case,
  since fills can only be cheaper).
- Market buy: needs the cost of sweeping the book for its size, **including hidden iceberg
  reserve**, which the sweep would also consume. Alternatively, cap it with a price collar and
  reserve `collar × size`.
- Any sell: needs `total size` of free position.

A failure is a business rejection: HTTP 200, `accepted: false`, a `reason` such as
`"Insufficient USD: order needs $40,048.00, $39,000.00 available"`, and a trace holding a
single `REJECT` event. A `PATCH` that would need more than is free returns **409** with
`{ "error": "..." }`.

## How the frontend uses it

- Polls `GET /book`, `GET /trades?after=…`, `GET /orders` every `VITE_POLL_MS` (500 ms), with
  exponential backoff while the server is unreachable.
- Reloads `GET /candles` when the timeframe changes, and hourly candles every 30 s for the
  24h stats.
- Fires `POST` / `PATCH` / `DELETE` from the ticket and the open-orders table, then polls
  immediately.
- Works out "your" fills by matching `resting_id` / `incoming_id` on the tape against the ids
  it got back from `POST /orders` and `GET /orders`.

When a push feed exists (WebSocket), the store's `poll()` can be swapped for message
handlers without touching any component.

## What the engine needs before it can serve this

A checklist of gaps between `order_book.h` today and this contract. They're listed, not
implemented, because the engine is yours to write.

1. **Return what `add_order` did.** It returns `void` today. The server needs the trades
   it produced (e.g. `Trades.size()` before/after, or return a result struct), whether
   the order rested, and how much is left.
2. **Surface rejections.** BOC and FOK rejections go to `std::cerr` and `return`. The server
   needs `accepted = false` plus a reason.
3. **Server-assigned ids.** `Order.id` is set by the caller. The server should own an id
   counter (matching the proto's `assigned_id`).
4. **Aggressor side + timestamp on `Trade`.** `Trade` has neither. Add them, or have the
   server stamp them (it knows the incoming order's side).
5. **Depth as data.** `printDepth` writes to stdout. Add something like
   `std::vector<Level> depth(Side, int n) const`.
6. **Open-order lookup.** `idIndex` + `id_searcher` already get you most of the way. You also need
   `original_size` / `filled_size`, which means tracking fills per resting id.
7. **Concurrency.** HTTP servers handle requests on several threads, but the engine isn't
   thread-safe. Either guard it with one mutex, or (the roadmap's Stage 5 design) push
   commands through a queue into a single engine thread.
8. **A match trace.** Inside `add_order`'s while loop, push one event wherever the code already
   makes a decision: next to `Trades.push_back` (MATCH), in the iceberg refill branch
   (ICEBERG_REFILL), where an emptied level is erased (LEVEL_CLEARED), and at the end (REST
   or DROP). Snapshot `book_before` by walking the opposite map before the loop. Return the
   events from `add_order` along with the trades.
9. **Queue access.** Expose a level's deque read-only (for `GET /book/level`) and an order's
   index within it (for `queue_position` / `size_ahead`). `idIndex` already gives you the level.
10. **Accounts live outside the engine.** Keep `OrderBook` owner-agnostic. The server keeps
    `order id → account`, runs the risk check before `add_order`, and settles each returned
    trade against the two accounts involved.
11. **CORS.** Not needed in dev (the Vite proxy makes it same-origin). Only needed if you serve
   the built frontend from a different origin than the API.

Implementation options for the HTTP layer: a single-header C++ library such as
cpp-httplib (simplest), Crow or Drogon, or the roadmap's Stage 7 FastAPI service in front of the
engine (it could talk to the engine over your existing gRPC `Exchange` service).
