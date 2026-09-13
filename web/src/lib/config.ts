export const INSTRUMENT = {
	symbol: 'OBV1-USD',
	base: 'OBV1',
	quote: 'USD',
	name: 'Order Book V1',
	// The engine stores prices as uint64 ticks. 2 decimals => 1 tick = 0.01 USD,
	// so an engine price of 10012 is displayed as 100.12.
	priceDecimals: 2
} as const;

export const PRICE_SCALE = 10 ** INSTRUMENT.priceDecimals;

/** mock = in-browser simulator, rest = HTTP engine, native = the C++ desktop host's client. */
export type ApiMode = 'mock' | 'rest' | 'native';

export const API_MODE: ApiMode = import.meta.env.VITE_API_MODE === 'rest' ? 'rest' : 'mock';
export const API_URL = (import.meta.env.VITE_API_URL ?? '/api').replace(/\/$/, '');
export const POLL_MS = Math.max(100, Number(import.meta.env.VITE_POLL_MS) || 500);

/** Price levels requested per side on every book poll. */
export const BOOK_DEPTH = 50;
/** Candles requested for the price chart. */
export const CANDLE_LIMIT = 300;

export const TIMEFRAMES = [
	{ label: '1s', sec: 1 },
	{ label: '5s', sec: 5 },
	{ label: '1m', sec: 60 },
	{ label: '5m', sec: 300 },
	{ label: '15m', sec: 900 },
	{ label: '1h', sec: 3600 }
] as const;
