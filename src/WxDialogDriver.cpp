#include "WxDialogDriver.h"

#include "ObservationDialog.h"

namespace {

constexpr int ID_SNOOZE = wxID_HIGHEST + 1;

}

WxDialogDriver::WxDialogDriver(wxWindow* parent)
    : parent_(parent)
{
}

ObserveResult WxDialogDriver::prompt(const ObservePromptDefaults& defaults)
{
    ObservationDialog dialog(parent_, defaults);
    dialog.CallAfter(&ObservationDialog::animateIn);
    const int result = dialog.ShowModal();

    if (result == wxID_OK) {
        Observation observation = dialog.observation();
        if (observation.activity.empty()) {
            return ObserveResult{ObserveResultKind::Empty, std::nullopt, dialog.intervalSeconds()};
        }
        return ObserveResult{ObserveResultKind::Submitted, observation, dialog.intervalSeconds()};
    }

    if (result == ID_SNOOZE) {
        return ObserveResult{ObserveResultKind::Snoozed, std::nullopt, dialog.intervalSeconds()};
    }

    return ObserveResult{ObserveResultKind::Skipped, std::nullopt, dialog.intervalSeconds()};
}
