<script lang="ts">
	import { onMount } from 'svelte';
	import { WifiOff } from '@lucide/svelte';
	import AccountPanel from '$lib/components/AccountPanel.svelte';
	import ChartPanel from '$lib/components/ChartPanel.svelte';
	import MatchVisualizer from '$lib/components/MatchVisualizer.svelte';
	import OrderBook from '$lib/components/OrderBook.svelte';
	import OrderTicket from '$lib/components/OrderTicket.svelte';
	import RecentTrades from '$lib/components/RecentTrades.svelte';
	import Toasts from '$lib/components/Toasts.svelte';
	import TopBar from '$lib/components/TopBar.svelte';
	import { configureBackend } from '$lib/api';
	import { backend } from '$lib/api/backend.svelte';
	import { INSTRUMENT } from '$lib/config';
	import { fmtPrice } from '$lib/format';
	import { market } from '$lib/stores/market.svelte';

	onMount(() => {
		let cancelled = false;
		// In the desktop app, ask the Rust shell which engine to use before polling starts.
		void configureBackend().finally(() => {
			if (!cancelled) market.start();
		});
		return () => {
			cancelled = true;
			market.stop();
		};
	});

	const title = $derived(
		market.lastPrice == null
			? `${INSTRUMENT.symbol} · OrderBook`
			: `${fmtPrice(market.lastPrice)} · ${INSTRUMENT.symbol} · OrderBook`
	);
</script>

<svelte:head>
	<title>{title}</title>
</svelte:head>

<div
	class="flex min-h-dvh flex-col gap-1.5 p-1.5 xl:grid xl:h-dvh xl:min-h-0 xl:grid-cols-[minmax(0,1fr)_300px_340px] xl:grid-rows-[56px_minmax(0,1fr)_280px]"
>
	<div class="xl:col-span-3">
		<TopBar />
	</div>

	<section class="panel min-h-[440px] xl:col-start-1 xl:row-start-2 xl:min-h-0">
		<ChartPanel />
	</section>

	<section class="panel h-[600px] xl:col-start-2 xl:row-start-2 xl:h-auto">
		<OrderBook />
	</section>

	<section class="panel min-h-[620px] xl:col-start-3 xl:row-span-2 xl:row-start-2 xl:min-h-0">
		<OrderTicket />
	</section>

	<section class="panel h-[340px] xl:col-start-1 xl:row-start-3 xl:h-auto">
		<AccountPanel />
	</section>

	<section class="panel h-[340px] xl:col-start-2 xl:row-start-3 xl:h-auto">
		<RecentTrades />
	</section>
</div>

<!--
	Notifications sit top-center over the chart, the least interactive area. Bottom-right
	would cover the order ticket's submit button and swallow the next click.
-->
<div
	class="pointer-events-none fixed top-[70px] left-1/2 z-40 flex w-[min(420px,calc(100vw-2rem))] -translate-x-1/2 flex-col items-center gap-2"
>
	{#if market.status === 'offline'}
		<div
			class="pointer-events-auto flex w-full items-center gap-2.5 rounded-lg border border-ask/40 bg-panel-2/95 px-3.5 py-2 text-[12px] shadow-xl shadow-black/40 backdrop-blur"
			role="alert"
		>
			<WifiOff size={15} class="shrink-0 text-ask" />
			<span>
				{#if backend.mode === 'native'}
					Your C++ client is offline <span class="text-muted">({market.error})</span>. Retrying…
				{:else}
					Can't reach the engine at <span class="num text-fg">{backend.url}</span>
					<span class="text-muted">({market.error})</span>. Retrying… Set
					<span class="num text-accent">VITE_API_MODE=mock</span> for simulated data.
				{/if}
			</span>
		</div>
	{/if}

	<Toasts />
</div>

<MatchVisualizer />
