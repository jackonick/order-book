/** Standard normal via Box–Muller: Z = sqrt(-2 ln U1) * cos(2 pi U2). */
export function gaussian() {
	const u1 = 1 - Math.random(); // (0, 1], keeps ln() finite
	const u2 = Math.random();
	return Math.sqrt(-2 * Math.log(u1)) * Math.cos(2 * Math.PI * u2);
}

/** Exponential with the given rate (mean 1/rate), by inverse CDF: X = -ln(U) / rate. */
export function exponential(rate: number) {
	return -Math.log(1 - Math.random()) / rate;
}

/** exp(mu + sigma Z): median e^mu, right-skewed like real order sizes. */
export function logNormal(mu: number, sigma: number) {
	return Math.exp(mu + sigma * gaussian());
}

/**
 * Poisson(lambda) by Knuth's method: multiply uniforms until the product drops below
 * e^-lambda. The count of events in one time step when arrivals happen at a constant rate.
 */
export function poisson(lambda: number) {
	const limit = Math.exp(-lambda);
	let k = 0;
	let p = 1;
	do {
		k++;
		p *= Math.random();
	} while (p > limit);
	return k - 1;
}

export const pick = <T>(xs: readonly T[]) => xs[Math.floor(Math.random() * xs.length)];
