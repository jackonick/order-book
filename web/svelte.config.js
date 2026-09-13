import adapter from '@sveltejs/adapter-static';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

/** @type {import('@sveltejs/kit').Config} */
const config = {
	preprocess: vitePreprocess(),
	compilerOptions: { runes: true },
	kit: {
		// Pure client-side SPA: the build is static files that talk to the engine's REST API.
		adapter: adapter({ fallback: 'index.html' }),
		// Hash routing: the C++ host serves the build from a virtual folder where only
		// index.html exists, so routes must live after the '#', not in the path.
		router: { type: 'hash' }
	}
};

export default config;
