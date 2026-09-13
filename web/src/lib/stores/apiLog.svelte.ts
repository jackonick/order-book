import type { Route } from '$lib/api/routes';

export interface ApiLogEntry {
	id: number;
	ts: number;
	route: Route;
	request?: unknown;
	response?: unknown;
	ok: boolean;
	status: number | null;
	error?: string;
	ms: number;
}

class ApiLog {
	entries = $state.raw<ApiLogEntry[]>([]);
	/** Polling calls fire several times a second, so they're off by default. */
	capturePolls = $state(false);
	private n = 0;

	push(e: Omit<ApiLogEntry, 'id' | 'ts'>) {
		this.entries = [{ ...e, id: ++this.n, ts: Date.now() }, ...this.entries].slice(0, 250);
	}

	clear() {
		this.entries = [];
	}
}

export const apiLog = new ApiLog();
