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

    wxTimer timer_;
    wxTimer hotKeyTimer_;
#if defined(__WXGTK__)
    Display* hotKeyDisplay_ = nullptr;
    Window hotKeyRoot_ = 0;
    unsigned int hotKeyModifiers_ = 0;
    int hotKeyCode_ = 0;
#endif
    std::unique_ptr<ObservationStore> store_;
    QuoteProvider quoteProvider_;
    std::unique_ptr<RenderDriver> renderDriver_;
    double intervalSeconds_ = 120.0;
    int consecutiveSkips_ = 0;
    bool hotKeyRegistered_ = false;
    bool promptOpen_ = false;
};

#endif
