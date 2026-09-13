/*
 * Decorator that records every call into the API console panel. It wraps either backend,
 * so in mock mode you still see the exact method, path and JSON the REST client would send.
 */
import { apiLog } from '$lib/stores/apiLog.svelte';
import { ApiError, errorMessage } from './errors';
import { routes, type Route } from './routes';
import type { ExchangeApi, Id, ModifyRequest, NewOrderRequest, Side } from './types';

export class LoggingApi implements ExchangeApi {
	constructor(private inner: ExchangeApi) {}

	get mode() {
		return this.inner.mode;
	}

	/** Replace the backend at runtime (the C++ desktop host does this when a client is wired in). */
	swap(next: ExchangeApi) {
		this.inner.dispose?.();
		this.inner = next;
	}

	private async track<T>(route: Route, request: unknown, fn: () => Promise<T>): Promise<T> {
		if (route.poll && !apiLog.capturePolls) return fn();
		const t0 = performance.now();
		try {
			const response = await fn();
			apiLog.push({ route, request, response, ok: true, status: null, ms: performance.now() - t0 });
			return response;
		} catch (e) {
			apiLog.push({
				route,
				request,
				ok: false,
				status: e instanceof ApiError ? e.status : null,
				error: errorMessage(e),
				response: e instanceof ApiError ? e.body : undefined,
				ms: performance.now() - t0
			});
			throw e;
		}
	}

	submitOrder(req: NewOrderRequest) {
		return this.track(routes.submitOrder(), req, () => this.inner.submitOrder(req));
	}
	cancelOrder(id: Id) {
		return this.track(routes.cancelOrder(id), undefined, () => this.inner.cancelOrder(id));
	}
	modifyOrder(id: Id, patch: ModifyRequest) {
		return this.track(routes.modifyOrder(id), patch, () => this.inner.modifyOrder(id, patch));
	}
	getBook(depth: number) {
		return this.track(routes.getBook(depth), undefined, () => this.inner.getBook(depth));
	}
	getLevel(side: Side, price: number) {
		return this.track(routes.getLevel(side, price), undefined, () => this.inner.getLevel(side, price));
	}
	getAccount() {
		return this.track(routes.getAccount(), undefined, () => this.inner.getAccount());
	}
	resetAccount() {
		return this.track(routes.resetAccount(), undefined, () => this.inner.resetAccount());
	}
	getTrades(after?: number, limit?: number) {
		return this.track(routes.getTrades(after, limit), undefined, () => this.inner.getTrades(after, limit));
	}
	getOpenOrders() {
		return this.track(routes.getOpenOrders(), undefined, () => this.inner.getOpenOrders());
	}
	getCandles(intervalSec: number, limit: number) {
		return this.track(routes.getCandles(intervalSec, limit), undefined, () =>
			this.inner.getCandles(intervalSec, limit)
		);
	}
	health() {
		return this.track(routes.health(), undefined, () => this.inner.health());
	}
	dispose() {
		this.inner.dispose?.();
	}
}
