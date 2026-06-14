#ifndef OBSERVER_FRAME_H
#define OBSERVER_FRAME_H

#include "ObservationStore.h"
#include "QuoteProvider.h"
#include "RenderDriver.h"

#include <memory>
#include <wx/timer.h>
#include <wx/wx.h>

#if defined(__WXGTK__)
#include <X11/Xlib.h>
#endif

class ObserverFrame : public wxFrame {
public:
    ObserverFrame();
    ~ObserverFrame() override;

private:
    void onTimer(wxTimerEvent& event);
    void setupGlobalHotKey();
    void cleanupGlobalHotKey();
    void onHotKeyPoll(wxTimerEvent& event);
    void triggerPromptFromHotKey();
    void scheduleNextPrompt(int delayMs);
    void handlePromptClosed(const ObserveResult& result);
    void showPrompt();
    void exitApp();

    static constexpr int SnoozeIntervalMs = 30000;
    static constexpr int PromptTimerId = wxID_HIGHEST + 100;
    static constexpr int HotKeyTimerId = wxID_HIGHEST + 101;

    wxTimer m_timer;
    wxTimer m_hotKeyTimer;
#if defined(__WXGTK__)
    Display* m_hotKeyDisplay = nullptr;
    Window m_hotKeyRoot = 0;
    unsigned int m_hotKeyModifiers = 0;
    int m_hotKeyCode = 0;
#endif
    std::unique_ptr<ObservationStore> m_store;
    QuoteProvider m_quoteProvider;
    std::unique_ptr<RenderDriver> m_renderDriver;
    double m_intervalSeconds = 120.0;
    int m_consecutiveSkips = 0;
    bool m_hotKeyRegistered = false;
    bool m_promptOpen = false;
};

#endif
