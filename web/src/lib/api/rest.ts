/*
 * RestExchangeApi: the real client. Every method is one HTTP call described in API.md.
 * Nothing here knows about Svelte; it's plain fetch + zod so it can be tested on its own.
 */
import { z } from 'zod';
import { ApiError } from './errors';
import { routes, type Route } from './routes';
import {
	AccountSchema,
	BookSchema,
	CandlesSchema,
	LevelQueueSchema,
	OpenOrderSchema,
	OpenOrdersSchema,
	OrderResponseSchema,
	TradesPageSchema
} from './schemas';
import type { ExchangeApi, Id, ModifyRequest, NewOrderRequest, OpenOrder, Side } from './types';

export class RestExchangeApi implements ExchangeApi {
	readonly mode = 'rest' as const;

	constructor(
		private readonly baseUrl: string,
		private readonly timeoutMs = 4000,
		/** Browser fetch by default; injectable for tests or another transport. */
		private readonly fetchImpl: (input: string, init?: RequestInit) => Promise<Response> = (
			input,
			init
		) => fetch(input, init)
	) {}

	submitOrder(req: NewOrderRequest) {
		return this.call(routes.submitOrder(), OrderResponseSchema, req);
	}

	async cancelOrder(id: Id) {
		await this.call(routes.cancelOrder(id), z.unknown());
	}

	modifyOrder(id: Id, patch: ModifyRequest): Promise<OpenOrder | null> {
		return this.call(routes.modifyOrder(id), OpenOrderSchema.nullable(), patch);
	}

	getBook(depth: number) {
		return this.call(routes.getBook(depth), BookSchema);
	}

	getLevel(side: Side, price: number) {
		return this.call(routes.getLevel(side, price), LevelQueueSchema);
	}

	getAccount() {
		return this.call(routes.getAccount(), AccountSchema);
	}

	resetAccount() {
		return this.call(routes.resetAccount(), AccountSchema);
	}

	getTrades(after?: number, limit = 100) {
		return this.call(routes.getTrades(after, limit), TradesPageSchema);
	}

	getOpenOrders() {
		return this.call(routes.getOpenOrders(), OpenOrdersSchema);
	}

	async getCandles(intervalSec: number, limit: number) {
		try {
			return await this.call(routes.getCandles(intervalSec, limit), CandlesSchema);
		} catch (e) {
			// /candles is optional: without it the chart just builds bars from live trades.
			if (e instanceof ApiError && e.status === 404) return [];
			throw e;
		}
	}

	async health() {
		try {
			await this.call(routes.health(), z.unknown());
			return true;
		} catch {
			return false;
		}
	}

	private async call<T>(route: Route, schema: z.ZodType<T, unknown>, body?: unknown): Promise<T> {
		const ctrl = new AbortController();
		const timer = setTimeout(() => ctrl.abort(), this.timeoutMs);
		let res: Response;
		try {
			res = await this.fetchImpl(this.baseUrl + route.path, {
				method: route.method,
				headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
				body: body === undefined ? undefined : JSON.stringify(body),
				signal: ctrl.signal
			});
		} catch (e) {
			const timedOut = e instanceof DOMException && e.name === 'AbortError';
			throw new ApiError(0, timedOut ? `Timed out after ${this.timeoutMs} ms` : 'Network error: server unreachable');
		} finally {
			clearTimeout(timer);
		}

		const text = await res.text();
		let json: unknown = null;
		if (text) {
			try {
				json = JSON.parse(text);
			} catch {
				throw new ApiError(res.status, `Response was not JSON: ${text.slice(0, 120)}`);
			}
		}

		if (!res.ok) {
			const msg =
				json && typeof json === 'object' && 'error' in json ? String(json.error) : res.statusText;
			throw new ApiError(res.status, msg || 'Request failed', json);
		}

		const parsed = schema.safeParse(json);
		if (!parsed.success) {
			const issue = parsed.error.issues[0];
			throw new ApiError(
				res.status,
				`Unexpected response shape at "${issue?.path.join('.') || '(root)'}": ${issue?.message}`,
				json
			);
		}
		return parsed.data;
	}
}
