/*
 * All live market + account state, and the polling loop that keeps it fresh.
 *
 * REST has no push, so the store polls book / trades / open orders / account every
 * POLL_MS (plus the L3 queue of whichever level is selected). Trades are fetched
 * incrementally with a cursor (`after = last seq seen`), so each poll only carries what's
 * new. When a WebSocket feed exists later, replace `poll()` with message handlers that
 * call the same `ingest*` methods; components won't change.
 */
import { api, onPush, type PushMessage } from '$lib/api';
import { errorMessage } from '$lib/api/errors';
import {
	ORDER_TYPE_META,
	opposite,
	type Account,
	type BookSnapshot,
	type Candle,
	type Id,
	type LevelQueue,
	type MatchTrace,
	type ModifyRequest,
	type NewOrderRequest,
	type OpenOrder,
	type OrderResponse,
	type Side,
	type Trade,
	type TradesPage
} from '$lib/api/types';
import { BOOK_DEPTH, CANDLE_LIMIT, POLL_MS } from '$lib/config';
import { fmtPrice, fmtSize } from '$lib/format';
import { toasts } from './toasts.svelte';

export interface Fill {
	seq: number;
	ts: number;
	order_id: Id;
	side: Side;
	price: number;
	size: number;
	liquidity: 'MAKER' | 'TAKER';
}

export interface AccountView extends Account {
	/** Price the position is valued at: last trade, else mid. */
	mark: number;
	cashFree: number;
	posFree: number;
	posValue: number;
	equity: number;
	unrealized: number;
	totalPnl: number;
	returnPct: number;
}

export type ConnStatus = 'connecting' | 'online' | 'offline';

const EMPTY_BOOK: BookSnapshot = { seq: 0, ts: 0, bids: [], asks: [] };
const TRADE_BUFFER = 200;
const MAX_BACKOFF_MS = 5000;
const AUTO_VIZ_KEY = 'ob.autoVisualize';

function loadFlag(key: string, fallback: boolean) {
	try {
		const v = localStorage.getItem(key);
		return v === null ? fallback : v === '1';
	} catch {
		return fallback;
	}
}

class MarketStore {
	book = $state.raw<BookSnapshot>(EMPTY_BOOK);
	/** Newest first. */
	trades = $state.raw<Trade[]>([]);
	openOrders = $state.raw<OpenOrder[]>([]);
	/** Newest first. */
	fills = $state.raw<Fill[]>([]);
	candles = $state.raw<Candle[]>([]);
	/** Bumped when `candles` is replaced wholesale (not just the last bar updated). */
	candleEpoch = $state(0);
	interval = $state(5);
	/** Hourly bars for the last 24h, for the top-bar stats. */
	day = $state.raw<Candle[]>([]);

	account = $state.raw<Account | null>(null);
	accountError = $state<string | null>(null);

	/** Level whose L3 queue is being inspected. */
	selected = $state.raw<{ side: Side; price: number } | null>(null);
	level = $state.raw<LevelQueue | null>(null);

	/** Match traces of your orders, newest first. */
	traces = $state.raw<MatchTrace[]>([]);
	/** Trace currently open in the visualizer. */
	replay = $state.raw<MatchTrace | null>(null);
	autoVisualize = $state(loadFlag(AUTO_VIZ_KEY, true));

	status = $state<ConnStatus>('connecting');
	latency = $state<number | null>(null);
	error = $state<string | null>(null);

	bestBid = $derived(this.book.bids[0]?.price ?? null);
	bestAsk = $derived(this.book.asks[0]?.price ?? null);
	mid = $derived(
		this.bestBid !== null && this.bestAsk !== null ? (this.bestBid + this.bestAsk) / 2 : null
	);
	spread = $derived(
		this.bestBid !== null && this.bestAsk !== null ? this.bestAsk - this.bestBid : null
	);
	lastPrice = $derived(this.trades[0]?.resting_price ?? this.mid);

	/** Direction of the last price change, for coloring the ticker. */
	tick = $derived.by((): 'up' | 'down' | 'flat' => {
		const last = this.trades[0]?.resting_price;
		if (last === undefined) return 'flat';
		const prev = this.trades.find((t) => t.resting_price !== last)?.resting_price;
		return prev === undefined ? 'flat' : last > prev ? 'up' : 'down';
	});

	stats = $derived.by(() => {
		const last = this.lastPrice;
		if (!this.day.length || last == null) return null;
		const open = this.day[0].open;
		let high = last;
		let low = last;
		let volume = 0;
		for (const c of this.day) {
			high = Math.max(high, c.high);
			low = Math.min(low, c.low);
			volume += c.volume;
		}
		return { open, high, low, volume, change: (last - open) / open };
	});

	/**
	 * Account marked to market. Equity = cash + position x mark.
	 * Unrealized = (mark - avg cost) x position; total P&L = equity - starting equity,
	 * which equals realized + unrealized when the starting position is booked at its mark.
	 */
	acct = $derived.by((): AccountView | null => {
		const a = this.account;
		if (!a) return null;
		const mark = this.lastPrice ?? this.mid ?? a.avg_cost ?? 0;
		const posValue = a.position * mark;
		const equity = a.cash + posValue;
		const totalPnl = equity - a.starting_equity;
		return {
			...a,
			mark,
			cashFree: a.cash - a.cash_reserved,
			posFree: a.position - a.position_reserved,
			posValue,
			equity,
			unrealized: a.avg_cost === null ? 0 : (mark - a.avg_cost) * a.position,
			totalPnl,
			returnPct: a.starting_equity ? totalPnl / a.starting_equity : 0
		};
	});

	private running = false;
	private gen = 0;
	private pollTimer: ReturnType<typeof setTimeout> | undefined;
	private dayTimer: ReturnType<typeof setInterval> | undefined;
	private inFlight: Promise<void> | null = null;
	private failures = 0;
	/** Message from the C++ client's last status(false, ...) push; null while it's connected. */
	private clientDown: string | null = null;
	private lastSeq: number | undefined;
	private candleReq = 0;
	private candlesAsOf = 0;
	private readonly myIds = new Set<Id>();
	private readonly fillSeqs = new Set<number>();

	// ------------------------------------------------------------ lifecycle

	start() {
		if (this.running) return;
		this.running = true;
		onPush((msg) => this.ingestPush(msg));
		void this.loadCandles();
		void this.loadDay();
		this.dayTimer = setInterval(() => void this.loadDay(), 30_000);
		this.kick();
	}

	stop() {
		this.running = false;
		this.gen++;
		clearTimeout(this.pollTimer);
		clearInterval(this.dayTimer);
	}

	/** Poll now instead of waiting for the next tick (e.g. right after placing an order). */
	kick() {
		clearTimeout(this.pollTimer);
		void this.loop(++this.gen);
	}

	private async loop(gen: number) {
		await this.poll();
		if (!this.running || gen !== this.gen) return;
		const delay = this.failures ? Math.min(POLL_MS * 2 ** this.failures, MAX_BACKOFF_MS) : POLL_MS;
		this.pollTimer = setTimeout(() => void this.loop(gen), delay);
	}

	private poll() {
		this.inFlight ??= this.doPoll().finally(() => (this.inFlight = null));
		return this.inFlight;
	}

	private async doPoll() {
		const t0 = performance.now();
		const sel = this.selected;
		try {
			// Account and L3 are optional extras: if the server lacks them, the core feed still works.
			const [book, page, orders, account, level] = await Promise.all([
				api.getBook(BOOK_DEPTH),
				api.getTrades(this.lastSeq, TRADE_BUFFER),
				api.getOpenOrders(),
				api.getAccount().catch((e) => {
					this.accountError = errorMessage(e);
					return null;
				}),
				sel ? api.getLevel(sel.side, sel.price).catch(() => null) : Promise.resolve(null)
			]);
			this.latency = Math.round(performance.now() - t0);
			this.book = book;
			this.ingestOrders(orders);
			this.ingestTrades(page);
			if (account) {
				this.account = account;
				this.accountError = null;
			}
			if (sel && this.selected === sel) this.level = level;
			if (this.status !== 'online' && this.candles.length === 0) void this.loadCandles();
			// A client that pushed "disconnected" stays offline even though its getters answer.
			this.status = this.clientDown === null ? 'online' : 'offline';
			this.failures = 0;
			this.error = this.clientDown;
		} catch (e) {
			this.failures++;
			this.error = errorMessage(e);
			if (this.failures >= 2 || this.status === 'connecting') this.status = 'offline';
		}
	}

	// ------------------------------------------------------------ ingest

	/**
	 * Updates pushed by the C++ client (e.g. from its gRPC streams). They apply immediately;
	 * polling keeps running underneath as a consistency check, and the trade cursor dedups
	 * anything that arrives both ways.
	 */
	ingestPush(msg: PushMessage) {
		switch (msg.type) {
			case 'book':
				this.book = msg.data;
				break;
			case 'trades':
				if (msg.data.length) {
					this.ingestTrades({ trades: msg.data, next_seq: msg.data[msg.data.length - 1].seq });
				}
				break;
			case 'orders':
				this.ingestOrders(msg.data);
				break;
			case 'account':
				this.account = msg.data;
				this.accountError = null;
				break;
			case 'status':
				this.clientDown = msg.data.connected ? null : (msg.data.message ?? 'Client disconnected');
				this.status = this.clientDown === null ? 'online' : 'offline';
				this.error = this.clientDown;
				break;
		}
	}

	private ingestOrders(orders: OpenOrder[]) {
		for (const o of orders) this.myIds.add(o.id);
		this.openOrders = orders;
	}

	private ingestTrades(page: TradesPage) {
		const first = this.lastSeq === undefined;
		const prev = this.lastSeq ?? -1;
		// A server that ignores `after` and re-sends trades we already have mustn't duplicate
		// the tape (or break keyed lists), and a cursor must never move backwards.
		const fresh = first ? page.trades : page.trades.filter((t) => t.seq > prev);
		this.lastSeq = first ? page.next_seq : Math.max(prev, page.next_seq);
		if (!fresh.length) return;

		this.trades = [...fresh].reverse().concat(this.trades).slice(0, TRADE_BUFFER);
		this.applyToCandles(fresh);
		if (!first) this.collectFills(fresh, true);
	}

	private applyToCandles(trades: Trade[]) {
		const sec = this.interval;
		const bars = this.candles.slice();
		// With history from /candles, skip trades that response already counted. Without it
		// (a server that has no /candles route yet), every trade builds the chart.
		const cutoff = bars.length ? this.candlesAsOf : -Infinity;
		let changed = false;
		for (const t of trades) {
			if (t.ts <= cutoff) continue;
			const time = Math.floor(t.ts / 1000 / sec) * sec;
			const last = bars[bars.length - 1];
			const p = t.resting_price;
			if (!last) {
				bars.push({ time, open: p, high: p, low: p, close: p, volume: t.trade_size });
			} else if (time === last.time) {
				bars[bars.length - 1] = {
					...last,
					high: Math.max(last.high, p),
					low: Math.min(last.low, p),
					close: p,
					volume: last.volume + t.trade_size
				};
			} else if (time > last.time) {
				bars.push({ time, open: p, high: p, low: p, close: p, volume: t.trade_size });
			} else continue;
			changed = true;
		}
		if (changed) this.candles = bars.slice(-CANDLE_LIMIT * 2);
	}

	/** Pick our own fills out of the public tape, by matching order ids. */
	private collectFills(trades: Trade[], notify: boolean) {
		const found: Fill[] = [];
		for (const t of trades) {
			if (this.fillSeqs.has(t.seq)) continue;
			let fill: Fill | null = null;
			if (this.myIds.has(t.incoming_id)) {
				fill = { seq: t.seq, ts: t.ts, order_id: t.incoming_id, side: t.aggressor, price: t.resting_price, size: t.trade_size, liquidity: 'TAKER' };
			} else if (this.myIds.has(t.resting_id)) {
				fill = { seq: t.seq, ts: t.ts, order_id: t.resting_id, side: opposite(t.aggressor), price: t.resting_price, size: t.trade_size, liquidity: 'MAKER' };
			}
			if (!fill) continue;
			this.fillSeqs.add(t.seq);
			found.push(fill);
		}
		if (!found.length) return;
		found.sort((a, b) => b.seq - a.seq);
		this.fills = [...found, ...this.fills].slice(0, 500);

		const passive = found.filter((f) => f.liquidity === 'MAKER');
		if (notify && passive.length) {
			const qty = passive.reduce((s, f) => s + f.size, 0);
			const f = passive[0];
			toasts.push({
				kind: 'info',
				title: `${f.side === 'BUY' ? 'Bought' : 'Sold'} ${fmtSize(qty)} as maker`,
				body: `Order #${f.order_id} filled @ ${fmtPrice(f.price)}`
			});
		}
	}

	// ------------------------------------------------------------ loaders

	async setInterval(sec: number) {
		if (sec === this.interval) return;
		this.interval = sec;
		await this.loadCandles();
	}

	private async loadCandles() {
		const req = ++this.candleReq;
		const asOf = Date.now();
		try {
			const bars = await api.getCandles(this.interval, CANDLE_LIMIT);
			if (req !== this.candleReq) return;
			this.candles = bars;
			this.candlesAsOf = asOf;
			this.candleEpoch++;
			// No history from the server: rebuild bars from the trades already buffered (oldest first).
			if (!bars.length) this.applyToCandles([...this.trades].reverse());
		} catch (e) {
			if (req === this.candleReq) this.error = errorMessage(e);
		}
	}

	private async loadDay() {
		try {
			this.day = await api.getCandles(3600, 24);
		} catch {
			/* stats are cosmetic; the poll loop reports connectivity */
		}
	}

	// ------------------------------------------------------------ L3 + visualizer

	selectLevel(side: Side, price: number) {
		if (this.selected?.side === side && this.selected.price === price) return;
		this.selected = { side, price };
		this.level = null;
		this.kick();
	}

	clearSelection() {
		this.selected = null;
		this.level = null;
	}

	openReplay(trace: MatchTrace) {
		this.replay = trace;
	}

	closeReplay() {
		this.replay = null;
	}

	setAutoVisualize(on: boolean) {
		this.autoVisualize = on;
		try {
			localStorage.setItem(AUTO_VIZ_KEY, on ? '1' : '0');
		} catch {
			/* private mode etc.; the setting just won't persist */
		}
	}

	// ------------------------------------------------------------ actions

	async submit(req: NewOrderRequest): Promise<OrderResponse | null> {
		const label = `${req.side === 'BUY' ? 'Buy' : 'Sell'} ${ORDER_TYPE_META[req.order_type].label}`;
		try {
			const res = await api.submitOrder(req);
			if (res.trace) {
				this.traces = [res.trace, ...this.traces].slice(0, 50);
				if (this.autoVisualize) this.replay = res.trace;
			}
			if (res.accepted) {
				this.myIds.add(res.assigned_id);
				this.collectFills(res.trades, false);
				// A passive fill can land in a poll that raced this response; rescan the buffer.
				this.collectFills(this.trades, false);
				toasts.push({ kind: 'success', title: `${label} · #${res.assigned_id}`, body: describe(res) });
			} else {
				toasts.push({ kind: 'error', title: `${label} rejected`, body: res.reason ?? 'Rejected by the engine' });
			}
			this.kick();
			return res;
		} catch (e) {
			toasts.push({ kind: 'error', title: `${label} failed`, body: errorMessage(e) });
			return null;
		}
	}

	async cancel(id: Id) {
		try {
			await api.cancelOrder(id);
			this.openOrders = this.openOrders.filter((o) => o.id !== id);
			toasts.push({ kind: 'info', title: `Cancelled #${id}` });
		} catch (e) {
			toasts.push({ kind: 'error', title: `Cancel #${id} failed`, body: errorMessage(e) });
		}
		this.kick();
	}

	async cancelAll() {
		const ids = this.openOrders.map((o) => o.id);
		const results = await Promise.allSettled(ids.map((id) => api.cancelOrder(id)));
		const ok = results.filter((r) => r.status === 'fulfilled').length;
		toasts.push({
			kind: ok === ids.length ? 'info' : 'error',
			title: `Cancelled ${ok} of ${ids.length} orders`
		});
		this.kick();
	}

	async modify(id: Id, patch: ModifyRequest) {
		try {
			const updated = await api.modifyOrder(id, patch);
			toasts.push({
				kind: 'success',
				title: `Modified #${id}`,
				body: updated
					? `${fmtSize(updated.size)} @ ${fmtPrice(updated.price)}`
					: 'Order is no longer resting'
			});
			this.kick();
			return true;
		} catch (e) {
			toasts.push({ kind: 'error', title: `Modify #${id} failed`, body: errorMessage(e) });
			this.kick();
			return false;
		}
	}

	async resetAccount() {
		try {
			this.account = await api.resetAccount();
			this.fills = [];
			this.traces = [];
			this.openOrders = [];
			toasts.push({ kind: 'info', title: 'Account reset', body: 'Open orders cancelled, starting balances restored.' });
		} catch (e) {
			toasts.push({ kind: 'error', title: 'Reset failed', body: errorMessage(e) });
		}
		this.kick();
	}
}

function describe(res: OrderResponse) {
	const parts: string[] = [];
	if (res.filled_size > 0) {
		const notional = res.trades.reduce((s, t) => s + t.resting_price * t.trade_size, 0);
		parts.push(`Filled ${fmtSize(res.filled_size)} @ avg ${fmtPrice(notional / res.filled_size)}`);
	}
	if (res.remaining_size > 0) parts.push(`${fmtSize(res.remaining_size)} resting`);
	if (!parts.length) parts.push(res.status === 'CANCELLED' ? 'Nothing to match; remainder cancelled' : res.status);
	return parts.join(' · ');
}

export const market = new MarketStore();
