<script lang="ts">
	import { Inbox, Play } from '@lucide/svelte';
	import { ORDER_TYPE_META } from '$lib/api/types';
	import { fmtPrice, fmtSize, fmtTime } from '$lib/format';
	import { market } from '$lib/stores/market.svelte';
	import { summarizeTrace } from '$lib/trace';
</script>

{#if market.traces.length}
	<table class="w-full min-w-[720px] text-[12px]">
		<thead class="sticky top-0 z-10 bg-panel">
			<tr class="label text-left [&>th]:h-8 [&>th]:px-3 [&>th]:font-normal">
				<th>Time</th><th>Order</th><th>Side</th><th>Type</th><th class="text-right!">Qty @ Price</th>
				<th>Result</th><th class="text-right!">Steps</th><th></th>
			</tr>
		</thead>
		<tbody>
			{#each market.traces as t (t.order_id)}
				{@const rejected = t.events.some((e) => e.type === 'REJECT')}
				<tr class="border-t border-line/60 hover:bg-panel-2/60 [&>td]:h-9 [&>td]:px-3">
					<td class="num text-muted">{fmtTime(t.ts)}</td>
					<td class="num text-dim">#{t.order_id}</td>
					<td class="font-medium {t.side === 'BUY' ? 'text-bid' : 'text-ask'}">{t.side === 'BUY' ? 'Buy' : 'Sell'}</td>
					<td class="text-muted">{ORDER_TYPE_META[t.order_type].label}</td>
					<td class="num text-right">{fmtSize(t.size)} @ {t.order_type === 'MARKET' ? 'MKT' : fmtPrice(t.price)}</td>
					<td class="max-w-[340px] truncate {rejected ? 'text-ask' : 'text-fg/90'}" title={summarizeTrace(t)}>
						{summarizeTrace(t)}
					</td>
					<td class="num text-right text-muted">{t.events.length}</td>
					<td class="text-right">
						<button class="chip gap-1 text-accent!" onclick={() => market.openReplay(t)}>
							<Play size={11} /> Replay
						</button>
					</td>
				</tr>
			{/each}
		</tbody>
	</table>
{:else}
	<div class="flex h-full flex-col items-center justify-center gap-1.5 p-6 text-center">
		<Inbox size={22} class="text-dim" />
		<div class="text-[12.5px] text-fg/80">No matches yet</div>
		<div class="max-w-sm text-[11.5px] text-dim">
			Every order you place records what the matching engine did with it. Replay any of them step by step.
		</div>
	</div>
{/if}
