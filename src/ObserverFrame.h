#ifndef OBSERVER_FRAME_H
#define OBSERVER_FRAME_H

#include "ObservationStore.h"
#include "QuoteProvider.h"
#include "RenderDriver.h"

#include <memory>
#include <wx/timer.h>
#include <wx/wx.h>

class wxTaskBarIcon;

#if defined(__WXGTK__)
#include <X11/Xlib.h>
#endif

class ObserverFrame : public wxFrame {
public:
    ObserverFrame();
    ~ObserverFrame() override;
    static bool notifyExistingInstance();

    void wakePrompt();
    void showStatisticsDialog();
    void exitApp();

private:
    void onTimer(wxTimerEvent& event);
    void setupTrayIcon();
    void cleanupTrayIcon();
    void setupIpcServer();
    void cleanupIpcServer();
    void onIpcPoll(wxTimerEvent& event);
    void setupGlobalHotKey();
    void cleanupGlobalHotKey();
    void onHotKeyPoll(wxTimerEvent& event);
    void triggerPromptFromHotKey();
    void scheduleNextPrompt(int delayMs);
    void handlePromptClosed(const ObserveResult& result);
    void showPrompt();

    static constexpr int SnoozeIntervalMs = 30000;
    static constexpr int PromptTimerId = wxID_HIGHEST + 100;
    static constexpr int HotKeyTimerId = wxID_HIGHEST + 101;
    static constexpr int IpcTimerId = wxID_HIGHEST + 102;

    wxTimer m_timer;
    wxTimer m_hotKeyTimer;
    wxTimer m_ipcTimer;
#if defined(__WXGTK__)
    Display* m_hotKeyDisplay = nullptr;
    Window m_hotKeyRoot = 0;
    unsigned int m_hotKeyModifiers = 0;
    int m_promptHotKeyCode = 0;
    int m_statsHotKeyCode = 0;
#endif
    std::unique_ptr<ObservationStore> m_store;
    QuoteProvider m_quoteProvider;
    std::unique_ptr<RenderDriver> m_renderDriver;
    std::unique_ptr<wxTaskBarIcon> m_trayIcon;
    double m_intervalSeconds = 120.0;
    int m_ipcServerFd = -1;
    int m_consecutiveSkips = 0;
    bool m_hotKeyRegistered = false;
    bool m_promptOpen = false;
};

#endif
