import { sveltekit } from '@sveltejs/kit/vite';
import tailwindcss from '@tailwindcss/vite';
import { defineConfig, loadEnv } from 'vite';

export default defineConfig(({ mode }) => {
	const env = loadEnv(mode, process.cwd(), '');

	return {
		plugins: [tailwindcss(), sveltekit()],
		server: {
			port: 5173,
			// The C++ desktop host can load the UI from here (OB_UI_URL=http://localhost:5173),
			// so fail loudly instead of silently moving to another port.
			strictPort: true,
			// In dev, /api/* is forwarded to the engine's HTTP server, so the browser
			// sees one origin and the C++ side never has to deal with CORS.
			proxy: {
				'/api': {
					target: env.API_PROXY_TARGET || 'http://localhost:8080',
					changeOrigin: true
				}
			}
		}
	};
});
