#ifndef OBSERVER_FRAME_H
#define OBSERVER_FRAME_H

#include "ObservationStore.h"
#include "QuoteProvider.h"
#include "RenderDriver.h"

#include <memory>
#include <wx/timer.h>
#include <wx/wx.h>

class ObserverFrame : public wxFrame {
public:
    ObserverFrame();

private:
    void onTimer(wxTimerEvent& event);
    void scheduleNormal();
    void scheduleSnooze();
    void showPrompt();
    void exitApp();

    static constexpr int SnoozeIntervalMs = 30000;

    wxTimer timer_;
    std::unique_ptr<ObservationStore> store_;
    QuoteProvider quoteProvider_;
    std::unique_ptr<RenderDriver> renderDriver_;
    double intervalSeconds_ = 120.0;
    int consecutiveSkips_ = 0;
    bool promptOpen_ = false;
};

#endif
