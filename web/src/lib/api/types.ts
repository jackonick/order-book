/*
 * The REST contract between this frontend and the engine.
 *
 * Field names deliberately mirror the C++ side (order_book.h and exchange.proto) so the
 * server can serialize its structs almost directly:
 *
 *   Side / Type enums     -> Side / OrderType  (sent as strings; wire ints listed below)
 *   Order                 -> NewOrderRequest / OpenOrder
 *   Trade                 -> Trade   (+ seq, aggressor, ts, which the server knows)
 *   NewOrderRequest (pb)  -> NewOrderRequest
 *   OrderResponse   (pb)  -> OrderResponse (+ status, remaining_size, reason, trades, trace)
 *
 * Units:
 *   price  integer ticks (uint64 in C++). See INSTRUMENT.priceDecimals for display.
 *   size   integer quantity.
 *   cash   quote amounts are price ticks x quantity (with 2 price decimals: cents).
 *   ids    uint64 sent as JSON *strings*. JSON numbers are IEEE doubles, which are only
 *          exact up to 2^53, so a uint64 id can silently lose its low bits as a number.
 *          The client also accepts numeric ids and converts them.
 *   ts     milliseconds since the Unix epoch.
 */

export type Side = 'BUY' | 'SELL';

/** Same members and order as `enum class Type` in order_book.h. */
export type OrderType = 'GTC' | 'FOK' | 'MARKET' | 'IOC' | 'BOC' | 'ICEBERG';

export type OrderStatus = 'RESTING' | 'PARTIALLY_FILLED' | 'FILLED' | 'CANCELLED' | 'REJECTED';

export type Id = string;

/** Integer encodings matching exchange.proto (side) and the C++ Type enum (order_type). */
export const SIDE_WIRE: Record<Side, number> = { BUY: 0, SELL: 1 };
export const TYPE_WIRE: Record<OrderType, number> = {
	GTC: 0,
	FOK: 1,
	MARKET: 2,
	IOC: 3,
	BOC: 4,
	ICEBERG: 5
};

export interface NewOrderRequest {
	side: Side;
	order_type: OrderType;
	/** Limit price in ticks. Ignored (send 0) for MARKET. */
	price: number;
	/** Visible quantity. For ICEBERG this is the first display slice. */
	size: number;
	/** ICEBERG only: hidden quantity behind the visible slice. */
	reserve: number;
	/** ICEBERG only: slice size shown each time the order refills. */
	display_size: number;
}

export interface OrderResponse {
	assigned_id: Id;
	accepted: boolean;
	filled_size: number;
	/** Quantity left resting on the book (visible + reserve). 0 if nothing rested. */
	remaining_size: number;
	status: OrderStatus;
	/** Human-readable reason when accepted === false (e.g. "BOC would cross"). */
	reason?: string;
	/** Trades this order produced on arrival. */
	trades: Trade[];
	/** Step-by-step record of what the matching loop did (drives the visualizer). Optional. */
	trace?: MatchTrace;
}

export interface Trade {
	/** Position in the engine's trade log (index into `Trades`). Strictly increasing. */
	seq: number;
	resting_id: Id;
	incoming_id: Id;
	resting_price: number;
	trade_size: number;
	/** Side of the incoming (aggressive) order. */
	aggressor: Side;
	ts: number;
}

/** One aggregated price level, as printDepth computes it. */
export interface Level {
	price: number;
	/** Total visible size at this price (iceberg reserve is hidden). */
	size: number;
	/** Number of orders queued at this price. */
	orders: number;
}

export interface BookSnapshot {
	/** Monotonic book version; lets a client detect it missed updates. */
	seq: number;
	ts: number;
	/** Best (highest) first. */
	bids: Level[];
	/** Best (lowest) first. */
	asks: Level[];
}

/** One order inside a price level's FIFO queue (L3). */
export interface QueueOrder {
	id: Id;
	/** Visible size (an iceberg's hidden reserve is not included). */
	size: number;
	ts: number;
	/** True for the caller's own orders. */
	mine: boolean;
	iceberg: boolean;
}

/** A single price level with its whole queue, front (next to fill) first. */
export interface LevelQueue {
	side: Side;
	price: number;
	orders: QueueOrder[];
}

export interface TradesPage {
	/** Oldest first. */
	trades: Trade[];
	/** Cursor for the next request: pass it back as `after`. */
	next_seq: number;
}

export interface OpenOrder {
	id: Id;
	side: Side;
	order_type: OrderType;
	price: number;
	/** Remaining quantity, visible + reserve. */
	size: number;
	/** Currently displayed quantity (differs from size only for icebergs). */
	visible_size: number;
	original_size: number;
	filled_size: number;
	ts: number;
	status: OrderStatus;
	/** 1-based position in its price level's queue. Optional. */
	queue_position?: number;
	/** Visible size queued ahead of it at the same price. Optional. */
	size_ahead?: number;
}

/** Maps onto modify_order (size) and modify_price (price). Size 0 cancels. */
export interface ModifyRequest {
	size?: number;
	price?: number;
}

export interface Candle {
	/** Bucket start, Unix seconds. */
	time: number;
	open: number;
	high: number;
	low: number;
	close: number;
	volume: number;
}

/**
 * What add_order did, in order. Everything the matching loop decides becomes one event,
 * so the UI can replay it without re-implementing the engine.
 */
export type MatchEvent =
	/** Incoming order traded `qty` against the resting order at the front of the queue. */
	| { type: 'MATCH'; resting_id: Id; price: number; qty: number; resting_remaining: number }
	/** A resting iceberg's slice ran out; the next slice went to the back of the queue. */
	| { type: 'ICEBERG_REFILL'; resting_id: Id; price: number; size: number; reserve: number }
	/** The last order at a price was filled, so the level was erased. */
	| { type: 'LEVEL_CLEARED'; price: number }
	/** The remainder was added to the book. */
	| { type: 'REST'; price: number; size: number; queue_position: number }
	/** The remainder was discarded (IOC / MARKET). */
	| { type: 'DROP'; size: number }
	/** Rejected before matching (post-only crossing, FOK short, risk check). */
	| { type: 'REJECT'; reason: string };

export interface MatchTrace {
	order_id: Id;
	side: Side;
	order_type: OrderType;
	/** Limit price in ticks (0 for MARKET). */
	price: number;
	/** Total quantity, visible + reserve. */
	size: number;
	ts: number;
	/**
	 * Opposite-side levels the order could reach, copied *before* matching, best first.
	 * Includes the first level beyond the limit (if any) for context.
	 */
	book_before: { price: number; orders: QueueOrder[] }[];
	events: MatchEvent[];
}

/**
 * Spot account: no borrowing, no shorting. Amounts in quote units (price ticks x qty).
 * Reserved = locked by resting orders (buys lock price x remaining, sells lock quantity).
 */
export interface Account {
	cash: number;
	cash_reserved: number;
	position: number;
	position_reserved: number;
	/** Average cost of the current position in (fractional) ticks. null when flat. */
	avg_cost: number | null;
	realized_pnl: number;
	/** Equity when the account was opened or last reset, for return %. */
	starting_equity: number;
}

/** Everything the UI needs from a backend. `RestExchangeApi` and `MockExchangeApi` both implement it. */
export interface ExchangeApi {
	readonly mode: 'mock' | 'rest' | 'native';
	submitOrder(req: NewOrderRequest): Promise<OrderResponse>;
	cancelOrder(id: Id): Promise<void>;
	/** Resolves to the updated order, or null if the change cancelled or fully filled it. */
	modifyOrder(id: Id, patch: ModifyRequest): Promise<OpenOrder | null>;
	getBook(depth: number): Promise<BookSnapshot>;
	/** Full FIFO queue at one price (L3). */
	getLevel(side: Side, price: number): Promise<LevelQueue>;
	/** `after` omitted => the latest `limit` trades. */
	getTrades(after?: number, limit?: number): Promise<TradesPage>;
	getOpenOrders(): Promise<OpenOrder[]>;
	getCandles(intervalSec: number, limit: number): Promise<Candle[]>;
	getAccount(): Promise<Account>;
	/** Simulator convenience: cancel everything and restore starting balances. */
	resetAccount(): Promise<Account>;
	health(): Promise<boolean>;
	dispose?(): void;
}

export const ORDER_TYPES: OrderType[] = ['GTC', 'MARKET', 'BOC', 'IOC', 'FOK', 'ICEBERG'];

export const ORDER_TYPE_META: Record<
	OrderType,
	{ label: string; help: string; needsPrice: boolean; rests: boolean }
> = {
	GTC: {
		label: 'Limit',
		help: 'Good til cancelled. Matches what it can, then rests the remainder at your price.',
		needsPrice: true,
		rests: true
	},
	MARKET: {
		label: 'Market',
		help: 'Takes the best prices available until filled or the book runs out. Never rests.',
		needsPrice: false,
		rests: false
	},
	BOC: {
		label: 'Post-only',
		help: 'Book or cancel. Rejected if it would trade on arrival, so it only ever adds liquidity.',
		needsPrice: true,
		rests: true
	},
	IOC: {
		label: 'IOC',
		help: 'Immediate or cancel. Fills what it can right now and drops the rest.',
		needsPrice: true,
		rests: false
	},
	FOK: {
		label: 'FOK',
		help: 'Fill or kill. Fills completely right now, or does nothing at all.',
		needsPrice: true,
		rests: false
	},
	ICEBERG: {
		label: 'Iceberg',
		help: 'Shows only a display slice. Each time a slice fills, the next one refills at the back of the queue.',
		needsPrice: true,
		rests: true
	}
};

export const opposite = (s: Side): Side => (s === 'BUY' ? 'SELL' : 'BUY');
