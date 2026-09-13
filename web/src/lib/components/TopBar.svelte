<script lang="ts">
	import { ArrowDownRight, ArrowUpRight } from '@lucide/svelte';
	import { backend } from '$lib/api/backend.svelte';
	import { INSTRUMENT, POLL_MS } from '$lib/config';
	import { fmtCompact, fmtPct, fmtPrice, fmtQuote, fmtSignedQuote } from '$lib/format';
	import { market } from '$lib/stores/market.svelte';

	const s = $derived(market.stats);
	const up = $derived((s?.change ?? 0) >= 0);
	const spreadBps = $derived(
		market.spread !== null && market.mid ? (market.spread / market.mid) * 1e4 : null
	);
</script>

<header class="panel h-full flex-row items-center gap-6 overflow-x-auto px-4 whitespace-nowrap">
	<div class="flex items-center gap-2.5">
		<svg viewBox="0 0 32 32" class="size-7 shrink-0" aria-hidden="true">
			<rect width="32" height="32" rx="8" fill="var(--color-panel-3)" />
			<rect x="7" y="7" width="8" height="3" rx="1.5" fill="var(--color-ask)" />
			<rect x="5" y="12" width="10" height="3" rx="1.5" fill="var(--color-ask)" opacity=".65" />
			<rect x="17" y="17" width="10" height="3" rx="1.5" fill="var(--color-bid)" opacity=".65" />
			<rect x="17" y="22" width="8" height="3" rx="1.5" fill="var(--color-bid)" />
		</svg>
		<span class="text-[15px] font-semibold tracking-tight">OrderBook</span>
		<span class="rounded bg-panel-3 px-1.5 py-0.5 text-[10px] font-semibold text-muted">V1</span>
	</div>

	<div class="h-7 w-px shrink-0 bg-line"></div>

	<div class="flex flex-col leading-tight">
		<span class="text-[14px] font-semibold">{INSTRUMENT.base}<span class="text-muted">/{INSTRUMENT.quote}</span></span>
		<span class="text-[11px] text-dim">{INSTRUMENT.name}</span>
	</div>

	<div
		class="num text-[20px] font-semibold transition-colors duration-300"
		class:text-bid={market.tick === 'up'}
		class:text-ask={market.tick === 'down'}
	>
		{fmtPrice(market.lastPrice)}
	</div>

	<dl class="flex items-center gap-6">
		<div class="flex flex-col">
			<dt class="label">24h Change</dt>
			<dd class="num flex items-center gap-1 text-[12.5px]" class:text-bid={up} class:text-ask={!up}>
				{#if s}
					{#if up}<ArrowUpRight size={13} />{:else}<ArrowDownRight size={13} />{/if}
					{fmtPrice(Math.abs((market.lastPrice ?? 0) - s.open))}
					<span class="opacity-80">{fmtPct(s.change)}</span>
				{:else}—{/if}
			</dd>
		</div>
		<div class="flex flex-col">
			<dt class="label">24h High</dt>
			<dd class="num text-[12.5px]">{fmtPrice(s?.high)}</dd>
		</div>
		<div class="flex flex-col">
			<dt class="label">24h Low</dt>
			<dd class="num text-[12.5px]">{fmtPrice(s?.low)}</dd>
		</div>
		<div class="flex flex-col">
			<dt class="label">24h Volume ({INSTRUMENT.base})</dt>
			<dd class="num text-[12.5px]">{fmtCompact(s?.volume)}</dd>
		</div>
		<div class="flex flex-col">
			<dt class="label">Spread</dt>
			<dd class="num text-[12.5px]">
				{fmtPrice(market.spread)}
				<span class="text-muted">{spreadBps === null ? '' : `${spreadBps.toFixed(1)} bps`}</span>
			</dd>
		</div>
	</dl>

	<div class="ml-auto flex items-center gap-2.5 pl-4">
		{#if market.acct}
			{@const a = market.acct}
			<div class="mr-2 flex flex-col items-end leading-tight" title="Cash + position marked at the last price">
				<span class="label">Equity</span>
				<span class="num text-[12.5px]">
					{fmtQuote(a.equity)}
					<span class={a.totalPnl >= 0 ? 'text-bid' : 'text-ask'}>{fmtSignedQuote(a.totalPnl)}</span>
				</span>
			</div>
		{/if}
		{#if backend.mode === 'mock'}
			<span
				class="rounded-md border border-accent/30 bg-accent/10 px-2 py-1 text-[10.5px] font-semibold tracking-wide text-accent"
				title={backend.desktop
					? `Desktop app: no C++ client wired in yet (makeClient() returns nullptr)${backend.hostError ? ` (${backend.hostError})` : ''}, so the built-in simulator is used.`
					: 'Simulated exchange running in your browser. Set VITE_API_MODE=rest to use the engine.'}
			>
				MOCK DATA
			</span>
		{:else if backend.mode === 'native'}
			<span
				class="rounded-md border border-info/30 bg-info/10 px-2 py-1 text-[10.5px] font-semibold tracking-wide text-info"
				title="Orders and market data go through your C++ client{backend.clientName ? ` (${backend.clientName})` : ''}"
			>
				C++ CLIENT
			</span>
		{:else}
			<span
				class="num rounded-md border border-info/30 bg-info/10 px-2 py-1 text-[10.5px] font-semibold text-info"
				title="REST client, polling every {POLL_MS} ms"
			>
				REST {backend.url}
			</span>
		{/if}

		<span class="flex items-center gap-2 rounded-md bg-panel-2 px-2.5 py-1 text-[11.5px]">
			<span
				class="size-2 rounded-full"
				class:bg-bid={market.status === 'online'}
				class:text-bid={market.status === 'online'}
				class:bg-accent={market.status === 'connecting'}
				class:bg-ask={market.status === 'offline'}
				style:animation={market.status === 'online' ? 'pulse-dot 2s infinite' : 'none'}
			></span>
			{#if market.status === 'online'}
				Live <span class="num text-muted">{market.latency ?? '–'} ms</span>
			{:else if market.status === 'connecting'}
				Connecting…
			{:else}
				Offline
			{/if}
		</span>
	</div>
</header>
