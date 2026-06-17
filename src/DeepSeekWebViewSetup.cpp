#include "DeepSeekWebViewSetup.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/webview.h>

#if defined(__WXGTK__)
#include <dlfcn.h>

#include <glib-object.h>

namespace {

struct WebKitApi {
    void *(*get_settings)(void *) = nullptr;
    void (*set_user_agent)(void *, const char *) = nullptr;
    void (*set_enable_javascript)(void *, int) = nullptr;
    void (*set_enable_webgl)(void *, int) = nullptr;
    void (*set_enable_site_specific_quirks)(void *, int) = nullptr;
    void (*set_javascript_can_open_windows)(void *, int) = nullptr;
    void (*set_auto_load_images)(void *, int) = nullptr;
    void (*set_enable_page_cache)(void *, int) = nullptr;
    void (*permission_allow)(void *) = nullptr;

    bool load() {
        // Resolve symbols from the WebKit build wxWebView already loaded.
        // Do not dlopen another libwebkit2gtk version: mixing libsoup2 (4.0) and
        // libsoup3 (4.1) in one process aborts.
        auto resolve = [](const char *name) -> void * { return dlsym(RTLD_DEFAULT, name); };

        get_settings =
            reinterpret_cast<void *(*)(void *)>(resolve("webkit_web_view_get_settings"));
        set_user_agent = reinterpret_cast<void (*)(void *, const char *)>(
            resolve("webkit_settings_set_user_agent"));
        set_enable_javascript = reinterpret_cast<void (*)(void *, int)>(
            resolve("webkit_settings_set_enable_javascript"));
        set_enable_webgl =
            reinterpret_cast<void (*)(void *, int)>(resolve("webkit_settings_set_enable_webgl"));
        set_enable_site_specific_quirks = reinterpret_cast<void (*)(void *, int)>(
            resolve("webkit_settings_set_enable_site_specific_quirks"));
        set_javascript_can_open_windows = reinterpret_cast<void (*)(void *, int)>(
            resolve("webkit_settings_set_javascript_can_open_windows_automatically"));
        set_auto_load_images = reinterpret_cast<void (*)(void *, int)>(
            resolve("webkit_settings_set_auto_load_images"));
        set_enable_page_cache = reinterpret_cast<void (*)(void *, int)>(
            resolve("webkit_settings_set_enable_page_cache"));
        permission_allow =
            reinterpret_cast<void (*)(void *)>(resolve("webkit_permission_request_allow"));
        return get_settings != nullptr && set_user_agent != nullptr;
    }
};

WebKitApi &webkitApi() {
    static WebKitApi api;
    static const bool loaded = api.load();
    (void)loaded;
    return api;
}

extern "C" void onWebKitPermissionRequest(void *, void *request, gpointer) {
    WebKitApi &api = webkitApi();
    if (api.permission_allow != nullptr) {
        api.permission_allow(request);
    }
}

} // namespace
#endif

void configureDeepSeekWebCache() {
#if defined(__WXGTK__)
    using GetDefaultContextFn = void *(*)();
    using SetDiskCacheFn = void (*)(void *context, const char *directory);
    using SetCacheModelFn = void (*)(void *context, int model);

    auto *getDefault = reinterpret_cast<GetDefaultContextFn>(
        dlsym(RTLD_DEFAULT, "webkit_web_context_get_default"));
    auto *setDiskCache = reinterpret_cast<SetDiskCacheFn>(
        dlsym(RTLD_DEFAULT, "webkit_web_context_set_disk_cache_directory"));
    auto *setCacheModel = reinterpret_cast<SetCacheModelFn>(
        dlsym(RTLD_DEFAULT, "webkit_web_context_set_cache_model"));
    if (getDefault == nullptr || setDiskCache == nullptr) {
        return;
    }

    wxFileName cacheDir(wxStandardPaths::Get().GetUserConfigDir(), "oremind");
    cacheDir.AppendDir("deepseek-webkit");
    if (!cacheDir.DirExists()) {
        cacheDir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }

    void *context = getDefault();
    if (context == nullptr) {
        return;
    }
    setDiskCache(context, cacheDir.GetFullPath().utf8_str().data());
    if (setCacheModel != nullptr) {
        setCacheModel(context, 1); // WEBKIT_CACHE_MODEL_WEB_BROWSER
    }
#endif
}

void prepareDeepSeekWebViewEnvironment() {
#if defined(__WXGTK__)
    if (wxGetenv("WEBKIT_DISABLE_DMABUF_RENDERER") == nullptr) {
        wxSetEnv("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
    }
#endif
}

void applyDeepSeekWebViewSettings(wxWebView *webView) {
#if defined(__WXGTK__)
    if (webView == nullptr) {
        return;
    }

    void *native = webView->GetNativeBackend();
    if (native == nullptr) {
        return;
    }

    WebKitApi &api = webkitApi();
    if (api.get_settings == nullptr || api.set_user_agent == nullptr) {
        return;
    }

    void *settings = api.get_settings(native);
    if (settings == nullptr) {
        return;
    }

    static const char kChromeUserAgent[] =
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/131.0.0.0 Safari/537.36";
    api.set_user_agent(settings, kChromeUserAgent);
    if (api.set_enable_javascript != nullptr) {
        api.set_enable_javascript(settings, TRUE);
    }
    if (api.set_enable_webgl != nullptr) {
        api.set_enable_webgl(settings, TRUE);
    }
    if (api.set_enable_site_specific_quirks != nullptr) {
        api.set_enable_site_specific_quirks(settings, TRUE);
    }
    if (api.set_javascript_can_open_windows != nullptr) {
        api.set_javascript_can_open_windows(settings, TRUE);
    }
    if (api.set_auto_load_images != nullptr) {
        api.set_auto_load_images(settings, TRUE);
    }
    if (api.set_enable_page_cache != nullptr) {
        api.set_enable_page_cache(settings, TRUE);
    }

    configureDeepSeekWebCache();

    if (api.permission_allow != nullptr) {
        g_signal_connect(native, "permission-request", G_CALLBACK(onWebKitPermissionRequest),
                         nullptr);
    }
#endif
}
