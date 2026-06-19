#ifndef DEEPSEEK_BROWSER_FRAME_H
#define DEEPSEEK_BROWSER_FRAME_H

#include <wx/wx.h>

class DeepSeekWebViewPane;

enum class DeepSeekPageMode { Unknown, LoginForm, ChatReady };

class DeepSeekBrowserFrame : public wxFrame {
  public:
    DeepSeekBrowserFrame(wxWindow *parent, const wxString &prompt, const wxString &searchQuote);

    bool isReady() const { return m_webView != nullptr; }
    void setPrompt(const wxString &prompt, const wxString &searchQuote);
    void activateWindow();

  private:
    void onLoaded(const wxString &url);
    void onError(const wxString &message);
    void onShow(wxShowEvent &event);
    void onTitleChanged();
    void onRpaTimer(wxTimerEvent &event);
    void onLoginWatchTimer(wxTimerEvent &event);
    void onClose(wxCloseEvent &event);
    void onCharHook(wxKeyEvent &event);
    void onStatusBarClick(wxMouseEvent &event);
    void stopRpa();
    void scheduleRpaAfterLoad(bool force = false, bool resetSentFlags = false);
    bool tryHistorySearch();
    bool trySubmitPrompt();
    bool handleRpaResult(const wxString &result);
    wxString buildHistorySearchScript() const;
    wxString buildSubmitScript() const;
    wxString buildProbeScript() const;
    wxString buildChatReadyProbeScript() const;
    bool updateChatReadyProbe();
    void handleAuthPageTransition();
    void startLoginWatch();
    void stopLoginWatch();
    void promoteToChatPage();
    void refreshStatusBar();
    void ensureDeepSeekLoaded();
    wxString scriptResultHint(const wxString &result) const;
    wxString currentPhaseDescription() const;

    DeepSeekWebViewPane *m_webView = nullptr;
    wxStatusBar *m_statusBar = nullptr;
    wxTimer m_rpaTimer;
    wxTimer m_loginWatchTimer;
    wxString m_prompt;
    wxString m_searchQuote;
    wxString m_lastScriptResult;
    wxString m_lastProbeSummary;
    int m_attempts = 0;
    int m_loginWatchTicks = 0;
    DeepSeekPageMode m_pageMode = DeepSeekPageMode::Unknown;
    bool m_rpaFinished = false;
    bool m_rpaGaveUp = false;
    bool m_rpaCycleActive = false;
    bool m_rpaInitialWait = false;
    bool m_historySearchDone = false;
    bool m_searchHotkeySent = false;
    int m_searchHotkeyWaitTicks = 0;
};

#endif
