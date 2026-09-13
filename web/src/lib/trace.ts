/*
 * Replaying a MatchTrace. `traceState(trace, k)` rebuilds the reachable book as it looked
 * after the first k events, starting from the `book_before` snapshot. Recomputing from
 * scratch each step (instead of mutating a live copy) makes stepping backwards free.
 */
import { ORDER_TYPE_META, type MatchEvent, type MatchTrace, type QueueOrder } from './api/types';
import { fmtPrice, fmtSize, ordinal } from './format';

export type VizOrderState = 'idle' | 'hit' | 'filled' | 'refilled';

export interface VizOrder extends QueueOrder {
	state: VizOrderState;
	/** Quantity taken from this order by the current step. */
	took: number;
}

export interface VizLevel {
	price: number;
	orders: VizOrder[];
	cleared: boolean;
	/** Within the incoming order's limit. */
	reachable: boolean;
}

export interface VizState {
	levels: VizLevel[];
	filled: number;
	/** Sum of price x qty over fills, in ticks x qty. */
	notional: number;
	rested: { price: number; size: number; queue_position: number } | null;
	dropped: number;
	rejected: string | null;
}

export function reachable(t: MatchTrace, price: number) {
	if (t.order_type === 'MARKET') return true;
	return t.side === 'BUY' ? price <= t.price : price >= t.price;
}

export function traceState(t: MatchTrace, step: number): VizState {
	const known = new Map<string, QueueOrder>();
	const levels: VizLevel[] = t.book_before.map((l) => ({
		price: l.price,
		cleared: false,
		reachable: reachable(t, l.price),
		orders: l.orders.map((o) => {
			known.set(o.id, o);
			return { ...o, state: 'idle' as const, took: 0 };
		})
	}));
	const s: VizState = { levels, filled: 0, notional: 0, rested: null, dropped: 0, rejected: null };

	const levelAt = (price: number) => {
		let l = levels.find((x) => x.price === price);
		if (!l) {
			// Deeper than the snapshot captured: add it so the step still has somewhere to land.
			l = { price, orders: [], cleared: false, reachable: reachable(t, price) };
			levels.push(l);
			levels.sort((a, b) => (t.side === 'BUY' ? a.price - b.price : b.price - a.price));
		}
		return l;
	};

	for (let i = 0; i < Math.min(step, t.events.length); i++) {
		// Whatever the previous step highlighted settles before the next one is shown.
		for (const l of levels) {
			l.orders = l.orders.filter((o) => o.state !== 'filled');
			for (const o of l.orders) {
				o.state = 'idle';
				o.took = 0;
			}
		}

		const e = t.events[i];
		switch (e.type) {
			case 'MATCH': {
				const l = levelAt(e.price);
				let o = l.orders.find((x) => x.id === e.resting_id);
				if (!o) {
					const base = known.get(e.resting_id);
					o = { id: e.resting_id, size: 0, ts: base?.ts ?? 0, mine: base?.mine ?? false, iceberg: base?.iceberg ?? false, state: 'idle', took: 0 };
					l.orders.unshift(o);
				}
				o.took = e.qty;
				o.size = e.resting_remaining;
				o.state = e.resting_remaining === 0 ? 'filled' : 'hit';
				s.filled += e.qty;
				s.notional += e.qty * e.price;
				break;
			}
			case 'ICEBERG_REFILL': {
				const l = levelAt(e.price);
				const base = known.get(e.resting_id);
				l.orders = l.orders.filter((x) => x.id !== e.resting_id);
				l.orders.push({ id: e.resting_id, size: e.size, ts: base?.ts ?? 0, mine: base?.mine ?? false, iceberg: true, state: 'refilled', took: 0 });
				break;
			}
			case 'LEVEL_CLEARED': {
				const l = levelAt(e.price);
				l.cleared = true;
				l.orders = [];
				break;
			}
			case 'REST':
				s.rested = { price: e.price, size: e.size, queue_position: e.queue_position };
				break;
			case 'DROP':
				s.dropped = e.size;
				break;
			case 'REJECT':
				s.rejected = e.reason;
				break;
		}
	}
	return s;
}

export interface Caption {
	title: string;
	detail: string;
}

export function introCaption(t: MatchTrace): Caption {
	const limit =
		t.order_type === 'MARKET'
			? 'at any price'
			: `at ${t.side === 'BUY' ? 'or below' : 'or above'} ${fmtPrice(t.price)}`;
	return {
		title: `${t.side} ${ORDER_TYPE_META[t.order_type].label} for ${fmtSize(t.size)} arrives`,
		detail: `It can trade against ${t.side === 'BUY' ? 'asks' : 'bids'} ${limit}. The engine starts at the best price and works outward.`
	};
}

export function describeEvent(e: MatchEvent, t: MatchTrace): Caption {
	const opp = t.side === 'BUY' ? 'ask' : 'bid';
	switch (e.type) {
		case 'MATCH':
			return {
				title: `Matched ${fmtSize(e.qty)} @ ${fmtPrice(e.price)} against #${e.resting_id}`,
				detail:
					e.resting_remaining > 0
						? `The resting order is only partly filled; its ${fmtSize(e.resting_remaining)} left keeps its place at the front of the queue.`
						: `Price priority picked the best ${opp}; time priority picked the oldest order there. It's fully filled and leaves the queue.`
			};
		case 'ICEBERG_REFILL':
			return {
				title: `Iceberg #${e.resting_id} refilled ${fmtSize(e.size)}`,
				detail: `Its visible slice ran out, so the next slice joins the *back* of the queue and loses time priority. ${fmtSize(e.reserve)} still hidden.`
			};
		case 'LEVEL_CLEARED':
			return {
				title: `Level ${fmtPrice(e.price)} cleared`,
				detail: `No orders left at this price, so the level is erased from the map and the next ${opp} becomes the best.`
			};
		case 'REST':
			return {
				title: `Rested ${fmtSize(e.size)} @ ${fmtPrice(e.price)}`,
				detail: `Nothing left to match within the limit, so the remainder joins the back of the ${t.side === 'BUY' ? 'bid' : 'ask'} queue in ${ordinal(e.queue_position)} place.`
			};
		case 'DROP':
			return {
				title: `Dropped ${fmtSize(e.size)} unfilled`,
				detail:
					t.order_type === 'MARKET'
						? "Market orders never rest; whatever the book couldn't fill is discarded."
						: 'Immediate-or-cancel: the unfilled remainder is cancelled instead of resting.'
			};
		case 'REJECT':
			return { title: 'Rejected', detail: e.reason };
	}
}

/** One-line outcome for the history table. */
export function summarizeTrace(t: MatchTrace): string {
	const s = traceState(t, t.events.length);
	if (s.rejected) return `Rejected: ${s.rejected}`;
	const parts: string[] = [];
	if (s.filled > 0) {
		const levels = new Set(t.events.filter((e) => e.type === 'MATCH').map((e) => (e.type === 'MATCH' ? e.price : 0)));
		parts.push(`Filled ${fmtSize(s.filled)} across ${levels.size} level${levels.size === 1 ? '' : 's'} · avg ${fmtPrice(s.notional / s.filled)}`);
	}
	if (s.rested) parts.push(`rested ${fmtSize(s.rested.size)} (${ordinal(s.rested.queue_position)} in queue)`);
	if (s.dropped) parts.push(`dropped ${fmtSize(s.dropped)}`);
	return parts.join(' · ') || 'No action';
}
