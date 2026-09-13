<script lang="ts">
	import { Check, Inbox, Pencil, X } from '@lucide/svelte';
	import { ORDER_TYPE_META, type ModifyRequest, type OpenOrder } from '$lib/api/types';
	import { backend } from '$lib/api/backend.svelte';
	import { PRICE_SCALE } from '$lib/config';
	import {
		fmtClock,
		fmtPlain,
		fmtPrice,
		fmtSize,
		fmtTime,
		fmtUsd,
		onTick,
		ordinal,
		priceToTicks
	} from '$lib/format';
	import { apiLog } from '$lib/stores/apiLog.svelte';
	import { market } from '$lib/stores/market.svelte';
	import { toasts } from '$lib/stores/toasts.svelte';
	import AccountSummary from './AccountSummary.svelte';
	import MatchHistory from './MatchHistory.svelte';

	type Tab = 'orders' | 'fills' | 'matches' | 'account' | 'api';

	let tab = $state<Tab>('orders');
	let editing = $state<string | null>(null);
	let editPrice = $state('');
	let editSize = $state('');
	let expanded = $state<number | null>(null);
	let confirmReset = $state(false);

	const METHOD_TONE: Record<string, string> = {
		GET: 'text-muted bg-panel-3',
		POST: 'text-bid bg-bid/10',
		PATCH: 'text-accent bg-accent/10',
		DELETE: 'text-ask bg-ask/10'
	};

	function startEdit(o: OpenOrder) {
		editing = o.id;
		editPrice = fmtPlain(o.price);
		editSize = String(o.visible_size);
	}

	async function saveEdit(o: OpenOrder) {
		const patch: ModifyRequest = {};
		// Validate only fields the user actually changed, and say so when one is bad:
		// silently skipping it would send a partial PATCH and report "Modified".
		if (editPrice.trim() !== fmtPlain(o.price)) {
			const p = Number(editPrice);
			if (!editPrice.trim() || !Number.isFinite(p) || p <= 0 || !onTick(p)) {
				toasts.push({ kind: 'error', title: 'Invalid price', body: `"${editPrice}" is not a positive multiple of ${fmtPlain(1)}` });
				return;
			}
			if (priceToTicks(p) !== o.price) patch.price = priceToTicks(p);
		}
		if (editSize.trim() !== String(o.visible_size)) {
			const s = Number(editSize);
			if (!editSize.trim() || !Number.isInteger(s) || s < 0) {
				toasts.push({ kind: 'error', title: 'Invalid size', body: `"${editSize}" is not a whole number (0 cancels the order)` });
				return;
			}
			if (s !== o.visible_size) patch.size = s;
		}
		if (!Object.keys(patch).length) {
			editing = null;
			return;
		}
		if (await market.modify(o.id, patch)) editing = null;
	}

	function onEditKey(e: KeyboardEvent, o: OpenOrder) {
		if (e.key === 'Enter') saveEdit(o);
		if (e.key === 'Escape') editing = null;
	}

	async function reset() {
		if (!confirmReset) {
			confirmReset = true;
			setTimeout(() => (confirmReset = false), 3000);
			return;
		}
		confirmReset = false;
		await market.resetAccount();
	}

	const json = (v: unknown) => (v === undefined ? '—' : JSON.stringify(v, null, 2));
</script>

<div class="panel-head gap-5">
	<div role="tablist" class="flex gap-4 self-stretch overflow-x-auto">
		<button role="tab" class="tab shrink-0" aria-selected={tab === 'orders'} onclick={() => (tab = 'orders')}>
			Open Orders <span class="num text-dim">({market.openOrders.length})</span>
		</button>
		<button role="tab" class="tab shrink-0" aria-selected={tab === 'fills'} onclick={() => (tab = 'fills')}>
			Fills <span class="num text-dim">({market.fills.length})</span>
		</button>
		<button role="tab" class="tab shrink-0" aria-selected={tab === 'matches'} onclick={() => (tab = 'matches')}>
			Matches <span class="num text-dim">({market.traces.length})</span>
		</button>
		<button role="tab" class="tab shrink-0" aria-selected={tab === 'account'} onclick={() => (tab = 'account')}>
			Account
		</button>
		<button role="tab" class="tab shrink-0" aria-selected={tab === 'api'} onclick={() => (tab = 'api')}>
			API Console
		</button>
	</div>

	<div class="ml-auto flex shrink-0 items-center gap-2">
		{#if tab === 'orders' && market.openOrders.length}
			<button class="chip text-ask! hover:bg-ask/10!" onclick={() => market.cancelAll()}>Cancel all</button>
		{:else if tab === 'matches'}
			<label class="flex cursor-pointer items-center gap-1.5 text-[11px] text-muted select-none">
				<input
					type="checkbox"
					class="accent-[var(--color-accent)]"
					checked={market.autoVisualize}
					onchange={(e) => market.setAutoVisualize(e.currentTarget.checked)}
				/>
				Open on submit
			</label>
		{:else if tab === 'account' && market.account}
			<button class="chip {confirmReset ? 'bg-ask/10! text-ask!' : ''}" onclick={reset}>
				{confirmReset ? 'Click again to reset' : 'Reset account'}
			</button>
		{:else if tab === 'api'}
			<span class="label hidden md:inline">
				{backend.mode === 'mock'
					? 'Mock mode: calls are simulated, shapes are real'
					: backend.mode === 'native'
						? 'Calls go to your C++ client (ob_call)'
						: `Sending to ${backend.url}`}
			</span>
			<label class="flex cursor-pointer items-center gap-1.5 text-[11px] text-muted select-none">
				<input type="checkbox" class="accent-[var(--color-accent)]" bind:checked={apiLog.capturePolls} />
				Log polling
			</label>
			<button class="chip" onclick={() => apiLog.clear()}>Clear</button>
		{/if}
	</div>
</div>

<div class="min-h-0 flex-1 overflow-auto">
	{#if tab === 'orders'}
		{#if market.openOrders.length}
			<table class="w-full min-w-[820px] text-[12px]">
				<thead class="sticky top-0 z-10 bg-panel">
					<tr class="label text-left [&>th]:h-8 [&>th]:px-3 [&>th]:font-normal">
						<th>Time</th><th>Side</th><th>Type</th><th class="text-right!">Price</th>
						<th class="text-right!">Remaining</th><th>Filled</th><th>Queue</th><th>ID</th><th class="text-right!">Actions</th>
					</tr>
				</thead>
				<tbody>
					{#each market.openOrders as o (o.id)}
						{@const pct = o.original_size ? (o.filled_size / o.original_size) * 100 : 0}
						<tr class="border-t border-line/60 hover:bg-panel-2/60 [&>td]:h-9 [&>td]:px-3">
							<td class="num text-muted">{fmtTime(o.ts)}</td>
							<td class="font-medium {o.side === 'BUY' ? 'text-bid' : 'text-ask'}">{o.side === 'BUY' ? 'Buy' : 'Sell'}</td>
							<td class="text-muted">{ORDER_TYPE_META[o.order_type].label}</td>
							{#if editing === o.id}
								<td class="text-right">
									<!-- svelte-ignore a11y_autofocus -->
									<input
										class="num h-7 w-24 rounded-md border border-line-2 bg-panel-2 px-2 text-right outline-none focus:border-accent/60"
										bind:value={editPrice}
										onkeydown={(e) => onEditKey(e, o)}
										onfocus={(e) => e.currentTarget.select()}
										autofocus
										aria-label="New price"
									/>
								</td>
								<td class="text-right">
									<input
										class="num h-7 w-20 rounded-md border border-line-2 bg-panel-2 px-2 text-right outline-none focus:border-accent/60"
										bind:value={editSize}
										onkeydown={(e) => onEditKey(e, o)}
										onfocus={(e) => e.currentTarget.select()}
										aria-label="New visible size"
									/>
								</td>
							{:else}
								<td class="num text-right">{fmtPrice(o.price)}</td>
								<td class="num text-right">
									{fmtSize(o.size)}
									{#if o.visible_size !== o.size}<span class="text-dim" title="visible slice">({fmtSize(o.visible_size)})</span>{/if}
								</td>
							{/if}
							<td>
								<div class="flex items-center gap-2">
									<div class="h-1 w-14 overflow-hidden rounded-full bg-panel-3">
										<div class="h-full {o.side === 'BUY' ? 'bg-bid' : 'bg-ask'}" style:width="{pct}%"></div>
									</div>
									<span class="num text-muted">{fmtSize(o.filled_size)}/{fmtSize(o.original_size)}</span>
								</div>
							</td>
							<td>
								{#if o.queue_position}
									<button
										class="num text-accent hover:underline"
										title="Show this level's queue in the order book"
										onclick={() => market.selectLevel(o.side, o.price)}
									>
										{ordinal(o.queue_position)} · {fmtSize(o.size_ahead ?? 0)} ahead
									</button>
								{:else}
									<span class="text-dim">—</span>
								{/if}
							</td>
							<td class="num text-dim">#{o.id}</td>
							<td>
								<div class="flex justify-end gap-0.5">
									{#if editing === o.id}
										<button class="icon-btn hover:text-bid!" aria-label="Save changes" onclick={() => saveEdit(o)}><Check size={14} /></button>
										<button class="icon-btn" aria-label="Discard changes" onclick={() => (editing = null)}><X size={14} /></button>
									{:else}
										<button class="icon-btn" aria-label="Modify order" title="Modify price / size" onclick={() => startEdit(o)}><Pencil size={13} /></button>
										<button class="icon-btn hover:text-ask!" aria-label="Cancel order" title="Cancel" onclick={() => market.cancel(o.id)}><X size={14} /></button>
									{/if}
								</div>
							</td>
						</tr>
					{/each}
				</tbody>
			</table>
		{:else}
			{@render empty('No open orders', 'Resting limit, post-only and iceberg orders show up here.')}
		{/if}
	{:else if tab === 'fills'}
		{#if market.fills.length}
			<table class="w-full min-w-[680px] text-[12px]">
				<thead class="sticky top-0 z-10 bg-panel">
					<tr class="label text-left [&>th]:h-8 [&>th]:px-3 [&>th]:font-normal">
						<th>Time</th><th>Side</th><th class="text-right!">Price</th><th class="text-right!">Size</th>
						<th class="text-right!">Value</th><th>Liquidity</th><th>Order</th><th>Trade</th>
					</tr>
				</thead>
				<tbody>
					{#each market.fills as f (f.seq)}
						<tr class="border-t border-line/60 hover:bg-panel-2/60 [&>td]:h-8 [&>td]:px-3">
							<td class="num text-muted">{fmtTime(f.ts)}</td>
							<td class="font-medium {f.side === 'BUY' ? 'text-bid' : 'text-ask'}">{f.side === 'BUY' ? 'Buy' : 'Sell'}</td>
							<td class="num text-right">{fmtPrice(f.price)}</td>
							<td class="num text-right">{fmtSize(f.size)}</td>
							<td class="num text-right text-muted">{fmtUsd((f.price * f.size) / PRICE_SCALE)}</td>
							<td>
								<span class="rounded px-1.5 py-0.5 text-[10.5px] font-medium {f.liquidity === 'MAKER' ? 'bg-info/10 text-info' : 'bg-panel-3 text-muted'}">
									{f.liquidity === 'MAKER' ? 'Maker' : 'Taker'}
								</span>
							</td>
							<td class="num text-dim">#{f.order_id}</td>
							<td class="num text-dim">{f.seq}</td>
						</tr>
					{/each}
				</tbody>
			</table>
		{:else}
			{@render empty('No fills yet', 'Executions against your orders will appear here.')}
		{/if}
	{:else if tab === 'matches'}
		<MatchHistory />
	{:else if tab === 'account'}
		<AccountSummary />
	{:else if apiLog.entries.length}
		<ul class="text-[12px]">
			{#each apiLog.entries as e (e.id)}
				<li class="border-b border-line/60">
					<button
						class="grid w-full grid-cols-[92px_62px_minmax(0,1fr)_64px_60px] items-center gap-2 px-3 py-1.5 text-left hover:bg-panel-2/60"
						onclick={() => (expanded = expanded === e.id ? null : e.id)}
						aria-expanded={expanded === e.id}
					>
						<span class="num text-dim">{fmtClock(e.ts)}</span>
						<span class="num w-fit rounded px-1.5 py-0.5 text-[10.5px] font-semibold {METHOD_TONE[e.route.method]}">{e.route.method}</span>
						<span class="num truncate">{backend.url}{e.route.path}</span>
						<span class="num text-right {e.ok ? 'text-bid' : 'text-ask'}">{e.ok ? 'OK' : (e.status ?? 'ERR')}</span>
						<span class="num text-right text-muted">{e.ms.toFixed(0)} ms</span>
					</button>
					{#if expanded === e.id}
						<div class="grid gap-2 px-3 pb-3 md:grid-cols-2">
							<div>
								<div class="label mb-1">Request body</div>
								<pre class="num max-h-56 overflow-auto rounded-md border border-line bg-bg p-2 text-[11px] text-muted">{json(e.request)}</pre>
							</div>
							<div>
								<div class="label mb-1">{e.ok ? 'Response' : 'Error'}</div>
								<pre class="num max-h-56 overflow-auto rounded-md border border-line bg-bg p-2 text-[11px] {e.ok ? 'text-muted' : 'text-ask'}">{e.ok ? json(e.response) : `${e.error}${e.response === undefined ? '' : '\n\n' + json(e.response)}`}</pre>
							</div>
						</div>
					{/if}
				</li>
			{/each}
		</ul>
	{:else}
		{@render empty('No requests logged', 'Place, modify or cancel an order to see the exact HTTP calls. Tick "Log polling" to include market-data polls.')}
	{/if}
</div>

{#snippet empty(title: string, body: string)}
	<div class="flex h-full flex-col items-center justify-center gap-1.5 p-6 text-center">
		<Inbox size={22} class="text-dim" />
		<div class="text-[12.5px] text-fg/80">{title}</div>
		<div class="max-w-sm text-[11.5px] text-dim">{body}</div>
	</div>
{/snippet}
