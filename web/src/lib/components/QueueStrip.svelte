<script lang="ts">
	import { X } from '@lucide/svelte';
	import { fmtPrice, fmtSize, fmtTime, ordinal } from '$lib/format';
	import { market } from '$lib/stores/market.svelte';

	/** Blocks drawn before collapsing the rest into "+N". */
	const MAX = 30;

	const sel = $derived(market.selected);
	const q = $derived(market.level);
	const total = $derived(q ? q.orders.reduce((s, o) => s + o.size, 0) : 0);
	const mineIdx = $derived(q ? q.orders.findIndex((o) => o.mine) : -1);
	const ahead = $derived(
		q && mineIdx > 0 ? q.orders.slice(0, mineIdx).reduce((s, o) => s + o.size, 0) : 0
	);
	const shown = $derived(q ? q.orders.slice(0, MAX) : []);
</script>

{#if sel}
	<div class="shrink-0 border-t border-line bg-panel-2/40 px-3 pt-2 pb-2.5">
		<div class="flex items-center gap-2 text-[11px]">
			<span class="font-semibold">Queue</span>
			<span class="num {sel.side === 'BUY' ? 'text-bid' : 'text-ask'}">{fmtPrice(sel.price)}</span>
			<span class="text-muted">
				{q ? `${q.orders.length} order${q.orders.length === 1 ? '' : 's'} · ${fmtSize(total)}` : 'loading…'}
			</span>
			<button class="icon-btn ml-auto size-5!" aria-label="Close queue view" onclick={() => market.clearSelection()}>
				<X size={12} />
			</button>
		</div>

		{#if q && q.orders.length}
			<!-- One block per order, width proportional to size, in FIFO order. -->
			<div class="mt-1.5 flex h-6 gap-[2px]">
				{#each shown as o, i (o.id)}
					<div
						class="blk {sel.side === 'BUY' ? 'bid' : 'ask'}"
						class:mine={o.mine}
						class:ice={o.iceberg}
						style:flex-grow={Math.max(o.size, 1)}
						title="{ordinal(i + 1)} in queue · {fmtSize(o.size)}{o.iceberg ? ' visible (iceberg)' : ''} · {fmtTime(o.ts)}{o.mine ? ` · your #${o.id}` : ''}"
					>
						{#if total && o.size / total > 0.09}<span class="num">{fmtSize(o.size)}</span>{/if}
					</div>
				{/each}
				{#if q.orders.length > MAX}
					<span class="self-center pl-1 text-[10px] text-dim">+{q.orders.length - MAX}</span>
				{/if}
			</div>
			<div class="mt-1 flex justify-between gap-2 text-[10.5px] text-muted">
				<span>← front fills first</span>
				{#if mineIdx >= 0}
					<span class="text-accent">
						Your #{q.orders[mineIdx].id}: {ordinal(mineIdx + 1)} · {fmtSize(ahead)} ahead
					</span>
				{:else}
					<span>new orders join the back →</span>
				{/if}
			</div>
		{:else if q}
			<p class="mt-1.5 text-[11px] text-dim">Level is empty; its orders were filled or cancelled.</p>
		{/if}
	</div>
{/if}

<style>
	.blk {
		display: grid;
		place-items: center;
		min-width: 5px;
		flex-basis: 0;
		overflow: hidden;
		border: 1px solid transparent;
		border-radius: 3px;
		font-size: 9.5px;
		color: var(--color-fg);
		transition: flex-grow 0.3s ease;
	}
	.blk.bid {
		background: color-mix(in srgb, var(--color-bid) 28%, transparent);
	}
	.blk.ask {
		background: color-mix(in srgb, var(--color-ask) 28%, transparent);
	}
	.blk.ice {
		border: 1px dashed color-mix(in srgb, var(--color-fg) 45%, transparent);
	}
	.blk.mine {
		background: var(--color-accent);
		color: #1a1200;
		font-weight: 600;
	}
</style>
