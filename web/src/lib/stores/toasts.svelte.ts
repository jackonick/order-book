export interface Toast {
	id: number;
	kind: 'success' | 'error' | 'info';
	title: string;
	body?: string;
}

class Toasts {
	items = $state.raw<Toast[]>([]);
	private n = 0;

	push(t: Omit<Toast, 'id'>, ms = 4500) {
		const id = ++this.n;
		this.items = [...this.items, { ...t, id }].slice(-5);
		setTimeout(() => this.dismiss(id), ms);
	}

	dismiss(id: number) {
		this.items = this.items.filter((t) => t.id !== id);
	}
}

export const toasts = new Toasts();
