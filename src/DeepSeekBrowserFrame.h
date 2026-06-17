#ifndef DEEPSEEK_BROWSER_FRAME_H
#define DEEPSEEK_BROWSER_FRAME_H

#include <wx/webview.h>
#include <wx/wx.h>

class wxWebView;

class DeepSeekBrowserFrame : public wxFrame {
  public:
    explicit DeepSeekBrowserFrame(const wxString &prompt);

    bool isReady() const { return m_webView != nullptr; }
    void setPrompt(const wxString &prompt);
    void activateWindow();

  private:
    void onLoaded(wxWebViewEvent &event);
    void onNavigated(wxWebViewEvent &event);
    void onSettleTimer(wxTimerEvent &event);
    void onTimer(wxTimerEvent &event);
    void onClose(wxCloseEvent &event);
    void stopRpa();
    void scheduleRpaAfterLoad();
    void startRpaAttempts();
    bool trySubmitPrompt();
    bool handleRpaResult(const wxString &result);
    wxString buildSubmitScript() const;

    wxWebView *m_webView = nullptr;
    wxTimer m_settleTimer;
    wxTimer m_rpaTimer;
    wxString m_prompt;
    int m_attempts = 0;
};

#endif
