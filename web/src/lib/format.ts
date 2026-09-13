import { INSTRUMENT, PRICE_SCALE } from './config';

const dp = INSTRUMENT.priceDecimals;

const priceFmt = new Intl.NumberFormat('en-US', {
	minimumFractionDigits: dp,
	maximumFractionDigits: dp
});
const sizeFmt = new Intl.NumberFormat('en-US', { maximumFractionDigits: 0 });
const compactFmt = new Intl.NumberFormat('en-US', {
	notation: 'compact',
	maximumFractionDigits: 2
});
const usdFmt = new Intl.NumberFormat('en-US', {
	style: 'currency',
	currency: 'USD',
	maximumFractionDigits: 2
});

export const ticksToPrice = (ticks: number) => ticks / PRICE_SCALE;
export const priceToTicks = (price: number) => Math.round(price * PRICE_SCALE);

/** True when a decimal price lands exactly on the tick grid (within float noise). */
export const onTick = (price: number) =>
	Math.abs(price * PRICE_SCALE - Math.round(price * PRICE_SCALE)) < 1e-6;

/** 10012 -> "100.12", with thousands separators. */
export const fmtPrice = (ticks: number | null | undefined) =>
	ticks == null ? '—' : priceFmt.format(ticks / PRICE_SCALE);

/** 10012 -> "100.12" with no separators, for filling inputs. */
export const fmtPlain = (ticks: number) => (ticks / PRICE_SCALE).toFixed(dp);

export const fmtSize = (n: number | null | undefined) => (n == null ? '—' : sizeFmt.format(n));
export const fmtCompact = (n: number | null | undefined) =>
	n == null ? '—' : compactFmt.format(n);
export const fmtUsd = (n: number | null | undefined) => (n == null ? '—' : usdFmt.format(n));

/** Quote amounts travel as price ticks x quantity (cents here); these print them as dollars. */
export const fmtQuote = (n: number | null | undefined) =>
	n == null ? '—' : usdFmt.format(n / PRICE_SCALE);
export const fmtSignedQuote = (n: number | null | undefined) =>
	n == null ? '—' : `${n >= 0 ? '+' : '−'}${usdFmt.format(Math.abs(n) / PRICE_SCALE)}`;

/** 1 -> "1st", 2 -> "2nd", 11 -> "11th", 23 -> "23rd". */
export const ordinal = (n: number) => {
	const s = ['th', 'st', 'nd', 'rd'];
	const v = n % 100;
	return n + (s[(v - 20) % 10] || s[v] || s[0]);
};

export const fmtPct =(x: number | null | undefined) =>
	x == null ? '—' : `${x >= 0 ? '+' : ''}${(x * 100).toFixed(2)}%`;

export const fmtBps = (x: number | null | undefined) => (x == null ? '—' : `${x.toFixed(1)} bps`);

export const fmtTime = (ms: number) =>
	new Date(ms).toLocaleTimeString('en-GB', { hour12: false });

export const fmtClock = (ms: number) => {
	const d = new Date(ms);
	return `${d.toLocaleTimeString('en-GB', { hour12: false })}.${String(d.getMilliseconds()).padStart(3, '0')}`;
};
