<script lang="ts">
	import { TIMEFRAMES } from '$lib/config';
	import { market } from '$lib/stores/market.svelte';
	import DepthChart from './DepthChart.svelte';
	import PriceChart from './PriceChart.svelte';

	let view = $state<'price' | 'depth'>('price');
</script>

<div class="panel-head gap-5">
	<div role="tablist" class="flex gap-4 self-stretch">
		<button role="tab" class="tab" aria-selected={view === 'price'} onclick={() => (view = 'price')}>
			Price
		</button>
		<button role="tab" class="tab" aria-selected={view === 'depth'} onclick={() => (view = 'depth')}>
			Depth
		</button>
	</div>

	{#if view === 'price'}
		<div class="ml-auto flex items-center gap-0.5">
			{#each TIMEFRAMES as tf (tf.sec)}
				<button
					class="chip num"
					aria-pressed={market.interval === tf.sec}
					onclick={() => market.setInterval(tf.sec)}
				>
					{tf.label}
				</button>
			{/each}
		</div>
	{:else}
		<span class="ml-auto label">Cumulative visible size by price</span>
	{/if}
</div>

<div class="relative min-h-0 flex-1">
	<!-- Keep the price chart mounted so switching tabs doesn't rebuild it. -->
	<div class="absolute inset-0" class:invisible={view !== 'price'}>
		<PriceChart />
	</div>
	{#if view === 'depth'}
		<div class="absolute inset-0">
			<DepthChart />
		</div>
	{/if}
</div>
