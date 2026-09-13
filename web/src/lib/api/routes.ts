/*
 * Single source of truth for the HTTP routes. The REST client sends these, and the
 * logging wrapper uses the same table to show what the mock "would" have sent.
 * API.md documents each one.
 */
export type HttpMethod = 'GET' | 'POST' | 'PATCH' | 'DELETE';

export interface Route {
	method: HttpMethod;
	path: string;
	/** Fired by the polling loop; hidden from the API console unless asked for. */
	poll?: boolean;
}

function query(params: Record<string, number | string | undefined>) {
	const parts = Object.entries(params)
		.filter(([, v]) => v !== undefined)
		.map(([k, v]) => `${k}=${encodeURIComponent(String(v))}`);
	return parts.length ? `?${parts.join('&')}` : '';
}

export const routes = {
	health: (): Route => ({ method: 'GET', path: '/health' }),
	submitOrder: (): Route => ({ method: 'POST', path: '/orders' }),
	cancelOrder: (id: string): Route => ({ method: 'DELETE', path: `/orders/${encodeURIComponent(id)}` }),
	modifyOrder: (id: string): Route => ({ method: 'PATCH', path: `/orders/${encodeURIComponent(id)}` }),
	getOpenOrders: (): Route => ({ method: 'GET', path: '/orders', poll: true }),
	getBook: (depth: number): Route => ({ method: 'GET', path: `/book${query({ depth })}`, poll: true }),
	getLevel: (side: string, price: number): Route => ({
		method: 'GET',
		path: `/book/level${query({ side, price })}`,
		poll: true
	}),
	getAccount: (): Route => ({ method: 'GET', path: '/account', poll: true }),
	resetAccount: (): Route => ({ method: 'POST', path: '/account/reset' }),
	getTrades: (after?: number, limit?: number): Route => ({
		method: 'GET',
		path: `/trades${query({ after, limit })}`,
		poll: true
	}),
	getCandles: (interval: number, limit: number): Route => ({
		method: 'GET',
		path: `/candles${query({ interval, limit })}`
	})
};
