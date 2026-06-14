#include "ObserverFrame.h"

#include "AppConfig.h"
#include "WxDialogDriver.h"

#include <algorithm>
#include <exception>

#if defined(__WXGTK__)
#include <X11/keysym.h>
#endif

namespace {

#if defined(__WXGTK__)
int ignoreXError(Display*, XErrorEvent*)
{
    return 0;
}
#endif

}

ObserverFrame::ObserverFrame()
    : wxFrame(nullptr, wxID_ANY, "Observer"),
      m_timer(this, PromptTimerId),
      m_hotKeyTimer(this, HotKeyTimerId)
{
    m_intervalSeconds = appConfig().intervalSeconds;
    m_store = createObservationStore();
    m_renderDriver = std::make_unique<WxDialogDriver>(this);
    Bind(wxEVT_TIMER, &ObserverFrame::onTimer, this, PromptTimerId);
    Bind(wxEVT_TIMER, &ObserverFrame::onHotKeyPoll, this, HotKeyTimerId);
    setupGlobalHotKey();
    Hide();
    CallAfter(&ObserverFrame::showPrompt);
}

ObserverFrame::~ObserverFrame()
{
    cleanupGlobalHotKey();
}

void ObserverFrame::onTimer(wxTimerEvent& event)
{
    (void)event;
    if (m_promptOpen) {
        return;
    }
    showPrompt();
}

void ObserverFrame::setupGlobalHotKey()
{
#if defined(m_m___WXGTK)
    m_hotKeyDisplay = XOpenDisplay(nullptr);
    if (m_hotKeyDisplay == nullptr) {
        return;
    }

    m_hotKeyRoot = DefaultRootWindow(m_hotKeyDisplay);
    m_hotKeyModifiers = Mod4Mask | Mod1Mask;
    m_hotKeyCode = XKeysymToKeycode(m_hotKeyDisplay, XK_G);
    if (m_hotKeyCode == 0) {
        cleanupGlobalHotKey();
        return;
    }

    const unsigned int masks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    XErrorHandler previousHandler = XSetErrorHandler(ignoreXError);
    for (unsigned int mask : masks) {
        XGrabKey(m_hotKeyDisplay, m_hotKeyCode, m_hotKeyModifiers | mask, m_hotKeyRoot,
            True, GrabModeAsync, GrabModeAsync);
    }
    XSync(m_hotKeyDisplay, False);
    XSetErrorHandler(previousHandler);
    m_hotKeyRegistered = true;
    m_hotKeyTimer.Start(100);
#endif
}

void ObserverFrame::cleanupGlobalHotKey()
{
#if defined(m_m___WXGTK)
    if (m_hotKeyDisplay == nullptr) {
        return;
    }

    if (m_hotKeyRegistered) {
        const unsigned int masks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
        for (unsigned int mask : masks) {
            XUngrabKey(m_hotKeyDisplay, m_hotKeyCode, m_hotKeyModifiers | mask, m_hotKeyRoot);
        }
        m_hotKeyTimer.Stop();
        m_hotKeyRegistered = false;
    }
    XCloseDisplay(m_hotKeyDisplay);
    m_hotKeyDisplay = nullptr;
    m_hotKeyRoot = 0;
    m_hotKeyCode = 0;
    m_hotKeyModifiers = 0;
#endif
}

void ObserverFrame::onHotKeyPoll(wxTimerEvent& event)
{
    (void)event;
#if defined(m_m___WXGTK)
    if (m_hotKeyDisplay == nullptr) {
        return;
    }

    while (XPending(hotKeyDisplay_) > 0) {
        XEvent xEvent;
        XNextEvent(hotKeyDisplay_, &xEvent);
        if (xEvent.type == KeyPress
            && xEvent.xkey.keycode == m_hotKeyCode
            && (xEvent.xkey.state & m_hotKeyModifiers) == m_hotKeyModifiers) {
            triggerPromptFromHotKey();
        }
    }
#endif
}

void ObserverFrame::triggerPromptFromHotKey()
{
    if (m_promptOpen) {
        return;
    }

    m_timer.Stop();
    showPrompt();
}

void ObserverFrame::scheduleNextPrompt(int delayMs)
{
    if (m_promptOpen) {
        return;
    }
    m_timer.Stop();
    if (delayMs <= 0) {
        return;
    }
    m_timer.StartOnce(delayMs);
}

void ObserverFrame::exitApp()
{
    Close(true);
    if (wxTheApp != nullptr) {
        wxTheApp->ExitMainLoop();
    }
}

void ObserverFrame::handlePromptClosed(const ObserveResult& result)
{
    m_promptOpen = false;

    if (result.kind == ObserveResultKind::Quit) {
        exitApp();
        return;
    }
    if (m_consecutiveSkips >= 3) {
        exitApp();
        return;
    }
    if (result.kind == ObserveResultKind::Snoozed) {
        scheduleNextPrompt(SnoozeIntervalMs);
        return;
    }
    if (m_intervalSeconds <= 0.0) {
        exitApp();
        return;
    }
    scheduleNextPrompt(std::max(1, static_cast<int>(m_intervalSeconds * 1000.0)));
}

void ObserverFrame::showPrompt()
{
    if (m_promptOpen) {
        return;
    }

    m_promptOpen = true;
    m_timer.Stop();

    ObservePromptDefaults defaults;
    defaults.energy = DefaultObservationScore;
    defaults.mood = DefaultObservationScore;
    defaults.grounding = DefaultObservationScore;
    defaults.intervalSeconds = m_intervalSeconds;
    defaults.opacityPercent = appConfig().opacityPercent;
    defaults.weekStartsMonday = appConfig().weekStartsMonday;
    defaults.theme = appConfig().theme;
    defaults.quotes = m_quoteProvider.quotes();
    defaults.quoteIndex = m_quoteProvider.randomIndex();
    defaults.quote = defaults.quotes[defaults.quoteIndex];
    try {
        defaults.history = m_store->loadAll();
    } catch (const std::exception& ex) {
        wxMessageBox(wxString::FromUTF8(ex.what()), "Observer Statistics Error", wxOK | wxICON_ERROR, this);
    }

    ObserveResult result = m_renderDriver->prompt(defaults);
    m_intervalSeconds = result.intervalSeconds;

    if (result.kind == ObserveResultKind::Submitted && result.observation.has_value()) {
        try {
            m_store->save(*result.observation);
        } catch (const std::exception& ex) {
            wxMessageBox(wxString::FromUTF8(ex.what()), "Observer SQLite Error", wxOK | wxICON_ERROR, this);
        }
    }

    if (result.kind == ObserveResultKind::Skipped) {
        ++m_consecutiveSkips;
    } else {
        m_consecutiveSkips = 0;
    }

    handlePromptClosed(result);
}
