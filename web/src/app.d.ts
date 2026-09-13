// See https://svelte.dev/docs/kit/types#app.d.ts
declare global {
	namespace App {}

	interface ImportMetaEnv {
		readonly VITE_API_MODE?: 'mock' | 'rest';
		readonly VITE_API_URL?: string;
		readonly VITE_POLL_MS?: string;
	}
}

export {};
