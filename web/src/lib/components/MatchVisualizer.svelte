<script lang="ts">
	import { ChevronLeft, ChevronRight, Pause, Play, RotateCcw, X } from '@lucide/svelte';
	import { ORDER_TYPE_META } from '$lib/api/types';
	import { INSTRUMENT, PRICE_SCALE } from '$lib/config';
	import { fmtPrice, fmtSize, ordinal } from '$lib/format';
	import { market } from '$lib/stores/market.svelte';
	import { describeEvent, introCaption, traceState } from '$lib/trace';

	const SPEEDS = [0.5, 1, 2, 4];
	/** Order blocks drawn per level before collapsing into "+N". */
	const MAX_BLOCKS = 14;

	const trace = $derived(market.replay);
	let step = $state(0);
	let playing = $state(true);
	let speed = $state(1);

	// A new trace starts from the top and autoplays.
	$effect(() => {
		if (trace) {
			step = 0;
			playing = true;
		}
	});

	const total = $derived(trace?.events.length ?? 0);
	const view = $derived(trace ? traceState(trace, step) : null);
	const isBuy = $derived(trace?.side === 'BUY');
	const caption = $derived(
		trace ? (step === 0 ? introCaption(trace) : describeEvent(trace.events[step - 1], trace)) : null
	);
	const avg = $derived(view && view.filled ? view.notional / view.filled : null);
	const remaining = $derived(trace && view ? Math.max(0, trace.size - view.filled - view.dropped) : 0);
	const done = $derived(step >= total);

	$effect(() => {
		if (!trace || !playing) return;
		if (step >= total) {
			playing = false;
			return;
		}
		const t = setTimeout(() => (step += 1), (step === 0 ? 1400 : 1100) / speed);
		return () => clearTimeout(t);
	});

	function go(to: number) {
		step = Math.max(0, Math.min(total, to));
		playing = false;
	}

	function toggle() {
		if (step >= total) step = 0;
		playing = !playing;
	}

	function onKey(e: KeyboardEvent) {
		if (!trace) return;
		if (e.key === 'Escape') market.closeReplay();
		else if (e.key === ' ') {
			e.preventDefault();
			toggle();
		} else if (e.key === 'ArrowRight') go(step + 1);
		else if (e.key === 'ArrowLeft') go(step - 1);
	}
</script>

<svelte:window onkeydown={onKey} />

{#if trace && view && caption}
	<!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
	<div
		class="fixed inset-0 z-50 grid place-items-center bg-black/60 p-4 backdrop-blur-sm"
		onclick={(e) => e.target === e.currentTarget && market.closeReplay()}
	>
		<div
			class="panel max-h-[90vh] w-full max-w-[1080px] shadow-2xl shadow-black/60"
			role="dialog"
			aria-modal="true"
			aria-label="Matching engine replay"
			style="animation: toast-in 0.2s ease-out"
		>
			<div class="panel-head h-12!">
				<span class="panel-title">Matching engine</span>
				<span class="num text-[12px] {isBuy ? 'text-bid' : 'text-ask'}">
					#{trace.order_id} · {trace.side} {ORDER_TYPE_META[trace.order_type].label}
				</span>
				<span class="num text-[12px] text-muted">
					{fmtSize(trace.size)} @ {trace.order_type === 'MARKET' ? 'market' : fmtPrice(trace.price)}
				</span>
				<button class="icon-btn ml-auto" aria-label="Close" onclick={() => market.closeReplay()}>
					<X size={15} />
				</button>
			</div>

			<!-- What the engine is doing right now -->
			<div class="border-b border-line bg-panel-2/40 px-4 py-3" aria-live="polite">
				<div class="text-[13.5px] font-medium">{caption.title}</div>
				<div class="mt-0.5 text-[12px] text-muted">{caption.detail}</div>
			</div>

			<div class="grid min-h-0 flex-1 gap-4 overflow-y-auto p-4 lg:grid-cols-[210px_minmax(0,1fr)_270px]">
				<!-- Incoming order -->
				<div class="flex flex-col gap-3">
					<div class="rounded-lg border border-line bg-panel-2/60 p-3">
						<div class="label">Incoming order</div>
						<div class="mt-1 text-[16px] font-semibold {isBuy ? 'text-bid' : 'text-ask'}">
							{trace.side} {fmtSize(trace.size)}
						</div>
						<div class="num text-[12px] text-muted">
							{ORDER_TYPE_META[trace.order_type].label}
							{trace.order_type === 'MARKET' ? '· any price' : `· limit ${fmtPrice(trace.price)}`}
						</div>
						<div class="mt-3 flex h-2 overflow-hidden rounded-full bg-panel-3">
							<div class="transition-[width] duration-500 {isBuy ? 'bg-bid' : 'bg-ask'}" style:width="{(view.filled / trace.size) * 100}%"></div>
							<div class="bg-dim transition-[width] duration-500" style:width="{(view.dropped / trace.size) * 100}%"></div>
						</div>
						<dl class="mt-3 grid grid-cols-2 gap-y-1.5 text-[12px]">
							<dt class="text-muted">Filled</dt>
							<dd class="num text-right">{fmtSize(view.filled)}</dd>
							<dt class="text-muted">Remaining</dt>
							<dd class="num text-right">{fmtSize(remaining)}</dd>
							<dt class="text-muted">Avg price</dt>
							<dd class="num text-right">{avg === null ? '—' : (avg / PRICE_SCALE).toFixed(INSTRUMENT.priceDecimals + 2)}</dd>
							{#if view.dropped}
								<dt class="text-muted">Dropped</dt>
								<dd class="num text-right">{fmtSize(view.dropped)}</dd>
							{/if}
						</dl>
					</div>

					{#if done}
						<div
							class="rounded-lg border px-3 py-2.5 text-[12px] {view.rejected
								? 'border-ask/40 bg-ask/10 text-ask'
								: 'border-bid/30 bg-bid/10 text-bid'}"
						>
							{#if view.rejected}
								Rejected. Nothing traded.
							{:else if view.rested}
								Resting {fmtSize(view.rested.size)} @ {fmtPrice(view.rested.price)}, {ordinal(view.rested.queue_position)} in queue.
							{:else if view.dropped}
								Done. {fmtSize(view.dropped)} dropped.
							{:else}
								Fully filled.
							{/if}
						</div>
					{/if}
				</div>

				<!-- The side of the book it walks -->
				<div class="min-w-0">
					<div class="label mb-2">
						{isBuy ? 'Asks' : 'Bids'} in reach · best price first (top) · oldest order first (left)
					</div>
					{#each view.levels as l (l.price)}
						<div class="flex items-center gap-2 py-1 transition-opacity duration-300" class:opacity-35={!l.reachable}>
							<div
								class="num w-[70px] shrink-0 text-right text-[12px] {isBuy ? 'text-ask' : 'text-bid'}"
								class:line-through={l.cleared}
							>
								{fmtPrice(l.price)}
							</div>
							<div class="flex h-10 min-w-0 flex-1 gap-1">
								{#if l.cleared}
									<div class="grid flex-1 place-items-center rounded-md border border-dashed border-line-2 text-[11px] text-dim">
										level cleared
									</div>
								{:else}
									{#each l.orders.slice(0, MAX_BLOCKS) as o (o.id)}
										<div
											class="blk {isBuy ? 'ask' : 'bid'} {o.state}"
											class:mine={o.mine}
											class:ice={o.iceberg}
											style:flex-grow={Math.max(o.size, o.took, 1)}
											title="#{o.id}{o.mine ? ' (yours)' : ''}{o.iceberg ? ' · iceberg' : ''}"
										>
											<span class="num">{fmtSize(o.size)}</span>
											{#if o.took}<span class="took num">−{fmtSize(o.took)}</span>{/if}
										</div>
									{/each}
									{#if l.orders.length > MAX_BLOCKS}
										<span class="shrink-0 self-center text-[11px] text-dim">+{l.orders.length - MAX_BLOCKS}</span>
									{/if}
									{#if !l.orders.length}
										<div class="grid flex-1 place-items-center text-[11px] text-dim">no orders</div>
									{/if}
								{/if}
							</div>
							{#if !l.reachable}<span class="w-16 shrink-0 text-[10px] text-dim">beyond limit</span>{/if}
						</div>
					{:else}
						<p class="py-6 text-center text-[12px] text-dim">
							The {isBuy ? 'ask' : 'bid'} side was empty, so there was nothing to match against.
						</p>
					{/each}

					{#if view.rested}
						<div class="mt-3 rounded-lg border border-accent/40 bg-accent/10 px-3 py-2 text-[12px]">
							<span class="text-accent">Your order now rests on the {isBuy ? 'bid' : 'ask'} side:</span>
							<span class="num">{fmtSize(view.rested.size)} @ {fmtPrice(view.rested.price)}</span>
							<span class="text-muted">· {ordinal(view.rested.queue_position)} in its queue (time priority)</span>
						</div>
					{/if}
				</div>

				<!-- Event log -->
				<div class="min-w-0">
					<div class="label mb-2">Engine events</div>
					<ol class="flex flex-col gap-0.5 text-[11.5px]">
						<li>
							<button class="ev" class:current={step === 0} onclick={() => go(0)}>
								<span class="num text-dim">0</span> Order arrives
							</button>
						</li>
						{#each trace.events as e, i (i)}
							<li>
								<button class="ev" class:current={i === step - 1} class:future={i >= step} onclick={() => go(i + 1)}>
									<span class="num text-dim">{i + 1}</span>
									<span class="truncate">{describeEvent(e, trace).title}</span>
								</button>
							</li>
						{/each}
					</ol>
				</div>
			</div>

			<div class="flex shrink-0 items-center gap-2 border-t border-line px-4 py-2.5">
				<button class="icon-btn" aria-label="Restart" onclick={() => go(0)}><RotateCcw size={14} /></button>
				<button class="icon-btn" aria-label="Previous step" disabled={step === 0} onclick={() => go(step - 1)}>
					<ChevronLeft size={16} />
				</button>
				<button
					class="grid size-8 place-items-center rounded-full bg-accent text-[#1a1200] transition hover:brightness-110"
					aria-label={playing ? 'Pause' : 'Play'}
					onclick={toggle}
				>
					{#if playing}<Pause size={14} />{:else}<Play size={14} />{/if}
				</button>
				<button class="icon-btn" aria-label="Next step" disabled={done} onclick={() => go(step + 1)}>
					<ChevronRight size={16} />
				</button>
				<div class="mx-2 h-1 flex-1 overflow-hidden rounded-full bg-panel-3">
					<div class="h-full bg-accent transition-[width] duration-300" style:width="{total ? (step / total) * 100 : 100}%"></div>
				</div>
				<span class="num text-[11px] text-muted">Step {step}/{total}</span>
				<div class="flex gap-0.5">
					{#each SPEEDS as s (s)}
						<button class="chip num" aria-pressed={speed === s} onclick={() => (speed = s)}>{s}×</button>
					{/each}
				</div>
				<span class="label hidden md:inline">Space · ← → · Esc</span>
			</div>
		</div>
	</div>
{/if}

<style>
	.blk {
		position: relative;
		display: grid;
		place-items: center;
		min-width: 42px;
		flex-basis: 0;
		border: 1px solid;
		border-radius: 6px;
		font-size: 11px;
		transition:
			flex-grow 0.45s ease,
			opacity 0.45s ease,
			transform 0.25s ease,
			background 0.25s ease;
	}
	.blk.ask {
		background: color-mix(in srgb, var(--color-ask) 16%, transparent);
		border-color: color-mix(in srgb, var(--color-ask) 35%, transparent);
	}
	.blk.bid {
		background: color-mix(in srgb, var(--color-bid) 16%, transparent);
		border-color: color-mix(in srgb, var(--color-bid) 35%, transparent);
	}
	.blk.ice {
		border-style: dashed;
	}
	.blk.mine {
		border-color: var(--color-accent);
		box-shadow: inset 0 0 0 1px var(--color-accent);
	}
	.blk.hit {
		background: color-mix(in srgb, var(--color-accent) 30%, transparent);
		border-color: var(--color-accent);
		transform: translateY(-2px);
	}
	.blk.filled {
		background: color-mix(in srgb, var(--color-accent) 30%, transparent);
		border-color: var(--color-accent);
		opacity: 0.45;
		text-decoration: line-through;
	}
	.blk.refilled {
		border-color: var(--color-info);
		box-shadow: 0 0 0 2px color-mix(in srgb, var(--color-info) 45%, transparent);
	}
	.took {
		position: absolute;
		top: -9px;
		right: 3px;
		padding: 0 4px;
		border-radius: 4px;
		background: var(--color-accent);
		color: #1a1200;
		font-size: 10px;
		font-weight: 600;
		text-decoration: none;
	}
	.ev {
		display: flex;
		width: 100%;
		gap: 0.5rem;
		padding: 0.25rem 0.5rem;
		border-radius: 6px;
		text-align: left;
		color: var(--color-fg);
		transition: background 120ms;
	}
	.ev:hover {
		background: var(--color-panel-2);
	}
	.ev.current {
		background: color-mix(in srgb, var(--color-accent) 14%, transparent);
		box-shadow: inset 2px 0 0 var(--color-accent);
	}
	.ev.future {
		color: var(--color-dim);
	}
</style>
