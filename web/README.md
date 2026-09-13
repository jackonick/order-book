# Order Book: web frontend

An exchange-style trading UI for the C++ matching engine: live order book, price and depth
charts, trade tape, an order ticket for all six order types, open orders with modify and
cancel, fills, and an API console that shows every HTTP call.

It also makes the engine itself visible:

- **Matching visualizer.** Every order comes back with a match trace, and a replay shows the
  matching loop step by step: best price first, oldest order first, partial fills, levels
  clearing, iceberg refills going to the back of the queue, then rest / drop / reject.
  Replay any past order from the Matches tab.
- **Queue view (L3).** Click a book level to see its FIFO queue order by order, with your own
  highlighted ("3rd · 420 ahead"). Open orders show their queue position too.
- **Spot account.** Cash and position with reserved amounts, pre-trade risk checks,
  average-cost realized and unrealized P&L, equity in the top bar, and % size buttons.

It talks to the engine over a REST API ([API.md](API.md)). Until that server exists, it
runs against a **mock exchange** simulated in the browser, so everything works today.

## Stack

- **SvelteKit 2 + Svelte 5** (runes), as a static single-page app (`adapter-static`)
- **TypeScript**, strict
- **Tailwind CSS 4**, with the theme tokens in `src/app.css`
- **lightweight-charts 5** (TradingView) for candlesticks. The depth chart is hand-drawn SVG.
- **zod 4** validates every REST response at the boundary
- **Lucide** icons, **Inter** and **JetBrains Mono** fonts

## Run it

```bash
npm install
npm run dev          # http://localhost:5173
```

Other scripts: `npm run check` (type-check), `npm run build` (static build into `build/`),
`npm run preview` (serve the build).

## Desktop app

The same UI runs as a native Windows app in `../desktop/`: a small C++ host that shows this
build in a WebView2 window and forwards every action to your C++ client (gRPC to the server).
See [`../desktop/README.md`](../desktop/README.md).

Inside the host the UI uses a third backend, `native` (`src/lib/api/native.ts`). Each
`ExchangeApi` call becomes `window.ob_call(method, payload)`, handled in C++. The host can also
push updates any time through `window.__obPush({ type, data })`, which the store applies
immediately (`ingestPush` in `market.svelte.ts`). Both directions use the same zod schemas as
REST. Routing is hash-based (`svelte.config.js`) because the host serves the build from a
virtual folder where only `index.html` exists.

## Mock vs. real backend

Copy `.env.example` to `.env`:

| Variable           | Default                 | Meaning |
| ------------------ | ----------------------- | ------- |
| `VITE_API_MODE`    | `mock`                  | `mock` = simulated exchange in the browser, `rest` = real server |
| `VITE_API_URL`     | `/api`                  | Prefix for every route |
| `VITE_POLL_MS`     | `500`                   | Poll interval for book / trades / orders |
| `API_PROXY_TARGET` | `http://localhost:8080` | Where the dev server forwards `/api/*` |

To switch to the real engine: set `VITE_API_MODE=rest`, run your HTTP server on port 8080
serving the routes in [API.md](API.md) under `/api`, and restart `npm run dev`. The Vite proxy
makes it same-origin, so the C++ side needs no CORS handling in dev. If the server is down,
the UI shows an offline banner and retries with backoff.

## Layout

```
src/
  lib/
    api/
      types.ts       the contract: Side, OrderType, NewOrderRequest, Trade, ... + ExchangeApi
      routes.ts      method + path for every endpoint (single source of truth)
      schemas.ts     zod validators for responses
      rest.ts        RestExchangeApi: fetch + timeout + validation + error mapping
      logging.ts     wraps any backend and records calls for the API console
      index.ts       picks mock or rest from VITE_API_MODE and exports `api`
      mock/
        engine.ts    TS stand-in for OrderBook (same rules), used only by the mock
        index.ts     MockExchangeApi + the simulated crowd of traders
        random.ts    Box–Muller normal, Poisson, exponential, log-normal
    stores/
      market.svelte.ts   live state + polling loop + order actions
      ticket.svelte.ts   order form state (the book writes prices into it)
      toasts / apiLog    notifications and the request log
    book.ts          grouping, cumulative depth, imbalance, microprice, fill estimate
    format.ts        ticks <-> price, number/time formatting
    config.ts        instrument (tick size), env, timeframes
    components/      TopBar, ChartPanel, PriceChart, DepthChart, OrderBook,
                     RecentTrades, OrderTicket, AccountPanel, Toasts
  routes/+page.svelte   the exchange layout
```

Components never import a backend directly. They read `market` and call its actions. The
store only knows the `ExchangeApi` interface. That's what lets the mock and the REST client
swap with an env var, and later lets a WebSocket feed replace polling in one place.

## The math in the UI

- **Fill estimate** (ticket): walks the opposite side level by level, like `add_order`'s
  matching loop, to preview the fill, average price, slippage and what rests. Slippage in
  bps = (avg / touch - 1) * 10^4 for a buy.
- **Imbalance** (book footer): I = (Vb - Va) / (Vb + Va) over the top 10 levels.
- **Microprice** (book): (Pb * Va + Pa * Vb) / (Va + Vb), a mid weighted by the opposite
  side's top-of-book size.
- **Mock market**: fair value follows geometric Brownian motion, F(t+dt) = F(t) * exp(sigma *
  sqrt(dt) * Z). Makers arrive as a Poisson process at distances ~ Exp(k) from fair value.
  Takers lean toward the mispricing. The synthetic chart history scales volatility by
  sqrt(interval).
