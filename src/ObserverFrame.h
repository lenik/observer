#ifndef OBSERVER_FRAME_H
#define OBSERVER_FRAME_H

#include "ObservationStore.h"
#include "QuoteProvider.h"
#include "RenderDriver.h"

#include <memory>
#include <wx/timer.h>
#include <wx/wx.h>

class DeepSeekBrowserFrame;
class wxTaskBarIcon;

class ObserverFrame : public wxFrame {
public:
    ObserverFrame();
    ~ObserverFrame() override;
    static bool notifyExistingInstance();

    void wakePrompt();
    void showHistoryFrame();
    void exitApp();

private:
    void showBrowserWindow(const wxString &prompt, const wxString &searchQuote);
    void onTimer(wxTimerEvent& event);
    void setupTrayIcon();
    void cleanupTrayIcon();
    void setupIpcServer();
    void cleanupIpcServer();
    void onIpcPoll(wxTimerEvent& event);
    void handleExternalWake();
    void scheduleNextPrompt(int delayMs);
    void handlePromptClosed(const ObserveResult& result);
    void onBrowserWindowClosed();
    void showPrompt();
    bool hasOpenBrowserWindow() const;

    static constexpr int SnoozeIntervalMs = 30000;
    static constexpr int PromptTimerId = wxID_HIGHEST + 100;
    static constexpr int IpcTimerId = wxID_HIGHEST + 101;

    wxTimer m_timer;
    wxTimer m_ipcTimer;
    std::unique_ptr<ObservationStore> m_store;
    QuoteProvider m_quoteProvider;
    std::unique_ptr<RenderDriver> m_renderDriver;
    std::unique_ptr<wxTaskBarIcon> m_trayIcon;
    double m_intervalSeconds = 120.0;
    int m_ipcServerFd = -1;
    int m_consecutiveSkips = 0;
    bool m_promptOpen = false;
    std::optional<ObservePromptDefaults> m_savedPromptDefaults;
    DeepSeekBrowserFrame *m_browserFrame = nullptr;
};

#endif
