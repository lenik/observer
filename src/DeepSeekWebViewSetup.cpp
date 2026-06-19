#include "DeepSeekWebViewSetup.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#if defined(__WXGTK__)
#include <dlfcn.h>

#include <glib-object.h>

namespace {

constexpr int kWebKitLoadFinished = 3;

void ensureWebKitLoaded() {
    static bool loaded = false;
    if (loaded) {
        return;
    }
    loaded = true;
    if (dlopen("libwebkit2gtk-4.1.so.0", RTLD_LAZY | RTLD_NOLOAD) == nullptr &&
        dlopen("libwebkit2gtk-4.1.so.0", RTLD_LAZY | RTLD_GLOBAL) == nullptr) {
        dlopen("libwebkit2gtk-4.0.so.37", RTLD_LAZY | RTLD_GLOBAL);
    }
    dlopen("libjavascriptcoregtk-4.1.so.0", RTLD_LAZY | RTLD_GLOBAL);
}

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

wxString deepSeekWebKitCachePaths(wxString *dataPathOut, wxString *cachePathOut) {
    wxFileName cacheRoot;
    cacheRoot.AssignDir(wxStandardPaths::Get().GetUserConfigDir());
    cacheRoot.AppendDir("oremind");
    cacheRoot.AppendDir("deepseek-webkit");
    cacheRoot.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    wxFileName dataDir = cacheRoot;
    dataDir.AppendDir("data");
    dataDir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    wxFileName cacheDir = cacheRoot;
    cacheDir.AppendDir("cache");
    cacheDir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    if (dataPathOut != nullptr) {
        *dataPathOut = dataDir.GetPath();
    }
    if (cachePathOut != nullptr) {
        *cachePathOut = cacheDir.GetPath();
    }
    return cacheRoot.GetPath();
}

void *getDeepSeekWebKitContext() {
    static void *context = nullptr;
    if (context != nullptr) {
        return context;
    }

    ensureWebKitLoaded();

    using GetTypeFn = unsigned long (*)();
    using ContextNewWithManagerFn = void *(*)(void *manager);
    using SetCacheModelFn = void (*)(void *context, int model);

    auto *getManagerType = reinterpret_cast<GetTypeFn>(
        dlsym(RTLD_DEFAULT, "webkit_website_data_manager_get_type"));
    auto *contextNewWithManager = reinterpret_cast<ContextNewWithManagerFn>(
        dlsym(RTLD_DEFAULT, "webkit_web_context_new_with_website_data_manager"));
    auto *setCacheModel = reinterpret_cast<SetCacheModelFn>(
        dlsym(RTLD_DEFAULT, "webkit_web_context_set_cache_model"));
    if (getManagerType == nullptr || contextNewWithManager == nullptr) {
        return nullptr;
    }

    wxString dataPath;
    wxString cachePath;
    deepSeekWebKitCachePaths(&dataPath, &cachePath);

    void *manager = g_object_new(getManagerType(), "base-data-directory",
                                 dataPath.utf8_str().data(), "base-cache-directory",
                                 cachePath.utf8_str().data(), nullptr);
    if (manager == nullptr) {
        return nullptr;
    }

    context = contextNewWithManager(manager);
    g_object_unref(manager);
    if (context == nullptr) {
        return nullptr;
    }
    if (setCacheModel != nullptr) {
        setCacheModel(context, 1); // WEBKIT_CACHE_MODEL_WEB_BROWSER
    }
    return context;
}

struct ScriptRunState {
    bool done = false;
    bool ok = false;
    wxString result;
};

extern "C" void onRunJavascriptFinished(void *source, void *asyncResult, void *userData) {
    auto *state = static_cast<ScriptRunState *>(userData);
    using FinishFn = void *(*)(void *webView, void *result, void **error);
    using GetJsValueFn = void *(*)(void *javascriptResult);
    using UnrefResultFn = void (*)(void *javascriptResult);
    using ValueToStringFn = char *(*)(void *value);

    auto *finish = reinterpret_cast<FinishFn>(dlsym(RTLD_DEFAULT, "webkit_web_view_run_javascript_finish"));
    auto *getJsValue =
        reinterpret_cast<GetJsValueFn>(dlsym(RTLD_DEFAULT, "webkit_javascript_result_get_js_value"));
    auto *unrefResult =
        reinterpret_cast<UnrefResultFn>(dlsym(RTLD_DEFAULT, "webkit_javascript_result_unref"));
    auto *valueToString =
        reinterpret_cast<ValueToStringFn>(dlsym(RTLD_DEFAULT, "jsc_value_to_string"));
    if (finish == nullptr) {
        state->done = true;
        return;
    }

    void *error = nullptr;
    void *javascriptResult = finish(source, asyncResult, &error);
    if (error != nullptr) {
        state->ok = false;
        state->done = true;
        return;
    }
    if (javascriptResult == nullptr || getJsValue == nullptr || valueToString == nullptr) {
        state->ok = true;
        state->done = true;
        return;
    }

    void *value = getJsValue(javascriptResult);
    char *text = valueToString(value);
    if (text != nullptr) {
        state->result = wxString::FromUTF8(text);
        g_free(text);
        state->ok = true;
    }
    if (unrefResult != nullptr) {
        unrefResult(javascriptResult);
    }
    state->done = true;
}

extern "C" void onWebKitLoadChanged(void *webView, int loadEvent, void *userData) {
    (void)webView;
    if (loadEvent != kWebKitLoadFinished) {
        return;
    }
    auto *pane = static_cast<DeepSeekWebViewPane *>(userData);
    if (pane != nullptr) {
        pane->NotifyLoaded();
    }
}

extern "C" void onWebKitLoadFailed(void *webView, const char *failingUri, void *error, void *userData) {
    (void)webView;
    (void)failingUri;
    auto *pane = static_cast<DeepSeekWebViewPane *>(userData);
    if (pane == nullptr) {
        return;
    }
    wxString message = wxString("load-error");
    if (error != nullptr) {
        using ErrorMessageFn = const char *(*)(void *gerror);
        auto *messageFn = reinterpret_cast<ErrorMessageFn>(dlsym(RTLD_DEFAULT, "g_error_get_message"));
        if (messageFn != nullptr) {
            const char *text = messageFn(error);
            if (text != nullptr) {
                message = wxString::FromUTF8(text);
            }
        }
    }
    pane->NotifyError(message);
}

extern "C" void onWebKitTitleNotify(void *object, void * /*pspec*/, void *userData) {
    (void)object;
    auto *pane = static_cast<DeepSeekWebViewPane *>(userData);
    if (pane != nullptr) {
        pane->NotifyTitleChanged();
    }
}

void addDeepSeekUserScript(void *webView) {
    using GetUserContentManagerFn = void *(*)(void *webView);
    using ScriptNewFn = void *(*)(const char *source, int injectedFrames, int injectionTime,
                                  const char *const *whitelist, const char *const *blacklist);
    using AddScriptFn = void (*)(void *manager, void *script);

    auto *getUserContentManager = reinterpret_cast<GetUserContentManagerFn>(
        dlsym(RTLD_DEFAULT, "webkit_web_view_get_user_content_manager"));
    auto *scriptNew = reinterpret_cast<ScriptNewFn>(dlsym(RTLD_DEFAULT, "webkit_user_script_new"));
    auto *addScript =
        reinterpret_cast<AddScriptFn>(dlsym(RTLD_DEFAULT, "webkit_user_content_manager_add_script"));
    if (webView == nullptr || getUserContentManager == nullptr || scriptNew == nullptr ||
        addScript == nullptr) {
        return;
    }

    void *manager = getUserContentManager(webView);
    if (manager == nullptr) {
        return;
    }
    void *script = scriptNew(
        "window.__oremindDeepSeekSent = false; window.__oremindDeepSeekSendAttempted = false; "
        "window.__oremindDeepSeekHistoryDone = false; window.__oremindDeepSeekHistoryOpened = false; "
        "window.__oremindDeepSeekSearchOpened = false; window.__oremindDeepSeekSearchTyped = false;",
        0, 0, nullptr, nullptr);
    if (script == nullptr) {
        return;
    }
    addScript(manager, script);
    g_object_unref(script);
}

} // namespace
#endif

void prepareDeepSeekWebViewEnvironment() {
#if defined(__WXGTK__)
    if (wxGetenv("WEBKIT_DISABLE_DMABUF_RENDERER") == nullptr) {
        wxSetEnv("WEBKIT_DISABLE_DMABUF_RENDERER", "1");
    }
    ensureWebKitLoaded();
    getDeepSeekWebKitContext();
#endif
}

void applyDeepSeekWebViewSettings(void *webKitWebView) {
#if defined(__WXGTK__)
    if (webKitWebView == nullptr) {
        return;
    }

    WebKitApi &api = webkitApi();
    if (api.get_settings == nullptr || api.set_user_agent == nullptr) {
        return;
    }

    void *settings = api.get_settings(webKitWebView);
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

    if (api.permission_allow != nullptr) {
        g_signal_connect(webKitWebView, "permission-request", G_CALLBACK(onWebKitPermissionRequest),
                         nullptr);
    }
#endif
}

DeepSeekWebViewPane::DeepSeekWebViewPane(wxWindow *parent, wxWindowID id) {
    Create(parent, id);
}

bool DeepSeekWebViewPane::Create(wxWindow *parent, wxWindowID id, const wxPoint &pos,
                                 const wxSize &size) {
    const long style = wxHSCROLL | wxVSCROLL;
    if (!PreCreation(parent, pos, size) ||
        !CreateBase(parent, id, pos, size, style, wxDefaultValidator, wxT("DeepSeekWebView"))) {
        return false;
    }

#if defined(__WXGTK__)
    if (!attachWebKitView()) {
        return false;
    }
#endif

    if (m_parent != nullptr) {
        m_parent->DoAddChild(this);
    }
    PostCreation(size);
    return true;
}

bool DeepSeekWebViewPane::attachWebKitView() {
#if defined(__WXGTK__)
    using ViewNewWithContextFn = void *(*)(void *context);

    void *context = getDeepSeekWebKitContext();
    auto *viewNewWithContext =
        reinterpret_cast<ViewNewWithContextFn>(dlsym(RTLD_DEFAULT, "webkit_web_view_new_with_context"));
    if (context == nullptr || viewNewWithContext == nullptr) {
        return false;
    }

    m_webView = viewNewWithContext(context);
    if (m_webView == nullptr) {
        return false;
    }

    applyDeepSeekWebViewSettings(m_webView);
    addDeepSeekUserScript(m_webView);

    GTKCreateScrolledWindowWith(static_cast<GtkWidget *>(m_webView));
    g_object_ref(m_widget);

    g_signal_connect(m_webView, "load-changed", G_CALLBACK(onWebKitLoadChanged), this);
    g_signal_connect(m_webView, "load-failed", G_CALLBACK(onWebKitLoadFailed), this);
    g_signal_connect(m_webView, "notify::title", G_CALLBACK(onWebKitTitleNotify), this);
    return true;
#else
    return false;
#endif
}

DeepSeekWebViewPane::~DeepSeekWebViewPane() = default;

void DeepSeekWebViewPane::SetEventHandlers(LoadedFn onLoaded, ErrorFn onError, TitleFn onTitleChanged) {
    m_onLoaded = std::move(onLoaded);
    m_onError = std::move(onError);
    m_onTitleChanged = std::move(onTitleChanged);
}

void DeepSeekWebViewPane::NotifyLoaded() {
    if (m_onLoaded) {
        m_onLoaded(GetCurrentURL());
    }
}

void DeepSeekWebViewPane::NotifyError(const wxString &message) {
    if (m_onError) {
        m_onError(message);
    }
}

void DeepSeekWebViewPane::NotifyTitleChanged() {
    if (m_onTitleChanged) {
        m_onTitleChanged();
    }
}

void DeepSeekWebViewPane::LoadURL(const wxString &url) {
#if defined(__WXGTK__)
    if (m_webView == nullptr) {
        return;
    }
    using LoadUriFn = void (*)(void *webView, const char *uri);
    auto *loadUri = reinterpret_cast<LoadUriFn>(dlsym(RTLD_DEFAULT, "webkit_web_view_load_uri"));
    if (loadUri != nullptr) {
        loadUri(m_webView, url.utf8_str().data());
    }
#endif
}

wxString DeepSeekWebViewPane::GetCurrentURL() const {
#if defined(__WXGTK__)
    if (m_webView == nullptr) {
        return wxString();
    }
    using GetUriFn = const char *(*)(void *webView);
    auto *getUri = reinterpret_cast<GetUriFn>(dlsym(RTLD_DEFAULT, "webkit_web_view_get_uri"));
    if (getUri == nullptr) {
        return wxString();
    }
    const char *uri = getUri(m_webView);
    return uri != nullptr ? wxString::FromUTF8(uri) : wxString();
#else
    return wxString();
#endif
}

wxString DeepSeekWebViewPane::GetCurrentTitle() const {
#if defined(__WXGTK__)
    if (m_webView == nullptr) {
        return wxString();
    }
    using GetTitleFn = const char *(*)(void *webView);
    auto *getTitle = reinterpret_cast<GetTitleFn>(dlsym(RTLD_DEFAULT, "webkit_web_view_get_title"));
    if (getTitle == nullptr) {
        return wxString();
    }
    const char *title = getTitle(m_webView);
    return title != nullptr ? wxString::FromUTF8(title) : wxString();
#else
    return wxString();
#endif
}

bool DeepSeekWebViewPane::RunScript(const wxString &javascript, wxString *output) {
#if defined(__WXGTK__)
    if (m_webView == nullptr) {
        return false;
    }
    using RunJavascriptFn = void (*)(void *webView, const char *script, void *cancellable,
                                     void (*callback)(void *, void *, void *), void *userData);
    auto *runJavascript =
        reinterpret_cast<RunJavascriptFn>(dlsym(RTLD_DEFAULT, "webkit_web_view_run_javascript"));
    if (runJavascript == nullptr) {
        return false;
    }

    ScriptRunState state;
    runJavascript(m_webView, javascript.utf8_str().data(), nullptr, onRunJavascriptFinished, &state);
    while (!state.done) {
        g_main_context_iteration(nullptr, TRUE);
    }
    if (output != nullptr && state.ok) {
        *output = state.result;
    }
    return state.ok;
#else
    (void)javascript;
    (void)output;
    return false;
#endif
}

bool DeepSeekWebViewPane::SendCtrlK() {
#if defined(__WXGTK__)
    if (m_webView == nullptr) {
        return false;
    }

    using GtkWidgetGetRealizedFn = int (*)(void *widget);
    using GtkWidgetGetWindowFn = void *(*)(void *widget);
    using GtkWidgetEventFn = int (*)(void *widget, void *event);
    using GdkEventNewFn = void *(*)(int type);
    using GdkEventFreeFn = void (*)(void *event);
    using GObjectRefFn = void (*)(void *object);
    using GdkKeysymFromNameFn = unsigned (*)(const char *keyval_name);

    auto *getRealized =
        reinterpret_cast<GtkWidgetGetRealizedFn>(dlsym(RTLD_DEFAULT, "gtk_widget_get_realized"));
    auto *getWindow =
        reinterpret_cast<GtkWidgetGetWindowFn>(dlsym(RTLD_DEFAULT, "gtk_widget_get_window"));
    auto *widgetEvent =
        reinterpret_cast<GtkWidgetEventFn>(dlsym(RTLD_DEFAULT, "gtk_widget_event"));
    auto *eventNew = reinterpret_cast<GdkEventNewFn>(dlsym(RTLD_DEFAULT, "gdk_event_new"));
    auto *eventFree = reinterpret_cast<GdkEventFreeFn>(dlsym(RTLD_DEFAULT, "gdk_event_free"));
    auto *gObjectRef = reinterpret_cast<GObjectRefFn>(dlsym(RTLD_DEFAULT, "g_object_ref"));
    auto *keysymFromName =
        reinterpret_cast<GdkKeysymFromNameFn>(dlsym(RTLD_DEFAULT, "gdk_keyval_from_name"));
    if (getRealized == nullptr || getWindow == nullptr || widgetEvent == nullptr ||
        eventNew == nullptr || eventFree == nullptr || gObjectRef == nullptr) {
        return false;
    }
    if (!getRealized(m_webView)) {
        return false;
    }
    void *window = getWindow(m_webView);
    if (window == nullptr) {
        return false;
    }

    constexpr int kGdkKeyPress = 8;
    constexpr int kGdkKeyRelease = 9;
    constexpr unsigned kGdkKeyK = 0x06b;
    constexpr unsigned kGdkControlMask = 1u << 2;
    const unsigned keyval =
        keysymFromName != nullptr ? keysymFromName("k") : kGdkKeyK;

    struct GdkEventKeyLayout {
        int type;
        void *window;
        int send_event;
        unsigned time;
        double x;
        double y;
        double *axes;
        unsigned state;
        unsigned keyval;
    };

    const auto sendKey = [&](int type) {
        void *event = eventNew(type);
        if (event == nullptr) {
            return;
        }
        auto *keyEvent = reinterpret_cast<GdkEventKeyLayout *>(event);
        keyEvent->window = window;
        keyEvent->send_event = 1;
        keyEvent->state = kGdkControlMask;
        keyEvent->keyval = keyval;
        gObjectRef(window);
        widgetEvent(m_webView, event);
        eventFree(event);
    };

    sendKey(kGdkKeyPress);
    sendKey(kGdkKeyRelease);
    return true;
#else
    return false;
#endif
}

bool DeepSeekWebViewPane::SendEscape() {
#if defined(__WXGTK__)
    if (m_webView == nullptr) {
        return false;
    }

    using GtkWidgetGetRealizedFn = int (*)(void *widget);
    using GtkWidgetGetWindowFn = void *(*)(void *widget);
    using GtkWidgetEventFn = int (*)(void *widget, void *event);
    using GdkEventNewFn = void *(*)(int type);
    using GdkEventFreeFn = void (*)(void *event);
    using GObjectRefFn = void (*)(void *object);
    using GdkKeysymFromNameFn = unsigned (*)(const char *keyval_name);

    auto *getRealized =
        reinterpret_cast<GtkWidgetGetRealizedFn>(dlsym(RTLD_DEFAULT, "gtk_widget_get_realized"));
    auto *getWindow =
        reinterpret_cast<GtkWidgetGetWindowFn>(dlsym(RTLD_DEFAULT, "gtk_widget_get_window"));
    auto *widgetEvent =
        reinterpret_cast<GtkWidgetEventFn>(dlsym(RTLD_DEFAULT, "gtk_widget_event"));
    auto *eventNew = reinterpret_cast<GdkEventNewFn>(dlsym(RTLD_DEFAULT, "gdk_event_new"));
    auto *eventFree = reinterpret_cast<GdkEventFreeFn>(dlsym(RTLD_DEFAULT, "gdk_event_free"));
    auto *gObjectRef = reinterpret_cast<GObjectRefFn>(dlsym(RTLD_DEFAULT, "g_object_ref"));
    auto *keysymFromName =
        reinterpret_cast<GdkKeysymFromNameFn>(dlsym(RTLD_DEFAULT, "gdk_keyval_from_name"));
    if (getRealized == nullptr || getWindow == nullptr || widgetEvent == nullptr ||
        eventNew == nullptr || eventFree == nullptr || gObjectRef == nullptr) {
        return false;
    }
    if (!getRealized(m_webView)) {
        return false;
    }
    void *window = getWindow(m_webView);
    if (window == nullptr) {
        return false;
    }

    constexpr int kGdkKeyPress = 8;
    constexpr int kGdkKeyRelease = 9;
    constexpr unsigned kGdkEscape = 0xff1b;
    const unsigned keyval =
        keysymFromName != nullptr ? keysymFromName("Escape") : kGdkEscape;

    struct GdkEventKeyLayout {
        int type;
        void *window;
        int send_event;
        unsigned time;
        double x;
        double y;
        double *axes;
        unsigned state;
        unsigned keyval;
    };

    const auto sendKey = [&](int type) {
        void *event = eventNew(type);
        if (event == nullptr) {
            return;
        }
        auto *keyEvent = reinterpret_cast<GdkEventKeyLayout *>(event);
        keyEvent->window = window;
        keyEvent->send_event = 1;
        keyEvent->state = 0;
        keyEvent->keyval = keyval;
        gObjectRef(window);
        widgetEvent(m_webView, event);
        eventFree(event);
    };

    sendKey(kGdkKeyPress);
    sendKey(kGdkKeyRelease);
    return true;
#else
    return false;
#endif
}
