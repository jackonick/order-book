// OrderBook desktop host: a native window running the web UI (web/build), wired to the
// ExchangeClient from client/my_client.cpp through the Bridge.
//
// Environment:
//   OB_DEMO_CLIENT=1   use host/demo_client.cpp (fake market) instead of makeClient()
//   OB_UI_URL=<url>    load the UI from a dev server instead, e.g. http://localhost:5173
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "bridge.hpp"
#include "demo_client.hpp"
#include "exchange_client.hpp"
#include "webview/webview.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

/** The UI is served from https://<kUiHost>/ (a reserved .example name, never a real site). */
constexpr const wchar_t* kUiHost = L"orderbook.example";
constexpr const char* kUiEntry = "https://orderbook.example/index.html";

std::filesystem::path exeDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

/**
 * Serve `folder` at https://orderbook.example/ inside WebView2. Browsers won't load the UI's
 * JavaScript modules from file:// URLs, and this avoids running an HTTP server for static files.
 */
bool mapUiFolder(webview::webview& w, const std::filesystem::path& folder) {
#ifdef _WIN32
    auto controllerHandle = w.browser_controller();
    if (!controllerHandle.ok() || !controllerHandle.value()) return false;
    auto* controller = static_cast<ICoreWebView2Controller*>(controllerHandle.value());

    ICoreWebView2* core = nullptr;
    if (FAILED(controller->get_CoreWebView2(&core)) || !core) return false;
    ICoreWebView2_3* core3 = nullptr;
    const HRESULT qi = core->QueryInterface(IID_PPV_ARGS(&core3));
    core->Release();
    if (FAILED(qi) || !core3) return false;

    const HRESULT hr = core3->SetVirtualHostNameToFolderMapping(
        kUiHost, folder.wstring().c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    core3->Release();
    return SUCCEEDED(hr);
#else
    (void)w;
    (void)folder;
    return false;
#endif
}

void showError(webview::webview& w, const std::string& title, const std::string& detail) {
    w.set_html(
        "<body style=\"margin:0;padding:40px;background:#0a0d12;color:#e7ebf0;"
        "font:14px system-ui,sans-serif\"><h2 style=\"color:#f6465d\">" +
        title + "</h2><p>" + detail + "</p></body>");
}

}  // namespace

int main() {
    // Flush every write, so your client's std::cout shows up immediately (even when redirected).
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    try {
        // debug = true enables the WebView2 DevTools (right-click > Inspect).
        webview::webview w(true, nullptr);
        w.set_title("OrderBook");
        w.set_size(1100, 700, WEBVIEW_HINT_MIN);
        w.set_size(1600, 960, WEBVIEW_HINT_NONE);

        std::unique_ptr<ob::ExchangeClient> client;
        const char* demo = std::getenv("OB_DEMO_CLIENT");
        if (demo && std::string(demo) == "1") client = ob::makeDemoClient();
        else client = ob::makeClient();
        std::cout << "[orderbook-app] client: " << (client ? client->name() : "none, the UI uses its simulator") << "\n";

        ob::Bridge bridge(w, client.get());
        bridge.install();

        if (const char* url = std::getenv("OB_UI_URL")) {
            w.navigate(url);
        } else {
            const auto ui = exeDir() / "ui";
            if (!std::filesystem::exists(ui / "index.html")) {
                showError(w, "UI files not found",
                          "Expected " + ui.string() + "\\index.html. Run <code>npm run build</code> in web/, "
                          "then rebuild orderbook-app (the build copies web/build next to the exe).");
            } else if (!mapUiFolder(w, ui)) {
                showError(w, "Couldn't serve the UI", "WebView2 refused the folder mapping for " + ui.string() + ".");
            } else {
                w.navigate(kUiEntry);
            }
        }

        bridge.startClient();
        w.run();
        bridge.shutdown();
    } catch (const webview::exception& e) {
        std::cerr << "[orderbook-app] webview error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[orderbook-app] error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
