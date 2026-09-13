<script lang="ts">
	import { onMount } from 'svelte';
	import {
		CandlestickSeries,
		ColorType,
		CrosshairMode,
		HistogramSeries,
		LineStyle,
		createChart,
		type CandlestickData,
		type HistogramData,
		type IChartApi,
		type IPriceLine,
		type ISeriesApi,
		type UTCTimestamp
	} from 'lightweight-charts';
	import type { Candle } from '$lib/api/types';
	import { INSTRUMENT } from '$lib/config';
	import { fmtCompact, fmtPrice, ticksToPrice } from '$lib/format';
	import { market } from '$lib/stores/market.svelte';

	const BID = '#1fc77e';
	const ASK = '#f6465d';
	// lightweight-charts draws timestamps as UTC; shift so the axis reads local time.
	const TZ = -new Date().getTimezoneOffset() * 60;
	const VISIBLE_BARS = 140;

	let el: HTMLDivElement;
	let chart: IChartApi | undefined;
	let candles: ISeriesApi<'Candlestick'> | undefined;
	let volume: ISeriesApi<'Histogram'> | undefined;
	let renderedEpoch = -1;
	let lastTime = -Infinity;
	let lines: IPriceLine[] = [];
	let linesKey = '';
	let hover = $state<Candle | null>(null);

	const legend = $derived(hover ?? market.candles[market.candles.length - 1] ?? null);

	const toBar = (c: Candle): CandlestickData<UTCTimestamp> => ({
		time: (c.time + TZ) as UTCTimestamp,
		open: ticksToPrice(c.open),
		high: ticksToPrice(c.high),
		low: ticksToPrice(c.low),
		close: ticksToPrice(c.close)
	});
	const toVol = (c: Candle): HistogramData<UTCTimestamp> => ({
		time: (c.time + TZ) as UTCTimestamp,
		value: c.volume,
		color: c.close >= c.open ? 'rgba(31,199,126,0.32)' : 'rgba(246,70,93,0.32)'
	});

	onMount(() => {
		const c = createChart(el, {
			autoSize: true,
			layout: {
				background: { type: ColorType.Solid, color: 'transparent' },
				textColor: '#8591a3',
				fontSize: 11,
				fontFamily: "'JetBrains Mono Variable', ui-monospace, monospace"
			},
			grid: {
				vertLines: { color: 'rgba(43,52,66,0.35)' },
				horzLines: { color: 'rgba(43,52,66,0.35)' }
			},
			rightPriceScale: { borderColor: '#1f2630' },
			timeScale: {
				borderColor: '#1f2630',
				timeVisible: true,
				secondsVisible: true,
				rightOffset: 6,
				barSpacing: 8
			},
			crosshair: {
				mode: CrosshairMode.Normal,
				vertLine: { color: '#566175', labelBackgroundColor: '#1c2230' },
				horzLine: { color: '#566175', labelBackgroundColor: '#1c2230' }
			}
		});

		candles = c.addSeries(CandlestickSeries, {
			upColor: BID,
			downColor: ASK,
			wickUpColor: BID,
			wickDownColor: ASK,
			borderVisible: false,
			priceFormat: {
				type: 'price',
				precision: INSTRUMENT.priceDecimals,
				minMove: 1 / 10 ** INSTRUMENT.priceDecimals
			}
		});
		candles.priceScale().applyOptions({ scaleMargins: { top: 0.08, bottom: 0.24 } });

		volume = c.addSeries(HistogramSeries, {
			priceScaleId: 'vol',
			priceFormat: { type: 'volume' },
			lastValueVisible: false,
			priceLineVisible: false
		});
		c.priceScale('vol').applyOptions({ scaleMargins: { top: 0.82, bottom: 0 } });

		c.subscribeCrosshairMove((p) => {
			if (p.time === undefined) {
				hover = null;
				return;
			}
			const t = (p.time as number) - TZ;
			hover = market.candles.find((b) => b.time === t) ?? null;
		});

		chart = c;
		return () => {
			c.remove();
			chart = candles = volume = undefined;
		};
	});

	// Candles: full reset when the store replaces the series, incremental update otherwise.
	$effect(() => {
		const bars = market.candles;
		const epoch = market.candleEpoch;
		const sec = market.interval;
		if (!chart || !candles || !volume) return;

		if (epoch !== renderedEpoch) {
			candles.setData(bars.map(toBar));
			volume.setData(bars.map(toVol));
			chart.timeScale().applyOptions({ secondsVisible: sec < 60 });
			if (bars.length) {
				chart.timeScale().setVisibleLogicalRange({
					from: Math.max(0, bars.length - VISIBLE_BARS),
					to: bars.length + 5
				});
			}
			renderedEpoch = epoch;
			lastTime = bars[bars.length - 1]?.time ?? -Infinity;
			return;
		}

		// Only the tail can change: re-send every bar at or after the last one drawn.
		let i = bars.length - 1;
		while (i > 0 && bars[i - 1].time >= lastTime) i--;
		for (; i < bars.length; i++) {
			if (bars[i].time < lastTime) continue;
			candles.update(toBar(bars[i]));
			volume.update(toVol(bars[i]));
		}
		lastTime = bars[bars.length - 1]?.time ?? lastTime;
	});

	// A dashed line for each of your resting orders.
	$effect(() => {
		const orders = market.openOrders;
		if (!candles) return;
		const key = orders.map((o) => `${o.id}:${o.price}:${o.size}`).join('|');
		if (key === linesKey) return;
		linesKey = key;
		for (const l of lines) candles.removePriceLine(l);
		lines = orders.map((o) =>
			candles!.createPriceLine({
				price: ticksToPrice(o.price),
				color: o.side === 'BUY' ? BID : ASK,
				lineWidth: 1,
				lineStyle: LineStyle.Dashed,
				axisLabelVisible: true,
				title: `${o.side === 'BUY' ? 'BUY' : 'SELL'} ${o.size}`
			})
		);
	});
</script>

<div class="absolute inset-0" bind:this={el}></div>

{#if legend}
	{@const up = legend.close >= legend.open}
	<div
		class="num pointer-events-none absolute top-2 left-3 z-10 flex gap-3 text-[11px] text-muted"
	>
		<span>O <span class={up ? 'text-bid' : 'text-ask'}>{fmtPrice(legend.open)}</span></span>
		<span>H <span class={up ? 'text-bid' : 'text-ask'}>{fmtPrice(legend.high)}</span></span>
		<span>L <span class={up ? 'text-bid' : 'text-ask'}>{fmtPrice(legend.low)}</span></span>
		<span>C <span class={up ? 'text-bid' : 'text-ask'}>{fmtPrice(legend.close)}</span></span>
		<span>V <span class="text-fg">{fmtCompact(legend.volume)}</span></span>
	</div>
{/if}

{#if !market.candles.length}
	<div class="absolute inset-0 grid place-items-center text-[12px] text-dim">
		Waiting for trades…
	</div>
{/if}
