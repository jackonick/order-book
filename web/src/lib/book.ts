import type { BookSnapshot, Level, OrderType, Side } from './api/types';

export interface CumLevel extends Level {
	/** Running total from the touch outward. */
	cum: number;
}

/**
 * Buckets levels into groups of `group` ticks.
 * Bids round down and asks round up, so a grouped row never shows a better price than
 * actually exists on the book.
 */
export function groupPrice(price: number, group: number, side: Side) {
	if (group <= 1) return price;
	return side === 'BUY' ? Math.floor(price / group) * group : Math.ceil(price / group) * group;
}

export function groupLevels(levels: Level[], group: number, side: Side): Level[] {
	if (group <= 1) return levels;
	const out: Level[] = [];
	for (const l of levels) {
		const p = groupPrice(l.price, group, side);
		const last = out[out.length - 1];
		// Input is sorted best-first, so equal buckets are always adjacent.
		if (last && last.price === p) {
			last.size += l.size;
			last.orders += l.orders;
		} else {
			out.push({ price: p, size: l.size, orders: l.orders });
		}
	}
	return out;
}

export function cumulate(levels: Level[]): CumLevel[] {
	let cum = 0;
	return levels.map((l) => ({ ...l, cum: (cum += l.size) }));
}

/**
 * Order-book imbalance over the top `n` levels:  I = (Vb - Va) / (Vb + Va),  I in [-1, 1].
 * Positive means more resting size on the bid, which tends to lead short-term upticks.
 */
export function imbalance(book: BookSnapshot, n = 10) {
	const vb = book.bids.slice(0, n).reduce((s, l) => s + l.size, 0);
	const va = book.asks.slice(0, n).reduce((s, l) => s + l.size, 0);
	return vb + va === 0 ? 0 : (vb - va) / (vb + va);
}

/**
 * Microprice: the mid weighted by the *opposite* side's top-of-book size,
 *   P_micro = (P_bid * V_ask + P_ask * V_bid) / (V_bid + V_ask).
 * A heavy bid (large V_bid) pulls it toward the ask, because the ask is the side more
 * likely to get taken next. A better short-horizon fair value than the plain mid.
 */
export function microprice(book: BookSnapshot) {
	const b = book.bids[0];
	const a = book.asks[0];
	if (!b || !a || b.size + a.size === 0) return null;
	return (b.price * a.size + a.price * b.size) / (b.size + a.size);
}

export interface FillEstimate {
	/** Quantity that would execute immediately. */
	filled: number;
	/** Sum of price * qty over the fills, in ticks * qty. */
	notionalTicks: number;
	avgPrice: number | null;
	worstPrice: number | null;
	levelsTouched: number;
	/** Quantity that would rest on the book afterwards. */
	rests: number;
	/** Quantity that would be dropped (IOC / MARKET remainder). */
	dropped: number;
	/** Average fill vs. the touch, in basis points (always >= 0: it's a cost). */
	slippageBps: number | null;
	crosses: boolean;
	rejected: 'BOC_CROSSES' | 'FOK_SHORT' | null;
}

/**
 * Walks the opposite side of the book the way add_order's matching loop does, to preview
 * what an order would do *before* sending it. Only as accurate as the visible depth we
 * fetched, and iceberg reserve is invisible by design.
 */
export function estimateFill(
	book: BookSnapshot,
	side: Side,
	type: OrderType,
	limit: number | null,
	size: number,
	/** For icebergs, only the display slice is aggressive (it's what add_order sees as size). */
	aggressiveSize = size
): FillEstimate {
	const opp = side === 'BUY' ? book.asks : book.bids;
	const touch = opp[0]?.price ?? null;
	const inLimit = (p: number) =>
		type === 'MARKET' || limit === null ? true : side === 'BUY' ? p <= limit : p >= limit;
	const crosses = touch !== null && inLimit(touch);

	const empty: FillEstimate = {
		filled: 0,
		notionalTicks: 0,
		avgPrice: null,
		worstPrice: null,
		levelsTouched: 0,
		rests: 0,
		dropped: 0,
		slippageBps: null,
		crosses,
		rejected: null
	};

	if (type === 'BOC' && crosses) return { ...empty, rejected: 'BOC_CROSSES' };

	let remaining = aggressiveSize;
	let notional = 0;
	let worst: number | null = null;
	let touched = 0;
	for (const l of opp) {
		if (remaining <= 0 || !inLimit(l.price)) break;
		const take = Math.min(remaining, l.size);
		remaining -= take;
		notional += take * l.price;
		worst = l.price;
		touched++;
	}
	const filled = aggressiveSize - remaining;

	if (type === 'FOK' && filled < size) return { ...empty, rejected: 'FOK_SHORT' };

	const avg = filled > 0 ? notional / filled : null;
	const slip =
		avg !== null && touch !== null
			? (side === 'BUY' ? avg / touch - 1 : 1 - avg / touch) * 1e4
			: null;
	const leftover = size - filled;
	const rests = type === 'GTC' || type === 'BOC' || type === 'ICEBERG' ? leftover : 0;

	return {
		filled,
		notionalTicks: notional,
		avgPrice: avg,
		worstPrice: worst,
		levelsTouched: touched,
		rests,
		dropped: leftover - rests,
		slippageBps: slip,
		crosses,
		rejected: null
	};
}
