export class ApiError extends Error {
	constructor(
		/** HTTP status, or 0 for network failures / timeouts. */
		readonly status: number,
		message: string,
		readonly body?: unknown
	) {
		super(message);
		this.name = 'ApiError';
	}
}

export function errorMessage(e: unknown): string {
	if (e instanceof ApiError) return e.status ? `${e.status} · ${e.message}` : e.message;
	if (e instanceof Error) return e.message;
	return String(e);
}
