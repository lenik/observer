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
      timer_(this, PromptTimerId),
      hotKeyTimer_(this, HotKeyTimerId)
{
    intervalSeconds_ = appConfig().intervalSeconds;
    store_ = createObservationStore();
    renderDriver_ = std::make_unique<WxDialogDriver>(this);
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
    showPrompt();
}

void ObserverFrame::setupGlobalHotKey()
{
#if defined(__WXGTK__)
    hotKeyDisplay_ = XOpenDisplay(nullptr);
    if (hotKeyDisplay_ == nullptr) {
        return;
    }

    hotKeyRoot_ = DefaultRootWindow(hotKeyDisplay_);
    hotKeyModifiers_ = Mod4Mask | Mod1Mask;
    hotKeyCode_ = XKeysymToKeycode(hotKeyDisplay_, XK_G);
    if (hotKeyCode_ == 0) {
        cleanupGlobalHotKey();
        return;
    }

    const unsigned int masks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    XErrorHandler previousHandler = XSetErrorHandler(ignoreXError);
    for (unsigned int mask : masks) {
        XGrabKey(hotKeyDisplay_, hotKeyCode_, hotKeyModifiers_ | mask, hotKeyRoot_,
            True, GrabModeAsync, GrabModeAsync);
    }
    XSync(hotKeyDisplay_, False);
    XSetErrorHandler(previousHandler);
    hotKeyRegistered_ = true;
    hotKeyTimer_.Start(100);
#endif
}

void ObserverFrame::cleanupGlobalHotKey()
{
#if defined(__WXGTK__)
    if (hotKeyDisplay_ == nullptr) {
        return;
    }

    if (hotKeyRegistered_) {
        const unsigned int masks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
        for (unsigned int mask : masks) {
            XUngrabKey(hotKeyDisplay_, hotKeyCode_, hotKeyModifiers_ | mask, hotKeyRoot_);
        }
        hotKeyTimer_.Stop();
        hotKeyRegistered_ = false;
    }
    XCloseDisplay(hotKeyDisplay_);
    hotKeyDisplay_ = nullptr;
    hotKeyRoot_ = 0;
    hotKeyCode_ = 0;
    hotKeyModifiers_ = 0;
#endif
}

void ObserverFrame::onHotKeyPoll(wxTimerEvent& event)
{
    (void)event;
#if defined(__WXGTK__)
    if (hotKeyDisplay_ == nullptr) {
        return;
    }

    while (XPending(hotKeyDisplay_) > 0) {
        XEvent xEvent;
        XNextEvent(hotKeyDisplay_, &xEvent);
        if (xEvent.type == KeyPress
            && xEvent.xkey.keycode == hotKeyCode_
            && (xEvent.xkey.state & hotKeyModifiers_) == hotKeyModifiers_) {
            triggerPromptFromHotKey();
        }
    }
#endif
}

void ObserverFrame::triggerPromptFromHotKey()
{
    if (promptOpen_) {
        return;
    }

    timer_.Stop();
    showPrompt();
}

void ObserverFrame::scheduleNormal()
{
    if (intervalSeconds_ <= 0.0) {
        return;
    }
    timer_.StartOnce(std::max(1, static_cast<int>(intervalSeconds_ * 1000.0)));
}

void ObserverFrame::scheduleSnooze()
{
    timer_.StartOnce(SnoozeIntervalMs);
}

void ObserverFrame::exitApp()
{
    Close(true);
    if (wxTheApp != nullptr) {
        wxTheApp->ExitMainLoop();
    }
}

void ObserverFrame::showPrompt()
{
    if (promptOpen_) {
        return;
    }

    promptOpen_ = true;
    timer_.Stop();

    ObservePromptDefaults defaults;
    defaults.energy = DefaultObservationScore;
    defaults.mood = DefaultObservationScore;
    defaults.grounding = DefaultObservationScore;
    defaults.intervalSeconds = intervalSeconds_;
    defaults.opacityPercent = appConfig().opacityPercent;
    defaults.weekStartsMonday = appConfig().weekStartsMonday;
    defaults.theme = appConfig().theme;
    defaults.quotes = quoteProvider_.quotes();
    defaults.quoteIndex = quoteProvider_.randomIndex();
    defaults.quote = defaults.quotes[defaults.quoteIndex];
    try {
        defaults.history = store_->loadAll();
    } catch (const std::exception& ex) {
        wxMessageBox(wxString::FromUTF8(ex.what()), "Observer Statistics Error", wxOK | wxICON_ERROR, this);
    }

    ObserveResult result = renderDriver_->prompt(defaults);
    intervalSeconds_ = result.intervalSeconds;

    if (result.kind == ObserveResultKind::Submitted && result.observation.has_value()) {
        try {
            store_->save(*result.observation);
        } catch (const std::exception& ex) {
            wxMessageBox(wxString::FromUTF8(ex.what()), "Observer SQLite Error", wxOK | wxICON_ERROR, this);
        }
    }

    if (result.kind == ObserveResultKind::Skipped) {
        ++consecutiveSkips_;
    } else {
        consecutiveSkips_ = 0;
    }

    promptOpen_ = false;

    if (result.kind == ObserveResultKind::Quit) {
        exitApp();
    } else if (consecutiveSkips_ >= 3) {
        exitApp();
    } else if (result.kind == ObserveResultKind::Snoozed) {
        scheduleSnooze();
    } else if (intervalSeconds_ <= 0.0) {
        exitApp();
    } else {
        scheduleNormal();
    }
}
