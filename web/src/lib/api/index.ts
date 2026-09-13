import { API_MODE, API_URL } from '$lib/config';
import { backend } from './backend.svelte';
import { errorMessage } from './errors';
import { LoggingApi } from './logging';
import { MockExchangeApi } from './mock';
import { HelloSchema, NativeExchangeApi, hasNativeHost, listenForPushes, type PushMessage } from './native';
import { RestExchangeApi } from './rest';
import type { ExchangeApi } from './types';

const logged = new LoggingApi(
	API_MODE === 'rest' ? new RestExchangeApi(API_URL) : new MockExchangeApi()
);

/**
 * The one API instance the app uses. Switch backends with VITE_API_MODE (see .env.example),
 * or let the C++ desktop host pick one at startup; no component imports a backend directly.
 */
export const api: ExchangeApi = logged;

let pushHandler: ((msg: PushMessage) => void) | null = null;

/** The market store registers here to receive pushes from the C++ client. */
export function onPush(handler: (msg: PushMessage) => void) {
	pushHandler = handler;
}

/**
 * Inside the C++ desktop host, ask it whether a client is wired in (makeClient() returned
 * one). If so, switch every call to the native bridge and start accepting pushes.
 * In a plain browser this does nothing.
 */
export async function configureBackend() {
	if (!hasNativeHost()) return;
	backend.desktop = true;
	try {
		const hello = HelloSchema.parse(await window.ob_call!('hello', null));
		backend.clientName = hello.name ?? null;
		if (hello.client) {
			logged.swap(new NativeExchangeApi());
			backend.mode = 'native';
			backend.url = '';
			listenForPushes((msg) => pushHandler?.(msg));
			// Tell the host we're listening; it replays the latest book/orders/account/status
			// so pushes sent while the page was still loading aren't lost.
			await window.ob_call!('subscribe', null);
		}
	} catch (e) {
		backend.hostError = errorMessage(e);
	}
}

// Stop the mock's simulation timer when Vite hot-reloads this module.
if (import.meta.hot) import.meta.hot.dispose(() => api.dispose?.());

export type { PushMessage } from './native';
export * from './types';
