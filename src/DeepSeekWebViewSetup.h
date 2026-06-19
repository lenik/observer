#ifndef DEEPSEEK_WEBVIEW_SETUP_H
#define DEEPSEEK_WEBVIEW_SETUP_H

#include <functional>

#include <wx/control.h>
#include <wx/wx.h>

void prepareDeepSeekWebViewEnvironment();
void applyDeepSeekWebViewSettings(void *webKitWebView);

class DeepSeekWebViewPane : public wxControl {
  public:
    using LoadedFn = std::function<void(const wxString &url)>;
    using ErrorFn = std::function<void(const wxString &message)>;
    using TitleFn = std::function<void()>;

    DeepSeekWebViewPane() = default;
    DeepSeekWebViewPane(wxWindow *parent, wxWindowID id = wxID_ANY);
    ~DeepSeekWebViewPane() override;

    bool Create(wxWindow *parent, wxWindowID id, const wxPoint &pos = wxDefaultPosition,
                const wxSize &size = wxDefaultSize);

    void SetEventHandlers(LoadedFn onLoaded, ErrorFn onError, TitleFn onTitleChanged);

    void LoadURL(const wxString &url);
    wxString GetCurrentURL() const;
    wxString GetCurrentTitle() const;
    bool RunScript(const wxString &javascript, wxString *output = nullptr);
    bool SendCtrlK();
    bool SendEscape();
    void *GetNativeBackend() const { return m_webView; }
    bool isReady() const { return m_webView != nullptr; }

    void NotifyLoaded();
    void NotifyError(const wxString &message);
    void NotifyTitleChanged();

  private:
    bool attachWebKitView();

    void *m_webView = nullptr;
    LoadedFn m_onLoaded;
    ErrorFn m_onError;
    TitleFn m_onTitleChanged;
};

#endif
