/*
 * A small TypeScript stand-in for the C++ OrderBook, used only by the mock API so the UI
 * has something realistic to talk to before the real server exists. It follows the same
 * rules as order_book.cpp (price-time priority, the six order types, iceberg refills at
 * the back of the queue, modify_order keeping priority, modify_price = cancel + re-add)
 * so the frontend behaves the same when you flip VITE_API_MODE to rest.
 *
 * On top of that it records a *match trace* (see MatchEvent in types.ts): every decision
 * the matching loop makes becomes one event. API.md describes how the C++ side can emit
 * the same thing.
 *
 * It is NOT the engine. The real one is yours, in C++.
 */
import type { Level, OrderType, Side } from '../types';

export interface EngineOrder {
	id: number;
	side: Side;
	type: OrderType;
	price: number;
	size: number;
	reserve: number;
	display: number;
	ts: number;
	owner: 'me' | 'sim';
}

export interface EngineTrade {
	seq: number;
	resting_id: number;
	incoming_id: number;
	resting_price: number;
	trade_size: number;
	aggressor: Side;
	ts: number;
}

export interface EngineQueueOrder {
	id: number;
	size: number;
	ts: number;
	mine: boolean;
	iceberg: boolean;
}

export type EngineEvent =
	| { type: 'MATCH'; resting_id: number; price: number; qty: number; resting_remaining: number }
	| { type: 'ICEBERG_REFILL'; resting_id: number; price: number; size: number; reserve: number }
	| { type: 'LEVEL_CLEARED'; price: number }
	| { type: 'REST'; price: number; size: number; queue_position: number }
	| { type: 'DROP'; size: number }
	| { type: 'REJECT'; reason: string };

export interface TraceLevel {
	price: number;
	orders: EngineQueueOrder[];
}

export interface AddResult {
	ok: boolean;
	reason?: string;
	trades: EngineTrade[];
	rested: EngineOrder | null;
	events: EngineEvent[];
	/** Opposite-side levels the order could reach, copied before matching. */
	bookBefore: TraceLevel[];
}

type Priced = Pick<EngineOrder, 'side' | 'type' | 'price' | 'size'>;

interface BookSide {
	levels: Map<number, EngineOrder[]>;
	/** Sorted best-first: descending for bids, ascending for asks. */
	prices: number[];
	/** True when price a is better than price b for this side. */
	better: (a: number, b: number) => boolean;
}

const TRACE_LEVELS = 6;
const TRACE_ORDERS = 40;

export class Engine {
	private readonly bids: BookSide = { levels: new Map(), prices: [], better: (a, b) => a > b };
	private readonly asks: BookSide = { levels: new Map(), prices: [], better: (a, b) => a < b };
	private readonly index = new Map<number, { price: number; side: Side }>();
	readonly trades: EngineTrade[] = [];
	/** Bumped on every mutation. */
	version = 0;

	private sideOf(s: Side) {
		return s === 'BUY' ? this.bids : this.asks;
	}

	private oppositeOf(s: Side) {
		return s === 'BUY' ? this.asks : this.bids;
	}

	/** Binary search for the insert position that keeps `prices` best-first. */
	private insertPrice(side: BookSide, price: number) {
		let lo = 0;
		let hi = side.prices.length;
		while (lo < hi) {
			const mid = (lo + hi) >> 1;
			if (side.better(side.prices[mid], price)) lo = mid + 1;
			else hi = mid;
		}
		side.prices.splice(lo, 0, price);
	}

	private removeLevel(side: BookSide, price: number) {
		side.levels.delete(price);
		const i = side.prices.indexOf(price);
		if (i >= 0) side.prices.splice(i, 1);
	}

	/** Appends to the back of the price level's queue; returns the 1-based queue position. */
	private rest(o: EngineOrder) {
		const side = this.sideOf(o.side);
		let queue = side.levels.get(o.price);
		if (!queue) {
			queue = [];
			side.levels.set(o.price, queue);
			this.insertPrice(side, o.price);
		}
		queue.push(o);
		this.index.set(o.id, { price: o.price, side: o.side });
		return queue.length;
	}

	private toQueue(o: EngineOrder): EngineQueueOrder {
		return { id: o.id, size: o.size, ts: o.ts, mine: o.owner === 'me', iceberg: o.type === 'ICEBERG' };
	}

	bestBid(): number | undefined {
		return this.bids.prices[0];
	}

	bestAsk(): number | undefined {
		return this.asks.prices[0];
	}

	levelCount(s: Side) {
		return this.sideOf(s).prices.length;
	}

	orderCount() {
		return this.index.size;
	}

	ids() {
		return [...this.index.keys()];
	}

	has(id: number) {
		return this.index.has(id);
	}

	get(id: number): EngineOrder | undefined {
		const loc = this.index.get(id);
		if (!loc) return undefined;
		return this.sideOf(loc.side)
			.levels.get(loc.price)
			?.find((o) => o.id === id);
	}

	/** The full FIFO queue at one price, front first (L3). */
	level(side: Side, price: number): EngineQueueOrder[] {
		return (this.sideOf(side).levels.get(price) ?? []).map((o) => this.toQueue(o));
	}

	/** 1-based queue position and the visible size ahead of an order at its price. */
	queueInfo(id: number): { position: number; ahead: number } | null {
		const loc = this.index.get(id);
		const queue = loc && this.sideOf(loc.side).levels.get(loc.price);
		if (!queue) return null;
		let ahead = 0;
		for (let i = 0; i < queue.length; i++) {
			if (queue[i].id === id) return { position: i + 1, ahead };
			ahead += queue[i].size;
		}
		return null;
	}

	/**
	 * What it would cost to take `qty` from the side opposite to `side`, walking best prices
	 * first and counting hidden iceberg reserve (which a sweep would also consume).
	 * Used for the pre-trade funds check on market buys.
	 */
	sweepCost(side: Side, qty: number): { qty: number; cost: number } {
		const opp = this.oppositeOf(side);
		let left = qty;
		let cost = 0;
		for (const price of opp.prices) {
			for (const o of opp.levels.get(price)!) {
				const take = Math.min(left, o.size + o.reserve);
				cost += take * price;
				left -= take;
				if (left === 0) return { qty, cost };
			}
		}
		return { qty: qty - left, cost };
	}

	private crosses(o: Priced, price: number) {
		return o.type === 'MARKET' || (o.side === 'BUY' ? price <= o.price : price >= o.price);
	}

	/** Same check as OrderBook::canFill: is there enough volume within the limit? */
	private canFill(o: Priced) {
		const opp = this.oppositeOf(o.side);
		let volume = 0;
		for (const p of opp.prices) {
			if (!this.crosses(o, p)) break;
			for (const r of opp.levels.get(p)!) volume += r.size;
			if (volume >= o.size) return true;
		}
		return false;
	}

	/**
	 * Copies the opposite-side levels an order can reach (until their size covers it),
	 * plus the first level beyond its limit for context. This is the "before" picture the
	 * visualizer animates.
	 */
	snapshotReach(o: Priced): TraceLevel[] {
		const opp = this.oppositeOf(o.side);
		const out: TraceLevel[] = [];
		let cum = 0;
		for (const price of opp.prices) {
			if (out.length >= TRACE_LEVELS) break;
			const queue = opp.levels.get(price)!;
			out.push({ price, orders: queue.slice(0, TRACE_ORDERS).map((q) => this.toQueue(q)) });
			if (!this.crosses(o, price)) break;
			for (const r of queue) cum += r.size + r.reserve;
			if (cum >= o.size) break;
		}
		return out;
	}

	add(incoming: EngineOrder): AddResult {
		const o = { ...incoming };
		const opp = this.oppositeOf(o.side);
		const trades: EngineTrade[] = [];
		const events: EngineEvent[] = [];
		const bookBefore = this.snapshotReach(o);
		const reject = (reason: string): AddResult => ({
			ok: false,
			reason,
			trades,
			rested: null,
			events: [{ type: 'REJECT', reason }],
			bookBefore
		});

		if (o.type === 'BOC' && opp.prices.length && this.crosses(o, opp.prices[0])) {
			return reject('Post-only order would cross the book');
		}
		if (o.type === 'FOK' && !this.canFill(o)) {
			return reject('Fill-or-kill: not enough liquidity at that price');
		}

		this.version++;
		while (o.size > 0 && opp.prices.length && this.crosses(o, opp.prices[0])) {
			const price = opp.prices[0];
			const queue = opp.levels.get(price)!;
			const resting = queue[0];
			const qty = Math.min(o.size, resting.size);
			o.size -= qty;
			resting.size -= qty;

			const t: EngineTrade = {
				seq: this.trades.length,
				resting_id: resting.id,
				incoming_id: o.id,
				resting_price: resting.price,
				trade_size: qty,
				aggressor: o.side,
				ts: Date.now()
			};
			this.trades.push(t);
			trades.push(t);
			events.push({ type: 'MATCH', resting_id: resting.id, price, qty, resting_remaining: resting.size });

			if (resting.size === 0) {
				queue.shift();
				if (resting.type === 'ICEBERG' && resting.reserve > 0) {
					// Next slice refills at the back of the queue, giving up time priority.
					const slice = Math.min(resting.display, resting.reserve);
					resting.size = slice;
					resting.reserve -= slice;
					queue.push(resting);
					events.push({ type: 'ICEBERG_REFILL', resting_id: resting.id, price, size: slice, reserve: resting.reserve });
				} else {
					this.index.delete(resting.id);
					if (queue.length === 0) {
						this.removeLevel(opp, price);
						events.push({ type: 'LEVEL_CLEARED', price });
					}
				}
			}
		}

		let rested: EngineOrder | null = null;
		if (o.size > 0 && o.type !== 'IOC' && o.type !== 'MARKET') {
			const position = this.rest(o);
			rested = o;
			events.push({ type: 'REST', price: o.price, size: o.size, queue_position: position });
		} else if (o.size > 0) {
			events.push({ type: 'DROP', size: o.size + o.reserve });
		}
		return { ok: true, trades, rested, events, bookBefore };
	}

	cancel(id: number): EngineOrder | null {
		const loc = this.index.get(id);
		if (!loc) return null;
		const side = this.sideOf(loc.side);
		const queue = side.levels.get(loc.price);
		const i = queue?.findIndex((o) => o.id === id) ?? -1;
		if (!queue || i < 0) return null;
		const [removed] = queue.splice(i, 1);
		this.index.delete(id);
		if (queue.length === 0) this.removeLevel(side, loc.price);
		this.version++;
		return removed;
	}

	/** Like modify_order: changes visible size in place and keeps queue position. */
	modifySize(id: number, size: number) {
		if (size === 0) return this.cancel(id) !== null;
		const o = this.get(id);
		if (!o) return false;
		o.size = size;
		this.version++;
		return true;
	}

	/** Like modify_price: cancel then re-add at the new price. Can trade, loses priority. */
	modifyPrice(id: number, price: number): AddResult | null {
		const o = this.cancel(id);
		if (!o) return null;
		return this.add({ ...o, price });
	}

	depth(n: number): { bids: Level[]; asks: Level[] } {
		const levels = (side: BookSide) =>
			side.prices.slice(0, n).map((price) => {
				const queue = side.levels.get(price)!;
				let size = 0;
				for (const o of queue) size += o.size;
				return { price, size, orders: queue.length };
			});
		return { bids: levels(this.bids), asks: levels(this.asks) };
	}
}
