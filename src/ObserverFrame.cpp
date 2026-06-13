#include "ObserverFrame.h"

#include "AppConfig.h"
#include "WxDialogDriver.h"

#include <algorithm>
#include <exception>

ObserverFrame::ObserverFrame()
    : wxFrame(nullptr, wxID_ANY, "Observer"),
      timer_(this)
{
    intervalSeconds_ = appConfig().intervalSeconds;
    store_ = createObservationStore();
    renderDriver_ = std::make_unique<WxDialogDriver>(this);
    Bind(wxEVT_TIMER, &ObserverFrame::onTimer, this);
    Hide();
    CallAfter(&ObserverFrame::showPrompt);
}

void ObserverFrame::onTimer(wxTimerEvent& event)
{
    (void)event;
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
    defaults.theme = appConfig().theme;
    defaults.quotes = quoteProvider_.quotes();
    defaults.quoteIndex = quoteProvider_.randomIndex();
    defaults.quote = defaults.quotes[defaults.quoteIndex];

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

    if (consecutiveSkips_ >= 3) {
        exitApp();
    } else if (result.kind == ObserveResultKind::Snoozed) {
        scheduleSnooze();
    } else if (intervalSeconds_ <= 0.0) {
        exitApp();
    } else {
        scheduleNormal();
    }
}
