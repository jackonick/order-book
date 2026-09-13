<script lang="ts">
	import { INSTRUMENT, PRICE_SCALE } from '$lib/config';
	import { fmtPct, fmtPrice, fmtQuote, fmtSignedQuote, fmtSize } from '$lib/format';
	import { market } from '$lib/stores/market.svelte';

	const a = $derived(market.acct);
	const tone = (n: number) => (n > 0 ? 'text-bid' : n < 0 ? 'text-ask' : 'text-fg');
	const pct = (part: number, whole: number) => (whole > 0 ? Math.min(100, (part / whole) * 100) : 0);
</script>

{#snippet row(label: string, value: string, cls = '')}
	<div class="flex items-center justify-between gap-3">
		<span class="text-muted">{label}</span>
		<span class="num {cls}">{value}</span>
	</div>
{/snippet}

{#if a}
	<div class="grid gap-2 p-3 text-[12px] sm:grid-cols-2 2xl:grid-cols-4">
		<div class="card">
			<div class="label">Equity</div>
			<div class="num mt-1 text-[20px] font-semibold">{fmtQuote(a.equity)}</div>
			<div class="num mt-0.5 {tone(a.totalPnl)}">{fmtSignedQuote(a.totalPnl)} ({fmtPct(a.returnPct)})</div>
			<div class="mt-2 text-[11px] text-dim">Cash + position × mark ({fmtPrice(a.mark)})</div>
		</div>

		<div class="card flex flex-col gap-1.5">
			<div class="label">P&amp;L</div>
			{@render row('Unrealized', fmtSignedQuote(a.unrealized), tone(a.unrealized))}
			{@render row('Realized', fmtSignedQuote(a.realized_pnl), tone(a.realized_pnl))}
			{@render row('Avg cost', a.avg_cost === null ? 'flat' : (a.avg_cost / PRICE_SCALE).toFixed(INSTRUMENT.priceDecimals + 2))}
			{@render row('Mark', fmtPrice(a.mark))}
		</div>

		<div class="card flex flex-col gap-1.5">
			<div class="label">{INSTRUMENT.quote} balance</div>
			{@render row('Total', fmtQuote(a.cash))}
			{@render row('Available', fmtQuote(a.cashFree))}
			{@render row('In orders', fmtQuote(a.cash_reserved), a.cash_reserved ? 'text-accent' : '')}
			<div class="mt-1 h-1 overflow-hidden rounded-full bg-panel-3" title="Share locked by open buy orders">
				<div class="h-full bg-accent/70" style:width="{pct(a.cash_reserved, a.cash)}%"></div>
			</div>
		</div>

		<div class="card flex flex-col gap-1.5">
			<div class="label">{INSTRUMENT.base} position</div>
			{@render row('Total', fmtSize(a.position))}
			{@render row('Available', fmtSize(a.posFree))}
			{@render row('In orders', fmtSize(a.position_reserved), a.position_reserved ? 'text-accent' : '')}
			{@render row('Value', fmtQuote(a.posValue))}
		</div>
	</div>
	<p class="px-3 pb-3 text-[11px] text-dim">
		Spot account: buys are fully funded from free cash, sells from free position. No shorting or
		borrowing. P&amp;L uses average cost: buys blend into the average, sells realize (price − avg) × qty.
	</p>
{:else}
	<div class="grid h-full place-items-center p-6 text-center text-[12px] text-dim">
		{market.accountError ? `Account unavailable: ${market.accountError}` : 'Loading account…'}
	</div>
{/if}

<style>
	.card {
		border: 1px solid var(--color-line);
		border-radius: 8px;
		background: color-mix(in srgb, var(--color-panel-2) 45%, transparent);
		padding: 0.75rem;
	}
</style>
