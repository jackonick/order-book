import type { OrderType, Side } from '$lib/api/types';

/**
 * Order-entry form state, shared so other panels can drive it
 * (clicking a price in the book fills in the ticket's price).
 * Values are the raw strings the user typed; the ticket parses and validates them.
 */
export const ticket = $state({
	side: 'BUY' as Side,
	type: 'GTC' as OrderType,
	price: '',
	size: '',
	display: ''
});
