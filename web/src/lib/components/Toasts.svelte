<script lang="ts">
	import { CircleCheck, CircleX, Info, X } from '@lucide/svelte';
	import { toasts } from '$lib/stores/toasts.svelte';
</script>

<!-- Positioned by the page; this is just the stack. -->
<div class="flex w-full flex-col gap-2" aria-live="polite">
	{#each toasts.items as t (t.id)}
		<div
			class="pointer-events-auto flex items-start gap-2.5 rounded-lg border border-line-2 bg-panel-2/95 px-3 py-2.5 shadow-xl shadow-black/40 backdrop-blur"
			style="animation: toast-in 0.18s ease-out"
		>
			{#if t.kind === 'success'}
				<CircleCheck size={16} class="mt-px shrink-0 text-bid" />
			{:else if t.kind === 'error'}
				<CircleX size={16} class="mt-px shrink-0 text-ask" />
			{:else}
				<Info size={16} class="mt-px shrink-0 text-info" />
			{/if}
			<div class="min-w-0 flex-1">
				<div class="text-[12.5px] font-medium">{t.title}</div>
				{#if t.body}<div class="mt-0.5 text-[11.5px] leading-snug text-muted">{t.body}</div>{/if}
			</div>
			<button class="icon-btn -mt-1 -mr-1.5 size-6!" aria-label="Dismiss" onclick={() => toasts.dismiss(t.id)}>
				<X size={12} />
			</button>
		</div>
	{/each}
</div>
