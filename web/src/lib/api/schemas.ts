/*
 * Runtime validation for everything the REST server sends back.
 * TypeScript types vanish at runtime, so without this a server that sends `"price": "100"`
 * or forgets a field would surface as NaN deep in the UI. Parsing at the boundary turns that
 * into one clear error at the moment the response arrives.
 */
import { z } from 'zod';
import type {
	Account,
	BookSnapshot,
	Candle,
	LevelQueue,
	MatchTrace,
	OpenOrder,
	OrderResponse,
	Trade,
	TradesPage
} from './types';

const uint = z.number().int().nonnegative();
/** Accept "123" or 123; always hand the UI a string (see the uint64 note in types.ts). */
const id = z.union([z.string(), z.number().int()]).transform(String);

const side = z.enum(['BUY', 'SELL']);
const orderType = z.enum(['GTC', 'FOK', 'MARKET', 'IOC', 'BOC', 'ICEBERG']);
const status = z.enum(['RESTING', 'PARTIALLY_FILLED', 'FILLED', 'CANCELLED', 'REJECTED']);

export const TradeSchema: z.ZodType<Trade, unknown> = z.object({
	seq: uint,
	resting_id: id,
	incoming_id: id,
	resting_price: uint,
	trade_size: uint,
	aggressor: side,
	ts: uint
});

const LevelSchema = z.object({
	price: uint,
	size: uint,
	orders: uint.default(0)
});

export const BookSchema: z.ZodType<BookSnapshot, unknown> = z.object({
	seq: uint.default(0),
	ts: uint.default(0),
	bids: z.array(LevelSchema),
	asks: z.array(LevelSchema)
});

const QueueOrderSchema = z.object({
	id,
	size: uint,
	ts: uint.default(0),
	mine: z.boolean().default(false),
	iceberg: z.boolean().default(false)
});

export const LevelQueueSchema: z.ZodType<LevelQueue, unknown> = z.object({
	side,
	price: uint,
	orders: z.array(QueueOrderSchema)
});

export const TradesPageSchema: z.ZodType<TradesPage, unknown> = z.object({
	trades: z.array(TradeSchema),
	next_seq: z.number().int()
});

export const OpenOrderSchema: z.ZodType<OpenOrder, unknown> = z.object({
	id,
	side,
	order_type: orderType,
	price: uint,
	size: uint,
	visible_size: uint,
	original_size: uint,
	filled_size: uint,
	ts: uint,
	status,
	queue_position: uint.optional(),
	size_ahead: uint.optional()
});

export const OpenOrdersSchema = z.array(OpenOrderSchema);

const MatchEventSchema = z.discriminatedUnion('type', [
	z.object({ type: z.literal('MATCH'), resting_id: id, price: uint, qty: uint, resting_remaining: uint }),
	z.object({ type: z.literal('ICEBERG_REFILL'), resting_id: id, price: uint, size: uint, reserve: uint }),
	z.object({ type: z.literal('LEVEL_CLEARED'), price: uint }),
	z.object({ type: z.literal('REST'), price: uint, size: uint, queue_position: uint }),
	z.object({ type: z.literal('DROP'), size: uint }),
	z.object({ type: z.literal('REJECT'), reason: z.string() })
]);

export const MatchTraceSchema: z.ZodType<MatchTrace, unknown> = z.object({
	order_id: id,
	side,
	order_type: orderType,
	price: uint,
	size: uint,
	ts: uint,
	book_before: z.array(z.object({ price: uint, orders: z.array(QueueOrderSchema) })),
	events: z.array(MatchEventSchema)
});

export const OrderResponseSchema: z.ZodType<OrderResponse, unknown> = z
	.object({
		assigned_id: id,
		accepted: z.boolean(),
		filled_size: uint,
		remaining_size: uint.default(0),
		status: status.optional(),
		reason: z.string().optional(),
		trades: z.array(TradeSchema).default([]),
		trace: MatchTraceSchema.optional()
	})
	.transform((r) => ({
		...r,
		// The proto's OrderResponse has no status; derive one if the server omits it.
		status:
			r.status ??
			(!r.accepted ? 'REJECTED' : r.remaining_size > 0 ? 'RESTING' : r.filled_size > 0 ? 'FILLED' : 'CANCELLED')
	}));

export const AccountSchema: z.ZodType<Account, unknown> = z.object({
	cash: z.number(),
	cash_reserved: z.number().nonnegative(),
	position: z.number(),
	position_reserved: z.number().nonnegative(),
	avg_cost: z.number().nullable(),
	realized_pnl: z.number(),
	starting_equity: z.number()
});

export const CandleSchema: z.ZodType<Candle, unknown> = z.object({
	time: uint,
	open: uint,
	high: uint,
	low: uint,
	close: uint,
	volume: uint
});

export const CandlesSchema = z.array(CandleSchema);
