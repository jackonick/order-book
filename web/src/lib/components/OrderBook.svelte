<script lang="ts">
	import { ArrowDown, ArrowUp } from '@lucide/svelte';
	import QueueStrip from './QueueStrip.svelte';
	import type { Side } from '$lib/api/types';
	import { cumulate, groupLevels, groupPrice, imbalance, microprice, type CumLevel } from '$lib/book';
	import { INSTRUMENT, PRICE_SCALE } from '$lib/config';
	import { fmtPlain, fmtPrice, fmtSize } from '$lib/format';
	import { market } from '$lib/stores/market.svelte';
	import { ticket } from '$lib/stores/ticket.svelte';

	const ROW_H = 20;
	const MID_H = 46;
	/** Grouping sizes in ticks. */
	const GROUPS = [1, 5, 10, 50];

	let group = $state(1);
	let view = $state<'both' | Side>('both');
	let listH = $state(0);

	const rows = $derived(
		Math.max(1, Math.floor((listH - MID_H) / ROW_H / (view === 'both' ? 2 : 1)))
	);
	const asks = $derived(
		view === 'BUY' ? [] : cumulate(groupLevels(market.book.asks, group, 'SELL').slice(0, rows))
	);
	const bids = $derived(
		view === 'SELL' ? [] : cumulate(groupLevels(market.book.bids, group, 'BUY').slice(0, rows))
	);
	const maxCum = $derived(
		Math.max(asks[asks.length - 1]?.cum ?? 0, bids[bids.length - 1]?.cum ?? 0, 1)
	);
	const mine = $derived(
		new Set(market.openOrders.map((o) => `${o.side}:${groupPrice(o.price, group, o.side)}`))
	);
	const imb = $derived(imbalance(market.book, 10));
	const bidPct = $derived(Math.round(((1 + imb) / 2) * 100));
	const micro = $derived(microprice(market.book));
	const spreadBps = $derived(
		market.spread !== null && market.mid ? (market.spread / market.mid) * 1e4 : null
	);

	// Flash a level when its size changes between polls.
	let prev = new Map<string, number>();
	let prevGroup = 1;
	let flashes = $state.raw(new Set<string>());
	$effect(() => {
		const book = market.book;
		const g = group;
		const next = new Map<string, number>();
		const changed = new Set<string>();
		const sides = [
			['BUY', groupLevels(book.bids, g, 'BUY')],
			['SELL', groupLevels(book.asks, g, 'SELL')]
		] as const;
		for (const [side, levels] of sides) {
			for (const l of levels) {
				const k = `${side}:${l.price}`;
				next.set(k, l.size);
				if (g === prevGroup && prev.size && prev.get(k) !== l.size) changed.add(k);
			}
		}
		prev = next;
		prevGroup = g;
		flashes = changed;
	});

	function pick(price: number, side: Side) {
		ticket.price = fmtPlain(price);
		if (ticket.type === 'MARKET') ticket.type = 'GTC';
		// A grouped row spans several real prices, so there's no single queue to show.
		if (group === 1) market.selectLevel(side, price);
	}
</script>

{#snippet row(l: CumLevel, side: Side)}
	{@const key = `${side}:${l.price}`}
	<button
		type="button"
		class="relative grid shrink-0 grid-cols-[1.1fr_1fr_1fr] items-center px-3 text-left text-[12px] hover:bg-panel-3/70"
		class:flash-bid={side === 'BUY' && flashes.has(key)}
		class:flash-ask={side === 'SELL' && flashes.has(key)}
		class:selected={group === 1 && market.selected?.side === side && market.selected.price === l.price}
		style:height="{ROW_H}px"
		title="{l.orders} order{l.orders === 1 ? '' : 's'} · click to use this price and inspect its queue"
		onclick={() => pick(l.price, side)}
	>
		<span
			class="absolute inset-y-px right-0 transition-[width] duration-200 {side === 'BUY' ? 'bg-bid/10' : 'bg-ask/10'}"
			style:width="{(l.cum / maxCum) * 100}%"
		></span>
		<span class="num relative flex items-center gap-1.5 {side === 'BUY' ? 'text-bid' : 'text-ask'}">
			{fmtPrice(l.price)}
			{#if mine.has(key)}
				<span class="size-1.5 rounded-full bg-accent" title="You have an order here"></span>
			{/if}
		</span>
		<span class="num relative text-right text-fg/90">{fmtSize(l.size)}</span>
		<span class="num relative text-right text-muted">{fmtSize(l.cum)}</span>
	</button>
{/snippet}

{#snippet viewIcon(kind: 'both' | Side)}
	<svg viewBox="0 0 14 14" class="size-3.5" aria-hidden="true">
		{#if kind !== 'BUY'}
			<rect x="1" y={kind === 'both' ? 1 : 1} width="12" height={kind === 'both' ? 5 : 12} rx="1" fill="var(--color-ask)" opacity=".85" />
		{/if}
		{#if kind !== 'SELL'}
			<rect x="1" y={kind === 'both' ? 8 : 1} width="12" height={kind === 'both' ? 5 : 12} rx="1" fill="var(--color-bid)" opacity=".85" />
		{/if}
	</svg>
{/snippet}

<div class="panel-head">
	<span class="panel-title">Order Book</span>
	<div class="ml-auto flex items-center gap-0.5">
		{#each ['both', 'BUY', 'SELL'] as const as v (v)}
			<button
				class="icon-btn"
				class:bg-panel-3={view === v}
				aria-pressed={view === v}
				aria-label={v === 'both' ? 'Show both sides' : v === 'BUY' ? 'Bids only' : 'Asks only'}
				onclick={() => (view = v)}
			>
				{@render viewIcon(v)}
			</button>
		{/each}
		<select
			bind:value={group}
			class="num ml-1.5 h-[26px] rounded-md border border-line bg-panel-2 px-1.5 text-[11px] text-fg outline-none"
			aria-label="Price grouping"
		>
			{#each GROUPS as g (g)}
				<option value={g}>{(g / PRICE_SCALE).toFixed(INSTRUMENT.priceDecimals)}</option>
			{/each}
		</select>
	</div>
</div>

<div class="label grid h-7 shrink-0 grid-cols-[1.1fr_1fr_1fr] items-center px-3">
	<span>Price ({INSTRUMENT.quote})</span>
	<span class="text-right">Size ({INSTRUMENT.base})</span>
	<span class="text-right">Total</span>
</div>

<div class="flex min-h-0 flex-1 flex-col" bind:clientHeight={listH}>
	{#if view !== 'BUY'}
		<!-- Asks come best-first; column-reverse puts the best ask next to the spread. -->
		<div class="flex min-h-0 flex-1 flex-col-reverse overflow-hidden">
			{#each asks as l (l.price)}
				{@render row(l, 'SELL')}
			{/each}
		</div>
	{/if}

	<div
		class="flex shrink-0 items-center justify-between border-y border-line bg-panel-2/60 px-3"
		style:height="{MID_H}px"
	>
		<div
			class="num flex items-center gap-1 text-[18px] font-semibold transition-colors duration-300"
			class:text-bid={market.tick === 'up'}
			class:text-ask={market.tick === 'down'}
		>
			{fmtPrice(market.lastPrice)}
			{#if market.tick === 'up'}<ArrowUp size={15} />{:else if market.tick === 'down'}<ArrowDown size={15} />{/if}
		</div>
		<div class="text-right text-[11px] leading-[1.35]">
			<div class="text-muted">
				Spread <span class="num text-fg">{fmtPrice(market.spread)}</span>
				<span class="num">{spreadBps === null ? '' : `(${spreadBps.toFixed(1)} bps)`}</span>
			</div>
			<div class="text-muted" title="Mid weighted by the opposite side's top-of-book size">
				Micro <span class="num text-fg">{micro === null ? '—' : (micro / PRICE_SCALE).toFixed(INSTRUMENT.priceDecimals + 1)}</span>
			</div>
		</div>
	</div>

	{#if view !== 'SELL'}
		<div class="flex min-h-0 flex-1 flex-col overflow-hidden">
			{#each bids as l (l.price)}
				{@render row(l, 'BUY')}
			{/each}
		</div>
	{/if}
</div>

<QueueStrip />

<div
	class="num flex h-8 shrink-0 items-center gap-2 border-t border-line px-3 text-[11px]"
	title="Size imbalance over the top 10 levels: (bids - asks) / (bids + asks)"
>
	<span class="text-bid">B {bidPct}%</span>
	<div class="flex h-1.5 flex-1 overflow-hidden rounded-full bg-panel-3">
		<div class="bg-bid/80 transition-[width] duration-300" style:width="{bidPct}%"></div>
		<div class="flex-1 bg-ask/80"></div>
	</div>
	<span class="text-ask">{100 - bidPct}% S</span>
</div>

<style>
	.flash-bid {
		animation: flash-bid 0.7s ease-out;
	}
	.flash-ask {
		animation: flash-ask 0.7s ease-out;
	}
	.selected {
		box-shadow: inset 0 0 0 1px color-mix(in srgb, var(--color-accent) 70%, transparent);
	}
</style>
