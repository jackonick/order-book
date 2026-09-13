<script lang="ts">
	import { fmtCompact, fmtPrice, fmtSize } from '$lib/format';
	import { market } from '$lib/stores/market.svelte';

	const PAD = { l: 12, r: 62, t: 18, b: 26 };

	let w = $state(0);
	let h = $state(0);
	let hoverX = $state<number | null>(null);

	interface Pt {
		price: number;
		cum: number;
	}

	const chart = $derived.by(() => {
		const { bids, asks } = market.book;
		if (!bids.length || !asks.length || w < 80 || h < 80) return null;

		let c = 0;
		const b: Pt[] = bids.map((l) => ({ price: l.price, cum: (c += l.size) }));
		c = 0;
		const a: Pt[] = asks.map((l) => ({ price: l.price, cum: (c += l.size) }));

		const mid = (bids[0].price + asks[0].price) / 2;
		// Symmetric window around the mid, as wide as the shallower side reaches.
		const span = Math.max(4, Math.min(mid - b[b.length - 1].price, a[a.length - 1].price - mid));
		const lo = mid - span;
		const hi = mid + span;
		const vb = b.filter((p) => p.price >= lo);
		const va = a.filter((p) => p.price <= hi);
		const maxCum = Math.max(vb[vb.length - 1]?.cum ?? 0, va[va.length - 1]?.cum ?? 0, 1) * 1.1;

		const iw = w - PAD.l - PAD.r;
		const ih = h - PAD.t - PAD.b;
		const x = (p: number) => PAD.l + ((p - lo) / (hi - lo)) * iw;
		const y = (v: number) => PAD.t + ih - (v / maxCum) * ih;
		const base = y(0);

		// Step function: selling down to price p hits every bid at p or better, so the
		// height between two levels is the cumulative size up to the nearer one.
		let bidLine = `M${x(vb[0]?.price ?? mid)},${base}`;
		vb.forEach((p, i) => (bidLine += `V${y(p.cum)}H${x(vb[i + 1]?.price ?? lo)}`));
		let askLine = `M${x(va[0]?.price ?? mid)},${base}`;
		va.forEach((p, i) => (askLine += `V${y(p.cum)}H${x(va[i + 1]?.price ?? hi)}`));

		const xTicks = Array.from({ length: 5 }, (_, i) => lo + ((hi - lo) * i) / 4);
		const yTicks = Array.from({ length: 4 }, (_, i) => (maxCum / 1.1) * ((i + 1) / 4));

		return {
			b: vb,
			a: va,
			mid,
			lo,
			hi,
			x,
			y,
			base,
			iw,
			bidLine,
			askLine,
			bidArea: `${bidLine}V${base}Z`,
			askArea: `${askLine}V${base}Z`,
			xTicks,
			yTicks
		};
	});

	const hoverInfo = $derived.by(() => {
		if (!chart || hoverX === null) return null;
		const { lo, hi, iw, mid, b, a, x, y } = chart;
		const price = lo + ((hoverX - PAD.l) / iw) * (hi - lo);
		if (price < lo || price > hi) return null;
		let cum: number | null = null;
		if (price <= mid) {
			for (const p of b) if (p.price >= price) cum = p.cum;
		} else {
			for (const p of a) if (p.price <= price) cum = p.cum;
		}
		if (cum === null) return null;
		const side = price <= mid ? 'bid' : 'ask';
		return { price, cum, side, px: x(price), py: y(cum), bps: ((price - mid) / mid) * 1e4 };
	});

	function onMove(e: PointerEvent) {
		const r = (e.currentTarget as SVGElement).getBoundingClientRect();
		hoverX = e.clientX - r.left;
	}
</script>

<div class="absolute inset-0" bind:clientWidth={w} bind:clientHeight={h}>
	{#if chart}
		<svg
			width={w}
			height={h}
			class="block"
			role="img"
			aria-label="Order book depth chart"
			onpointermove={onMove}
			onpointerleave={() => (hoverX = null)}
		>
			<defs>
				<linearGradient id="depth-bid" x1="0" y1="0" x2="0" y2="1">
					<stop offset="0" stop-color="var(--color-bid)" stop-opacity="0.32" />
					<stop offset="1" stop-color="var(--color-bid)" stop-opacity="0.02" />
				</linearGradient>
				<linearGradient id="depth-ask" x1="0" y1="0" x2="0" y2="1">
					<stop offset="0" stop-color="var(--color-ask)" stop-opacity="0.32" />
					<stop offset="1" stop-color="var(--color-ask)" stop-opacity="0.02" />
				</linearGradient>
			</defs>

			{#each chart.yTicks as t (t)}
				<line x1={PAD.l} x2={w - PAD.r} y1={chart.y(t)} y2={chart.y(t)} stroke="var(--color-line)" stroke-dasharray="2 4" />
				<text x={w - PAD.r + 8} y={chart.y(t) + 3.5} class="num" font-size="10" fill="var(--color-muted)">
					{fmtCompact(t)}
				</text>
			{/each}

			<path d={chart.bidArea} fill="url(#depth-bid)" />
			<path d={chart.askArea} fill="url(#depth-ask)" />
			<path d={chart.bidLine} fill="none" stroke="var(--color-bid)" stroke-width="1.5" />
			<path d={chart.askLine} fill="none" stroke="var(--color-ask)" stroke-width="1.5" />

			<line x1={chart.x(chart.mid)} x2={chart.x(chart.mid)} y1={PAD.t} y2={chart.base} stroke="var(--color-dim)" stroke-dasharray="3 3" />
			<text x={chart.x(chart.mid)} y={PAD.t - 5} text-anchor="middle" class="num" font-size="10" fill="var(--color-muted)">
				mid {fmtPrice(chart.mid)}
			</text>

			<line x1={PAD.l} x2={w - PAD.r} y1={chart.base} y2={chart.base} stroke="var(--color-line-2)" />
			{#each chart.xTicks as t (t)}
				<text x={chart.x(t)} y={h - 8} text-anchor="middle" class="num" font-size="10" fill="var(--color-muted)">
					{fmtPrice(t)}
				</text>
			{/each}

			{#if hoverInfo}
				<line x1={hoverInfo.px} x2={hoverInfo.px} y1={PAD.t} y2={chart.base} stroke="var(--color-muted)" stroke-width="1" />
				<circle
					cx={hoverInfo.px}
					cy={hoverInfo.py}
					r="4"
					fill="var(--color-panel)"
					stroke={hoverInfo.side === 'bid' ? 'var(--color-bid)' : 'var(--color-ask)'}
					stroke-width="2"
				/>
			{/if}
		</svg>

		{#if hoverInfo}
			<div
				class="num pointer-events-none absolute z-10 rounded-md border border-line-2 bg-panel-2/95 px-2.5 py-1.5 text-[11px] shadow-lg"
				style:left="{Math.min(hoverInfo.px + 12, w - 170)}px"
				style:top="{Math.max(hoverInfo.py - 58, 6)}px"
			>
				<div class="flex justify-between gap-4"><span class="text-muted">Price</span>{fmtPrice(hoverInfo.price)}</div>
				<div class="flex justify-between gap-4">
					<span class="text-muted">Cum. size</span>
					<span class={hoverInfo.side === 'bid' ? 'text-bid' : 'text-ask'}>{fmtSize(hoverInfo.cum)}</span>
				</div>
				<div class="flex justify-between gap-4">
					<span class="text-muted">From mid</span>{hoverInfo.bps >= 0 ? '+' : ''}{hoverInfo.bps.toFixed(1)} bps
				</div>
			</div>
		{/if}
	{:else}
		<div class="grid h-full place-items-center text-[12px] text-dim">Waiting for both sides of the book…</div>
	{/if}
</div>
