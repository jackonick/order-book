/*
 * NativeExchangeApi: the backend used inside the C++ desktop host (desktop/ in the repo).
 *
 * The host binds one function into the page:
 *     window.ob_call(method, payload) -> Promise<result>
 * Every ExchangeApi method becomes one ob_call; the host decodes the payload into the
 * C++ structs in exchange_client.hpp, runs the client's method on a worker thread, and
 * resolves the promise with JSON (or rejects it with { status, message }).
 *
 * The host can also push updates at any time by calling
 *     window.__obPush({ type: 'book' | 'trades' | 'orders' | 'account' | 'status', data })
 * which is how a gRPC stream gets onto the screen without waiting for the next poll.
 *
 * Responses and pushes go through the same zod schemas as REST, so a bad field in the
 * C++ conversion shows up as one clear error instead of NaN somewhere in the UI.
 */
import { z } from 'zod';
import { ApiError } from './errors';
import {
	AccountSchema,
	BookSchema,
	CandlesSchema,
	LevelQueueSchema,
	OpenOrderSchema,
	OpenOrdersSchema,
	OrderResponseSchema,
	TradeSchema,
	TradesPageSchema
} from './schemas';
import type { ExchangeApi, Id, ModifyRequest, NewOrderRequest, OpenOrder, Side } from './types';

type NativeCall = (method: string, payload: unknown) => Promise<unknown>;

declare global {
	interface Window {
		/** Bound by the C++ host. */
		ob_call?: NativeCall;
		/** Set by the UI; the C++ host calls it to push updates. */
		__obPush?: (msg: unknown) => void;
	}
}

export const hasNativeHost = () =>
	typeof window !== 'undefined' && typeof window.ob_call === 'function';

export const HelloSchema = z.object({
	/** False until makeClient() returns a client; the UI then keeps its simulator. */
	client: z.boolean(),
	name: z.string().optional(),
	version: z.string().optional()
});

const PushSchema = z.discriminatedUnion('type', [
	z.object({ type: z.literal('book'), data: BookSchema }),
	z.object({ type: z.literal('trades'), data: z.array(TradeSchema) }),
	z.object({ type: z.literal('orders'), data: OpenOrdersSchema }),
	z.object({ type: z.literal('account'), data: AccountSchema }),
	z.object({
		type: z.literal('status'),
		data: z.object({ connected: z.boolean(), message: z.string().optional() })
	})
]);

export type PushMessage = z.infer<typeof PushSchema>;

/** Route host pushes to `handler`, dropping (and logging) anything malformed. */
export function listenForPushes(handler: (msg: PushMessage) => void) {
	window.__obPush = (raw) => {
		const parsed = PushSchema.safeParse(raw);
		if (parsed.success) handler(parsed.data);
		else console.warn('Ignored malformed push from the C++ client', parsed.error.issues[0], raw);
	};
}

export class NativeExchangeApi implements ExchangeApi {
	readonly mode = 'native' as const;

	submitOrder(req: NewOrderRequest) {
		return this.call('submitOrder', OrderResponseSchema, req);
	}

	async cancelOrder(id: Id) {
		await this.call('cancelOrder', z.unknown(), { id });
	}

	modifyOrder(id: Id, patch: ModifyRequest): Promise<OpenOrder | null> {
		return this.call('modifyOrder', OpenOrderSchema.nullable(), { id, ...patch });
	}

	getBook(depth: number) {
		return this.call('getBook', BookSchema, { depth });
	}

	getLevel(side: Side, price: number) {
		return this.call('getLevel', LevelQueueSchema, { side, price });
	}

	getTrades(after?: number, limit = 100) {
		return this.call('getTrades', TradesPageSchema, { after: after ?? null, limit });
	}

	getOpenOrders() {
		return this.call('getOpenOrders', OpenOrdersSchema);
	}

	async getCandles(intervalSec: number, limit: number) {
		try {
			return await this.call('getCandles', CandlesSchema, { interval: intervalSec, limit });
		} catch (e) {
			// Optional in the client, like /candles over REST: the chart builds from trades.
			if (e instanceof ApiError && (e.status === 404 || e.status === 501)) return [];
			throw e;
		}
	}

	getAccount() {
		return this.call('getAccount', AccountSchema);
	}

	resetAccount() {
		return this.call('resetAccount', AccountSchema);
	}

	async health() {
		try {
			await this.call('health', z.unknown());
			return true;
		} catch {
			return false;
		}
	}

	private async call<T>(method: string, schema: z.ZodType<T, unknown>, payload: unknown = null): Promise<T> {
		let raw: unknown;
		try {
			raw = await window.ob_call!(method, payload);
		} catch (e) {
			const err = (e ?? {}) as { status?: unknown; message?: unknown };
			const status = typeof err.status === 'number' ? err.status : 500;
			const message = typeof err.message === 'string' ? err.message : String(e);
			throw new ApiError(status, message, e);
		}
		const parsed = schema.safeParse(raw);
		if (!parsed.success) {
			const issue = parsed.error.issues[0];
			throw new ApiError(
				500,
				`C++ client returned an unexpected shape for ${method} at "${issue?.path.join('.') || '(root)'}": ${issue?.message}`,
				raw
			);
		}
		return parsed.data;
	}
}
