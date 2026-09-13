import { API_MODE, API_URL, type ApiMode } from '$lib/config';

/**
 * Which backend the app is actually talking to. Starts from the build-time env
 * (VITE_API_MODE / VITE_API_URL); inside the C++ desktop host it switches to `native`
 * at startup when a client is wired in (see configureBackend in ./index.ts).
 */
export const backend = $state({
	mode: API_MODE as ApiMode,
	url: API_URL,
	/** Running inside the C++ desktop host (desktop/ in the repo). */
	desktop: false,
	/** Name the C++ client reported, e.g. "gRPC -> NYC". */
	clientName: null as string | null,
	hostError: null as string | null
});
