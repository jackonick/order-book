# OrderBook desktop app (C++)

A native Windows app that shows the exchange UI (the build of `web/`) and sends every action
to **your C++ client**, which talks gRPC to the server in NYC.

```
UI (web/build, inside a WebView2 window)
   │  window.ob_call("submitOrder", …)            ▲  window.__obPush({ type, data })
   ▼                                              │
host/  Bridge: decodes JSON into C++ structs, runs your method on a worker thread,
   │          sends results back; is your Publisher for pushes
   ▼                                              │
client/my_client.cpp  YOUR ExchangeClient  ──── gRPC ────►  VPS in NYC
```

You never touch JavaScript or JSON. The contract is plain C++ in
[`include/exchange_client.hpp`](include/exchange_client.hpp).

## Build and run

```bash
cd ../web && npm install && npm run build      # the UI (only needed when the UI changes)
cd ../desktop
cmake -S . -B build
cmake --build build --config Debug
build\Debug\orderbook-app.exe
```

The build copies `web/build` next to the exe as `ui/`. Or open the `desktop/` folder in
Visual Studio (it reads the CMake project directly) and press F5.

| Env var            | Effect |
| ------------------ | ------ |
| `OB_DEMO_CLIENT=1` | Use `host/demo_client.cpp`, a fake-market client, instead of yours |
| `OB_UI_URL=<url>`  | Load the UI from a dev server, e.g. `http://localhost:5173` after `npm run dev` in `web/` |

CMake option `-DOB_HIDE_CONSOLE=ON` drops the console window. Leave it off while developing:
`std::cout` from your client shows up there. Right-click › Inspect in the window opens the
WebView2 DevTools.

## What's yours

| Path | Owner |
| ---- | ----- |
| `client/my_client.cpp` | **You.** Implement `MyClient` and return it from `makeClient()`. |
| `include/exchange_client.hpp` | The contract. Change it together with the host if you need new fields. |
| `host/` | The window and bridge. You shouldn't need to touch it. |

`makeClient()` returns `nullptr` today, so the UI runs its built-in simulator (MOCK DATA in
the top bar). Return `std::make_unique<MyClient>()` and the top bar switches to **C++ CLIENT**:
every order, cancel and modify now calls your class.

## The interface in one page

```cpp
class ExchangeClient {
    void start(Publisher&);      // background thread, once: connect, start streams
    void stop();                 // window closing: stop streams, join threads

    OrderResponse submitOrder(const NewOrderRequest&);
    void cancelOrder(uint64_t id);
    std::optional<OpenOrder> modifyOrder(uint64_t id, const ModifyRequest&);

    BookSnapshot getBook(int depth);                 // polled every 500 ms:
    TradesPage getTrades(optional<uint64_t> after, int limit);   // return cached state
    std::vector<OpenOrder> getOpenOrders();
    Account getAccount();
    // + getLevel, getCandles, resetAccount (optional)
};

class Publisher {                // call from any thread, any time
    void book(const BookSnapshot&);
    void trades(const std::vector<Trade>&);
    void openOrders(const std::vector<OpenOrder>&);
    void account(const Account&);
    void status(bool connected, const std::string& message = {});
};
```

Everything has a default, so you can build it up one method at a time. Unimplemented market
data shows empty; unimplemented actions show "…isn't implemented in your client yet" in the UI.
`host/demo_client.cpp` implements the whole thing against a fake market, as a reference.

**Rules of the road**

- **Threads.** Each call runs on one of 4 worker threads and calls can overlap, so guard shared
  state with a mutex. gRPC stubs are thread-safe. Blocking is fine; the window never freezes.
- **Poll vs push.** The getters are polled, so return a cached copy your stream thread keeps
  current. For instant updates, call the `Publisher` from that stream thread the moment
  something changes; the UI applies pushes immediately.
- **Errors.** `throw ob::ClientError(status, "message")`. Status is HTTP-style: 400 bad input,
  404 unknown order, 409 conflict, 501 not implemented, 503 not connected.
- **Connection state.** `publish.status(false, "why")` shows the Offline banner; `status(true)`
  clears it.
- **Units.** Same as `order_book.h`: prices in ticks, integer sizes, `uint64` ids, ms timestamps.
- **Match trace.** `OrderResponse::trace` is optional. Fill it with `MatchEvent`s from the
  server and the replay window animates your engine's matching loop (see `web/API.md`).

## Adding gRPC

gRPC isn't a dependency yet; add it when you start on the client. With vcpkg:

```bash
vcpkg install grpc:x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
```

Then in `CMakeLists.txt`: `find_package(gRPC CONFIG REQUIRED)`, link `gRPC::grpc++`, and add the
`exchange.pb.cc` / `exchange.grpc.pb.cc` that `protoc` generates from `exchange.proto`.

Latency: your ping to the VPS is ~47 ms, so expect ~48 ms for an order round trip and ~24 ms
for streamed updates. Create the channel once in `start()` and keep it open; a new channel
costs an extra ~100 ms of connection setup.
