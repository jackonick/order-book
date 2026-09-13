<script lang="ts">
	import { INSTRUMENT } from '$lib/config';
	import { fmtPrice, fmtSize, fmtTime } from '$lib/format';
	import { market } from '$lib/stores/market.svelte';

	const mine = $derived(new Set(market.fills.map((f) => f.seq)));
	const shown = $derived(market.trades.slice(0, 80));
</script>

<div class="panel-head">
	<span class="panel-title">Market Trades</span>
	<span class="ml-auto label">{market.trades.length ? `${market.trades.length} recent` : ''}</span>
</div>

<div class="label grid h-7 shrink-0 grid-cols-3 items-center px-3">
	<span>Price ({INSTRUMENT.quote})</span>
	<span class="text-right">Size</span>
	<span class="text-right">Time</span>
</div>

<div class="min-h-0 flex-1 overflow-y-auto">
	{#each shown as t (t.seq)}
		<div
			class="trade grid h-5 grid-cols-3 items-center px-3 text-[12px]"
			class:bg-accent-soft={mine.has(t.seq)}
			title="{t.aggressor === 'BUY' ? 'Buyer' : 'Seller'} was the aggressor · #{t.seq}"
		>
			<span class="num {t.aggressor === 'BUY' ? 'text-bid' : 'text-ask'}">{fmtPrice(t.resting_price)}</span>
			<span class="num text-right text-fg/90">{fmtSize(t.trade_size)}</span>
			<span class="num text-right text-muted">{fmtTime(t.ts)}</span>
		</div>
	{:else}
		<div class="grid h-full place-items-center text-[12px] text-dim">No trades yet</div>
	{/each}
</div>

<style>
	.trade {
		animation: row-in 0.35s ease-out;
	}
	.bg-accent-soft {
		background: color-mix(in srgb, var(--color-accent) 9%, transparent);
		box-shadow: inset 2px 0 0 var(--color-accent);
	}
</style>
