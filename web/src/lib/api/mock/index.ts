/*
 * MockExchangeApi: a self-contained fake exchange that runs in the browser.
 *
 * A background loop plays a crowd of simulated traders against the mock engine:
 *
 *   fair value   geometric Brownian motion,  F(t+dt) = F(t) * exp(sigma * sqrt(dt) * Z)
 *   makers       Poisson arrivals; distance from fair value d ~ Exp(k), i.e. arrival
 *                intensity decays like e^(-k d) away from the touch (the shape used in
 *                Avellaneda–Stoikov style market-making models)
 *   takers       Poisson arrivals, more likely to buy when fair value sits above the mid,
 *                which is what drags the book toward fair value
 *   cancels      Poisson, biased toward orders far from fair value
 *
 * Your own orders go into the same book, so they rest, get hit, and show up as fills.
 * Only *your* orders go through the account layer (balances, risk checks, P&L); the
 * simulated crowd has unlimited money.
 */
import { INSTRUMENT, PRICE_SCALE } from '$lib/config';
import { fmtUsd } from '$lib/format';
import { ApiError } from '../errors';
import type {
	Account,
	BookSnapshot,
	Candle,
	ExchangeApi,
	Id,
	LevelQueue,
	MatchEvent,
	MatchTrace,
	ModifyRequest,
	NewOrderRequest,
	OpenOrder,
	OrderResponse,
	OrderStatus,
	OrderType,
	QueueOrder,
	Side,
	Trade,
	TradesPage
} from '../types';
import {
	Engine,
	type AddResult,
	type EngineEvent,
	type EngineOrder,
	type EngineQueueOrder,
	type EngineTrade
} from './engine';
import { exponential, gaussian, logNormal, pick, poisson } from './random';

const STEP_MS = 150;
/** Volatility of log fair value per sqrt(second). 0.00035 => ~0.27% per minute, ~2.1% per hour. */
const SIGMA = 0.00035;
const MAKER_RATE = 2.2; // expected maker orders per step
const TAKER_RATE = 0.3;
const CANCEL_RATE = 1.8;
const MAX_ORDERS = 500;
/** Rough traded volume per second, used to give the synthetic history plausible bars. */
const VOLUME_PER_SEC = (TAKER_RATE / (STEP_MS / 1000)) * 45;

/** Starting balances: $100,000 cash (in quote units = ticks x qty) and 1,000 OBV1. */
const START_CASH = 100_000 * PRICE_SCALE;
const START_POSITION = 1_000;

interface MyOrder {
	side: Side;
	type: OrderType;
	original: number;
	filled: number;
	ts: number;
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
const latency = () => sleep(12 + Math.random() * 30);
const bucket = (ms: number, sec: number) => Math.floor(ms / 1000 / sec) * sec;
const clamp = (x: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, x));
const usd = (quote: number) => fmtUsd(quote / PRICE_SCALE);

const toTrade = (t: EngineTrade): Trade => ({
	...t,
	resting_id: String(t.resting_id),
	incoming_id: String(t.incoming_id)
});
const toQueue = (q: EngineQueueOrder): QueueOrder => ({ ...q, id: String(q.id) });
const toEvent = (e: EngineEvent): MatchEvent =>
	e.type === 'MATCH' || e.type === 'ICEBERG_REFILL' ? { ...e, resting_id: String(e.resting_id) } : e;

export class MockExchangeApi implements ExchangeApi {
	readonly mode = 'mock' as const;

	private readonly engine = new Engine();
	private readonly startMs = Date.now();
	private readonly startPrice: number;
	private readonly mine = new Map<number, MyOrder>();
	private readonly history = new Map<number, Candle[]>();
	private readonly timer: ReturnType<typeof setInterval>;
	private nextId = 1;
	private fair: number;
	private acct = { cash: 0, position: 0, avgCost: null as number | null, realized: 0, starting: 0 };

	constructor(startPrice = 10_000) {
		this.startPrice = this.fair = startPrice;
		this.seed();
		this.resetBalances();
		this.timer = setInterval(() => this.step(), STEP_MS);
	}

	dispose() {
		clearInterval(this.timer);
	}

	// ---------------------------------------------------------------- public API

	async health() {
		return true;
	}

	async submitOrder(req: NewOrderRequest): Promise<OrderResponse> {
		await latency();
		this.validate(req);

		const id = this.nextId++;
		const iceberg = req.order_type === 'ICEBERG';
		const order: EngineOrder = {
			id,
			side: req.side,
			type: req.order_type,
			price: req.order_type === 'MARKET' ? 0 : req.price,
			size: req.size,
			reserve: iceberg ? req.reserve : 0,
			display: iceberg ? req.display_size : 0,
			ts: Date.now(),
			owner: 'me'
		};

		// Pre-trade risk check runs before the engine ever sees the order.
		const risk = this.checkFunds(order);
		if (risk) {
			const trace = this.toTrace(order, {
				bookBefore: this.engine.snapshotReach(order),
				events: [{ type: 'REJECT', reason: risk }]
			});
			return this.rejected(id, risk, trace);
		}

		const mine: MyOrder = {
			side: req.side,
			type: req.order_type,
			original: order.size + order.reserve,
			filled: 0,
			ts: order.ts
		};
		this.mine.set(id, mine);

		const res = this.engine.add(order);
		const trace = this.toTrace(order, res);
		if (!res.ok) {
			this.mine.delete(id);
			return this.rejected(id, res.reason ?? 'Rejected', trace);
		}

		this.record(res.trades);
		const remaining = res.rested ? res.rested.size + res.rested.reserve : 0;
		const status: OrderStatus = res.rested
			? mine.filled > 0
				? 'PARTIALLY_FILLED'
				: 'RESTING'
			: mine.filled >= mine.original
				? 'FILLED'
				: 'CANCELLED';
		if (!res.rested) this.mine.delete(id);

		return {
			assigned_id: String(id),
			accepted: true,
			filled_size: mine.filled,
			remaining_size: remaining,
			status,
			trades: res.trades.map(toTrade),
			trace
		};
	}

	async cancelOrder(id: Id): Promise<void> {
		await latency();
		const n = Number(id);
		if (!this.mine.has(n) || !this.engine.cancel(n)) {
			throw new ApiError(404, `Order ${id} not found or no longer resting`);
		}
		this.mine.delete(n);
	}

	async modifyOrder(id: Id, patch: ModifyRequest): Promise<OpenOrder | null> {
		await latency();
		const n = Number(id);
		const mine = this.mine.get(n);
		const o = this.engine.get(n);
		if (!mine || !o) throw new ApiError(404, `Order ${id} not found or no longer resting`);
		if (patch.size !== undefined && (!Number.isInteger(patch.size) || patch.size < 0)) {
			throw new ApiError(400, 'size must be a non-negative integer');
		}
		if (patch.price !== undefined && (!Number.isInteger(patch.price) || patch.price <= 0)) {
			throw new ApiError(400, 'price must be a positive integer number of ticks');
		}

		if (patch.size === 0) {
			this.engine.cancel(n);
			this.mine.delete(n);
			return null;
		}

		// A modify can need *more* funds (bigger buy, higher bid, bigger sell). Check the delta.
		const { cashFree, posFree } = this.free();
		const remaining = o.size + o.reserve;
		const newRemaining = patch.size !== undefined ? patch.size + o.reserve : remaining;
		const newPrice = patch.price ?? o.price;
		if (o.side === 'BUY') {
			const extra = newPrice * newRemaining - o.price * remaining;
			if (extra > cashFree) throw new ApiError(409, `Insufficient USD: modify needs ${usd(extra)} more, ${usd(cashFree)} available`);
		} else if (newRemaining - remaining > posFree) {
			throw new ApiError(409, `Insufficient ${INSTRUMENT.base}: modify needs ${newRemaining - remaining} more, ${posFree} available`);
		}

		if (patch.size !== undefined) {
			this.engine.modifySize(n, patch.size);
			mine.original = mine.filled + patch.size + o.reserve;
		}
		if (patch.price !== undefined && patch.price !== o.price) {
			const res = this.engine.modifyPrice(n, patch.price);
			if (res && !res.ok) {
				this.mine.delete(n);
				throw new ApiError(409, `${res.reason}; order cancelled`);
			}
			if (res) this.record(res.trades);
		}
		return this.openOrder(n);
	}

	async getBook(depth: number): Promise<BookSnapshot> {
		await latency();
		return { seq: this.engine.version, ts: Date.now(), ...this.engine.depth(depth) };
	}

	async getLevel(side: Side, price: number): Promise<LevelQueue> {
		await latency();
		return { side, price, orders: this.engine.level(side, price).map(toQueue) };
	}

	async getTrades(after?: number, limit = 100): Promise<TradesPage> {
		await latency();
		const all = this.engine.trades;
		const start = after === undefined ? Math.max(0, all.length - limit) : after + 1;
		const page = all.slice(start, start + limit);
		return {
			trades: page.map(toTrade),
			next_seq: page.length ? page[page.length - 1].seq : (after ?? all.length - 1)
		};
	}

	async getOpenOrders(): Promise<OpenOrder[]> {
		await latency();
		const out: OpenOrder[] = [];
		for (const id of this.mine.keys()) {
			const o = this.openOrder(id);
			if (o) out.push(o);
		}
		return out.sort((a, b) => b.ts - a.ts);
	}

	async getCandles(intervalSec: number, limit: number): Promise<Candle[]> {
		await latency();
		const live = this.liveCandles(intervalSec);
		const synth = this.syntheticHistory(intervalSec, limit);
		return [...synth, ...live].slice(-limit);
	}

	async getAccount(): Promise<Account> {
		await latency();
		return this.account();
	}

	async resetAccount(): Promise<Account> {
		await latency();
		for (const id of this.mine.keys()) this.engine.cancel(id);
		this.mine.clear();
		this.resetBalances();
		return this.account();
	}

	// ---------------------------------------------------------------- account

	private resetBalances() {
		const mark = this.engine.trades.at(-1)?.resting_price ?? Math.round(this.fair);
		this.acct = {
			cash: START_CASH,
			position: START_POSITION,
			avgCost: mark,
			realized: 0,
			starting: START_CASH + START_POSITION * mark
		};
	}

	/** Funds locked by resting orders: buys lock price x remaining, sells lock the quantity. */
	private reserved() {
		let cash = 0;
		let qty = 0;
		for (const id of this.mine.keys()) {
			const o = this.engine.get(id);
			if (!o) continue;
			const remaining = o.size + o.reserve;
			if (o.side === 'BUY') cash += o.price * remaining;
			else qty += remaining;
		}
		return { cash, qty };
	}

	private free() {
		const r = this.reserved();
		return { cashFree: this.acct.cash - r.cash, posFree: this.acct.position - r.qty };
	}

	private account(): Account {
		const r = this.reserved();
		return {
			cash: this.acct.cash,
			cash_reserved: r.cash,
			position: this.acct.position,
			position_reserved: r.qty,
			avg_cost: this.acct.avgCost,
			realized_pnl: Math.round(this.acct.realized),
			starting_equity: this.acct.starting
		};
	}

	/**
	 * Spot rules: a buy must be fully funded by free cash at its worst-case price, a sell by
	 * free position. Worst case for a limit buy is limit x size (fills can only be cheaper);
	 * for a market buy it's the cost of sweeping the book, hidden reserve included.
	 */
	private checkFunds(o: EngineOrder): string | null {
		const { cashFree, posFree } = this.free();
		const total = o.size + o.reserve;
		if (o.side === 'SELL') {
			return total > posFree
				? `Insufficient ${INSTRUMENT.base}: order needs ${total}, ${posFree} available`
				: null;
		}
		const need = o.type === 'MARKET' ? this.engine.sweepCost('BUY', total).cost : o.price * total;
		return need > cashFree ? `Insufficient USD: order needs ${usd(need)}, ${usd(cashFree)} available` : null;
	}

	/**
	 * Average-cost accounting. A buy blends into the average,
	 *   avg' = (avg * pos + price * qty) / (pos + qty),
	 * and a sell realizes (price - avg) * qty without moving the average.
	 */
	private settle(side: Side, price: number, qty: number) {
		const a = this.acct;
		if (side === 'BUY') {
			a.avgCost = ((a.avgCost ?? 0) * a.position + price * qty) / (a.position + qty);
			a.position += qty;
			a.cash -= price * qty;
		} else {
			a.realized += (price - (a.avgCost ?? price)) * qty;
			a.position -= qty;
			a.cash += price * qty;
			if (a.position === 0) a.avgCost = null;
		}
	}

	// ---------------------------------------------------------------- internals

	private validate(req: NewOrderRequest) {
		const bad = (msg: string) => {
			throw new ApiError(400, msg);
		};
		if (req.side !== 'BUY' && req.side !== 'SELL') bad('side must be BUY or SELL');
		if (!Number.isInteger(req.size) || req.size <= 0) bad('size must be a positive integer');
		if (req.order_type !== 'MARKET' && (!Number.isInteger(req.price) || req.price <= 0)) {
			bad('price must be a positive integer number of ticks');
		}
		if (req.order_type === 'ICEBERG') {
			if (!Number.isInteger(req.display_size) || req.display_size <= 0) bad('display_size must be > 0');
			if (!Number.isInteger(req.reserve) || req.reserve < 0) bad('reserve must be >= 0');
		}
	}

	private rejected(id: number, reason: string, trace: MatchTrace): OrderResponse {
		return {
			assigned_id: String(id),
			accepted: false,
			filled_size: 0,
			remaining_size: 0,
			status: 'REJECTED',
			reason,
			trades: [],
			trace
		};
	}

	private toTrace(o: EngineOrder, res: Pick<AddResult, 'events' | 'bookBefore'>): MatchTrace {
		return {
			order_id: String(o.id),
			side: o.side,
			order_type: o.type,
			price: o.price,
			size: o.size + o.reserve,
			ts: o.ts,
			book_before: res.bookBefore.map((l) => ({ price: l.price, orders: l.orders.map(toQueue) })),
			events: res.events.map(toEvent)
		};
	}

	private openOrder(id: number): OpenOrder | null {
		const o = this.engine.get(id);
		const m = this.mine.get(id);
		if (!o || !m) return null;
		const q = this.engine.queueInfo(id);
		return {
			id: String(id),
			side: o.side,
			order_type: o.type,
			price: o.price,
			size: o.size + o.reserve,
			visible_size: o.size,
			original_size: m.original,
			filled_size: m.filled,
			ts: m.ts,
			status: m.filled > 0 ? 'PARTIALLY_FILLED' : 'RESTING',
			queue_position: q?.position,
			size_ahead: q?.ahead
		};
	}

	/** Attribute fills to our own orders, settle them, and forget the ones that are done. */
	private record(trades: EngineTrade[]) {
		for (const t of trades) {
			const resting = this.mine.get(t.resting_id);
			if (resting) {
				resting.filled += t.trade_size;
				this.settle(resting.side, t.resting_price, t.trade_size);
			}
			const incoming = this.mine.get(t.incoming_id);
			if (incoming) {
				incoming.filled += t.trade_size;
				this.settle(incoming.side, t.resting_price, t.trade_size);
			}
		}
		for (const t of trades) {
			if (this.mine.has(t.resting_id) && !this.engine.has(t.resting_id)) {
				this.mine.delete(t.resting_id);
			}
		}
	}

	private simAdd(o: Omit<EngineOrder, 'id' | 'ts' | 'owner'>) {
		const res = this.engine.add({ ...o, id: this.nextId++, ts: Date.now(), owner: 'sim' });
		this.record(res.trades);
	}

	private seed() {
		for (let d = 1; d <= 32; d++) {
			for (const side of ['BUY', 'SELL'] as const) {
				const price = side === 'BUY' ? this.startPrice - d : this.startPrice + d;
				for (let k = poisson(1.6) + 1; k > 0; k--) {
					// Slightly thicker away from the touch, the usual hump-shaped book.
					const size = Math.round(clamp(logNormal(Math.log(30 + d * 2), 0.8), 1, 4000));
					this.simAdd({ side, type: 'GTC', price, size, reserve: 0, display: 0 });
				}
			}
		}
	}

	private placeMaker(side: Side) {
		const d = 1 + Math.floor(exponential(1 / 5)); // mean ~6 ticks from fair value
		const price =
			side === 'BUY' ? Math.floor(this.fair) - d + 1 : Math.ceil(this.fair) + d - 1;
		if (price < 1) return;
		const size = Math.round(clamp(logNormal(Math.log(45), 1), 1, 5000));
		if (Math.random() < 0.03 && size > 20) {
			const display = Math.ceil(size / 5);
			this.simAdd({ side, type: 'ICEBERG', price, size: display, reserve: size * 3, display });
		} else {
			this.simAdd({ side, type: 'GTC', price, size, reserve: 0, display: 0 });
		}
	}

	private step() {
		const dt = STEP_MS / 1000;
		this.fair *= Math.exp(SIGMA * Math.sqrt(dt) * gaussian());

		const bb = this.engine.bestBid();
		const ba = this.engine.bestAsk();
		const mid = bb !== undefined && ba !== undefined ? (bb + ba) / 2 : this.fair;

		for (let i = poisson(MAKER_RATE); i > 0; i--) this.placeMaker(Math.random() < 0.5 ? 'BUY' : 'SELL');
		if (this.engine.levelCount('BUY') < 15) this.placeMaker('BUY');
		if (this.engine.levelCount('SELL') < 15) this.placeMaker('SELL');

		for (let i = poisson(TAKER_RATE); i > 0; i--) {
			// Logistic in the mispricing: P(buy) = 1 / (1 + e^-((F - mid) / 3 ticks)).
			const pBuy = 1 / (1 + Math.exp(-(this.fair - mid) / 3));
			const side: Side = Math.random() < pBuy ? 'BUY' : 'SELL';
			const size = Math.round(clamp(logNormal(Math.log(30), 0.9), 1, 1500));
			const market = Math.random() < 0.6;
			const price = side === 'BUY' ? Math.round(this.fair) + 3 : Math.round(this.fair) - 3;
			this.simAdd({ side, type: market ? 'MARKET' : 'IOC', price, size, reserve: 0, display: 0 });
		}

		const ids = this.engine.ids().filter((id) => !this.mine.has(id));
		const cancels = poisson(CANCEL_RATE) + Math.max(0, ids.length - MAX_ORDERS);
		for (let i = 0; i < cancels && ids.length; i++) {
			// Of three random resting orders, pull the one furthest from fair value.
			let far = pick(ids);
			let farDist = -1;
			for (let k = 0; k < 3; k++) {
				const id = pick(ids);
				const o = this.engine.get(id);
				const dist = o ? Math.abs(o.price - this.fair) : -1;
				if (dist > farDist) {
					far = id;
					farDist = dist;
				}
			}
			this.engine.cancel(far);
		}
	}

	/** Bars built from trades that actually happened since the mock started. Empty buckets are skipped. */
	private liveCandles(sec: number): Candle[] {
		const out: Candle[] = [];
		for (const t of this.engine.trades) {
			const time = bucket(t.ts, sec);
			const last = out[out.length - 1];
			const p = t.resting_price;
			if (last && last.time === time) {
				last.high = Math.max(last.high, p);
				last.low = Math.min(last.low, p);
				last.close = p;
				last.volume += t.trade_size;
			} else {
				out.push({ time, open: p, high: p, low: p, close: p, volume: t.trade_size });
			}
		}
		return out;
	}

	/**
	 * Invented history before the mock started, so the chart isn't empty on load.
	 * Walks *backwards* from the start price. Per-bar volatility follows Brownian scaling,
	 * sd(log return over dt) = sigma * sqrt(dt), so 1h bars are sqrt(3600) = 60x wider than 1s bars.
	 */
	private syntheticHistory(sec: number, count: number): Candle[] {
		const cached = this.history.get(sec);
		if (cached && cached.length >= count) return cached.slice(-count);

		const s = SIGMA * Math.sqrt(sec);
		const firstLive = bucket(this.startMs, sec);
		const bars: Candle[] = [];
		let close = this.startPrice;
		for (let i = 1; i <= count; i++) {
			const open = close * Math.exp(-s * gaussian());
			const high = Math.max(open, close) * Math.exp(Math.abs(gaussian()) * s * 0.35);
			const low = Math.min(open, close) * Math.exp(-Math.abs(gaussian()) * s * 0.35);
			bars.push({
				time: firstLive - i * sec,
				open: Math.round(open),
				high: Math.round(high),
				low: Math.round(low),
				close: Math.round(close),
				volume: Math.round(VOLUME_PER_SEC * sec * logNormal(0, 0.45))
			});
			close = open;
		}
		bars.reverse();
		this.history.set(sec, bars);
		return bars;
	}
}
