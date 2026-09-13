<script lang="ts">
	import { ChevronDown, CircleAlert, Info, LoaderCircle, Minus, Plus } from '@lucide/svelte';
	import { routes } from '$lib/api/routes';
	import { ORDER_TYPES, ORDER_TYPE_META, type NewOrderRequest } from '$lib/api/types';
	import { estimateFill } from '$lib/book';
	import { backend } from '$lib/api/backend.svelte';
	import { INSTRUMENT, PRICE_SCALE } from '$lib/config';
	import { fmtBps, fmtPlain, fmtPrice, fmtQuote, fmtSize, fmtUsd, onTick, priceToTicks } from '$lib/format';
	import { market } from '$lib/stores/market.svelte';
	import { ticket } from '$lib/stores/ticket.svelte';

	const SIZE_PRESETS = [10, 50, 100, 500, 1000];

	let submitting = $state(false);
	let showPayload = $state(false);

	const meta = $derived(ORDER_TYPE_META[ticket.type]);
	const isBuy = $derived(ticket.side === 'BUY');
	const isIceberg = $derived(ticket.type === 'ICEBERG');

	const num = (s: string) => (s.trim() === '' ? null : Number(s));
	const priceNum = $derived(num(ticket.price));
	const sizeNum = $derived(num(ticket.size));
	const displayNum = $derived(num(ticket.display));

	const errors = $derived.by(() => {
		const e: Partial<Record<'price' | 'size' | 'display', string>> = {};
		if (meta.needsPrice && priceNum !== null) {
			if (!Number.isFinite(priceNum) || priceNum <= 0) e.price = 'Price must be above 0';
			else if (!onTick(priceNum)) e.price = `Price must be a multiple of ${fmtPlain(1)}`;
		}
		if (sizeNum !== null && (!Number.isInteger(sizeNum) || sizeNum <= 0)) {
			e.size = 'Size must be a whole number above 0';
		}
		if (isIceberg && displayNum !== null) {
			if (!Number.isInteger(displayNum) || displayNum <= 0) e.display = 'Display must be a whole number above 0';
			else if (sizeNum !== null && displayNum >= sizeNum) e.display = 'Display must be smaller than the total size';
		}
		return e;
	});

	const priceTicks = $derived(
		meta.needsPrice && priceNum !== null && !errors.price ? priceToTicks(priceNum) : null
	);
	const size = $derived(sizeNum !== null && !errors.size ? sizeNum : null);
	const display = $derived(isIceberg && displayNum !== null && !errors.display ? displayNum : null);

	/** Exactly what gets POSTed. Iceberg: size is the visible slice, reserve is the hidden rest. */
	const request = $derived.by((): NewOrderRequest | null => {
		if (size === null) return null;
		if (meta.needsPrice && priceTicks === null) return null;
		if (isIceberg && display === null) return null;
		return {
			side: ticket.side,
			order_type: ticket.type,
			price: priceTicks ?? 0,
			size: isIceberg ? display! : size,
			reserve: isIceberg ? size - display! : 0,
			display_size: isIceberg ? display! : 0
		};
	});

	const est = $derived(
		size !== null && (!meta.needsPrice || priceTicks !== null)
			? estimateFill(market.book, ticket.side, ticket.type, priceTicks, size, display ?? size)
			: null
	);
	const touch = $derived(isBuy ? market.bestAsk : market.bestBid);
	const orderValue = $derived(
		est ? (est.notionalTicks + est.rests * (priceTicks ?? 0)) / PRICE_SCALE : null
	);

	const acct = $derived(market.acct);

	/** Largest size free balance allows: position for sells, cash / price for limit buys, a book sweep for market buys. */
	const maxSize = $derived.by(() => {
		if (!acct) return null;
		if (!isBuy) return Math.max(0, acct.posFree);
		if (meta.needsPrice) return priceTicks ? Math.floor(acct.cashFree / priceTicks) : null;
		let cash = acct.cashFree;
		let qty = 0;
		for (const l of market.book.asks) {
			const take = Math.min(l.size, Math.floor(cash / l.price));
			qty += take;
			cash -= take * l.price;
			if (take < l.size) break;
		}
		return qty;
	});

	/** Worst-case funds the order locks: limit x size for buys (market: sweep cost), quantity for sells. */
	const required = $derived.by(() => {
		if (size === null) return null;
		if (!isBuy) return size;
		if (meta.needsPrice) return priceTicks === null ? null : priceTicks * size;
		return est?.notionalTicks ?? null;
	});
	const insufficient = $derived(
		acct !== null && required !== null && required > (isBuy ? acct.cashFree : acct.posFree)
	);

	function setPct(p: number) {
		if (maxSize) ticket.size = String(Math.max(1, Math.floor(maxSize * p)));
	}

	function setPrice(ticks: number | null) {
		if (ticks === null) return;
		if (!meta.needsPrice) ticket.type = 'GTC';
		ticket.price = fmtPlain(Math.round(ticks));
	}

	function stepPrice(dir: 1 | -1) {
		const base = priceTicks ?? market.lastPrice ?? market.mid;
		if (base !== null) setPrice(Math.max(1, Math.round(base) + dir));
	}

	function stepSize(dir: 1 | -1) {
		const next = Math.max(1, (size ?? 0) + dir * 10);
		ticket.size = String(next);
	}

	async function submit(e: SubmitEvent) {
		e.preventDefault();
		if (!request || submitting) return;
		submitting = true;
		await market.submit(request);
		submitting = false;
	}
</script>

{#snippet stat(label: string, value: string, tone = '')}
	<div class="flex items-center justify-between">
		<span class="text-muted">{label}</span>
		<span class="num {tone}">{value}</span>
	</div>
{/snippet}

<form class="flex h-full min-h-0 flex-col" onsubmit={submit} novalidate>
	<div class="panel-head">
		<span class="panel-title">Place Order</span>
		<span class="ml-auto label num">{INSTRUMENT.symbol}</span>
	</div>

	<div class="flex min-h-0 flex-1 flex-col gap-3.5 overflow-y-auto p-3">
		<!-- Side -->
		<div class="relative grid grid-cols-2 rounded-lg border border-line bg-panel-2 p-1">
			<span
				class="absolute inset-y-1 left-1 w-[calc(50%-4px)] rounded-md transition-transform duration-200 ease-out"
				class:bg-bid={isBuy}
				class:bg-ask={!isBuy}
				style:transform={isBuy ? 'none' : 'translateX(100%)'}
			></span>
			<button
				type="button"
				class="relative h-9 rounded-md text-[13px] font-semibold transition-colors"
				class:text-[#04150c]={isBuy}
				class:text-muted={!isBuy}
				onclick={() => (ticket.side = 'BUY')}
			>
				Buy
			</button>
			<button
				type="button"
				class="relative h-9 rounded-md text-[13px] font-semibold transition-colors"
				class:text-white={!isBuy}
				class:text-muted={isBuy}
				onclick={() => (ticket.side = 'SELL')}
			>
				Sell
			</button>
		</div>

		<!-- Order type -->
		<div>
			<div class="flex flex-wrap gap-1">
				{#each ORDER_TYPES as t (t)}
					<button type="button" class="chip" aria-pressed={ticket.type === t} onclick={() => (ticket.type = t)}>
						{ORDER_TYPE_META[t].label}
					</button>
				{/each}
			</div>
			<p class="mt-2 flex gap-1.5 text-[11.5px] leading-snug text-muted">
				<Info size={13} class="mt-px shrink-0 text-dim" />
				<span>{meta.help}</span>
			</p>
		</div>

		<!-- Price -->
		<div>
			<div class="mb-1.5 flex items-center justify-between">
				<label for="ticket-price" class="label">Price</label>
				<div class="flex gap-0.5">
					<button type="button" class="chip h-5! px-1.5!" onclick={() => setPrice(market.bestBid)}>Bid</button>
					<button type="button" class="chip h-5! px-1.5!" onclick={() => setPrice(market.mid)}>Mid</button>
					<button type="button" class="chip h-5! px-1.5!" onclick={() => setPrice(market.bestAsk)}>Ask</button>
				</div>
			</div>
			<div class="field" data-invalid={!!errors.price}>
				<button type="button" class="icon-btn ml-1.5" aria-label="Price down one tick" disabled={!meta.needsPrice} onclick={() => stepPrice(-1)}>
					<Minus size={14} />
				</button>
				{#if meta.needsPrice}
					<input id="ticket-price" type="text" inputmode="decimal" autocomplete="off" placeholder="0.00" bind:value={ticket.price} />
				{:else}
					<input id="ticket-price" type="text" disabled value="" placeholder="Best available" />
				{/if}
				<span class="px-2 text-[11px] text-dim">{INSTRUMENT.quote}</span>
				<button type="button" class="icon-btn mr-1.5" aria-label="Price up one tick" disabled={!meta.needsPrice} onclick={() => stepPrice(1)}>
					<Plus size={14} />
				</button>
			</div>
			{#if errors.price}<p class="mt-1 text-[11px] text-ask">{errors.price}</p>{/if}
		</div>

		<!-- Size -->
		<div>
			<div class="mb-1.5 flex items-center justify-between gap-2">
				<label for="ticket-size" class="label">{isIceberg ? 'Total size' : 'Size'}</label>
				{#if acct}
					<span class="label">
						Available
						<span class="num text-fg">
							{isBuy ? fmtQuote(acct.cashFree) : `${fmtSize(acct.posFree)} ${INSTRUMENT.base}`}
						</span>
					</span>
				{/if}
			</div>
			<div class="field" data-invalid={!!errors.size}>
				<button type="button" class="icon-btn ml-1.5" aria-label="Decrease size" onclick={() => stepSize(-1)}>
					<Minus size={14} />
				</button>
				<input id="ticket-size" type="text" inputmode="numeric" autocomplete="off" placeholder="0" bind:value={ticket.size} />
				<span class="px-2 text-[11px] text-dim">{INSTRUMENT.base}</span>
				<button type="button" class="icon-btn mr-1.5" aria-label="Increase size" onclick={() => stepSize(1)}>
					<Plus size={14} />
				</button>
			</div>
			{#if errors.size}<p class="mt-1 text-[11px] text-ask">{errors.size}</p>{/if}
			{#if acct}
				<div class="mt-1.5 grid grid-cols-4 gap-1">
					{#each [0.25, 0.5, 0.75, 1] as p (p)}
						<button
							type="button"
							class="num h-6 rounded-md border border-line text-[11px] text-muted transition-colors hover:border-line-2 hover:text-fg disabled:opacity-40"
							disabled={!maxSize}
							title={maxSize ? `${fmtSize(Math.max(1, Math.floor(maxSize * p)))} ${INSTRUMENT.base}` : 'Enter a price first'}
							onclick={() => setPct(p)}
						>
							{p * 100}%
						</button>
					{/each}
				</div>
			{:else}
				<div class="mt-1.5 grid grid-cols-5 gap-1">
					{#each SIZE_PRESETS as n (n)}
						<button
							type="button"
							class="num h-6 rounded-md border border-line text-[11px] text-muted transition-colors hover:border-line-2 hover:text-fg"
							class:border-line-2={size === n}
							class:text-fg={size === n}
							onclick={() => (ticket.size = String(n))}
						>
							{fmtSize(n)}
						</button>
					{/each}
				</div>
			{/if}
		</div>

		{#if isIceberg}
			<div>
				<label for="ticket-display" class="label mb-1.5 block">Display size</label>
				<div class="field" data-invalid={!!errors.display}>
					<input id="ticket-display" class="pr-2" type="text" inputmode="numeric" autocomplete="off" placeholder="0" bind:value={ticket.display} />
					<span class="px-3 text-[11px] text-dim">{INSTRUMENT.base}</span>
				</div>
				{#if errors.display}
					<p class="mt-1 text-[11px] text-ask">{errors.display}</p>
				{:else if size !== null && display !== null}
					<p class="mt-1 text-[11px] text-muted">
						Shows <span class="num text-fg">{fmtSize(display)}</span> at a time;
						<span class="num text-fg">{fmtSize(size - display)}</span> stays hidden in reserve.
					</p>
				{/if}
			</div>
		{/if}

		<!-- Estimate -->
		<div class="flex flex-col gap-1.5 rounded-lg border border-line bg-panel-2/50 p-3 text-[12px]">
			{#if est && size !== null}
				{@render stat('Est. immediate fill', `${fmtSize(est.filled)} / ${fmtSize(size)}`)}
				{@render stat(
					'Avg fill price',
					est.avgPrice === null ? '—' : (est.avgPrice / PRICE_SCALE).toFixed(INSTRUMENT.priceDecimals + 2)
				)}
				{@render stat(
					'Slippage vs touch',
					fmtBps(est.slippageBps),
					est.slippageBps !== null && est.slippageBps > 5 ? 'text-accent' : ''
				)}
				{@render stat('Rests on book', est.rests > 0 ? `${fmtSize(est.rests)} @ ${fmtPrice(priceTicks)}` : '—')}
				{@render stat('Order value', fmtUsd(orderValue))}

				{#if est.rejected === 'BOC_CROSSES'}
					<p class="mt-1 flex gap-1.5 text-accent">
						<CircleAlert size={13} class="mt-px shrink-0" />
						Crosses the best {isBuy ? 'ask' : 'bid'} ({fmtPrice(touch)}). A post-only order would be rejected.
					</p>
				{:else if est.rejected === 'FOK_SHORT'}
					<p class="mt-1 flex gap-1.5 text-accent">
						<CircleAlert size={13} class="mt-px shrink-0" />
						Not enough visible size at this price to fill {fmtSize(size)}. Fill-or-kill would be rejected.
					</p>
				{:else if est.dropped > 0}
					<p class="mt-1 flex gap-1.5 text-muted">
						<CircleAlert size={13} class="mt-px shrink-0" />
						{fmtSize(est.dropped)} would not fill and is dropped, not rested.
					</p>
				{/if}
				{#if insufficient && acct && required !== null}
					<p class="mt-1 flex gap-1.5 text-ask">
						<CircleAlert size={13} class="mt-px shrink-0" />
						{isBuy
							? `Needs ${fmtQuote(required)}; only ${fmtQuote(acct.cashFree)} available.`
							: `Needs ${fmtSize(required)} ${INSTRUMENT.base}; only ${fmtSize(acct.posFree)} available.`}
					</p>
				{/if}
			{:else}
				<p class="text-dim">Enter a {meta.needsPrice ? 'price and size' : 'size'} to preview the fill.</p>
			{/if}
		</div>
	</div>

	<div class="flex shrink-0 flex-col gap-2 border-t border-line p-3">
		<button
			type="submit"
			disabled={!request || submitting || insufficient}
			class="flex h-11 items-center justify-center gap-2 rounded-lg text-[14px] font-semibold transition-[filter,opacity] hover:brightness-110 disabled:opacity-40 disabled:hover:brightness-100 {isBuy ? 'bg-bid text-[#04150c]' : 'bg-ask text-white'}"
		>
			{#if submitting}<LoaderCircle size={16} class="animate-spin" />{/if}
			{insufficient
				? `Insufficient ${isBuy ? INSTRUMENT.quote : INSTRUMENT.base}`
				: `${isBuy ? 'Buy' : 'Sell'} ${INSTRUMENT.base}`}
		</button>

		<div class="flex items-center justify-between gap-2">
			<button
				type="button"
				class="flex items-center gap-1 text-[11px] text-muted hover:text-fg"
				onclick={() => (showPayload = !showPayload)}
			>
				<ChevronDown size={13} class="transition-transform {showPayload ? 'rotate-180' : ''}" />
				Request preview
			</button>
			<label class="flex cursor-pointer items-center gap-1.5 text-[11px] text-muted select-none" title="Replay what the matching engine did after each order">
				<input
					type="checkbox"
					class="accent-[var(--color-accent)]"
					checked={market.autoVisualize}
					onchange={(e) => market.setAutoVisualize(e.currentTarget.checked)}
				/>
				Visualize matching
			</label>
		</div>
		{#if showPayload}
			<pre class="num max-h-48 overflow-auto rounded-md border border-line bg-bg p-2.5 text-[11px] leading-relaxed text-muted">{#if backend.mode === 'native'}<span class="text-bid">ob_call</span><span class="text-fg">("submitOrder", …)</span>{:else}<span class="text-bid">{routes.submitOrder().method}</span> <span class="text-fg">{backend.url}{routes.submitOrder().path}</span>{/if}
{request ? JSON.stringify(request, null, 2) : '// fill in the form'}</pre>
		{/if}
	</div>
</form>
