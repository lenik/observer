#include "WxDialogDriver.h"

#include "ObservationDialog.h"

namespace {

constexpr int ID_SNOOZE = wxID_HIGHEST + 1;
constexpr int ID_QUIT = wxID_HIGHEST + 2;

}

WxDialogDriver::WxDialogDriver(wxWindow* parent)
    : m_parent(parent)
{
}

ObserveResult WxDialogDriver::prompt(const ObservePromptDefaults& defaults)
{
    ObservationDialog dialog(m_parent, defaults);
    m_activeDialog = &dialog;
    dialog.CallAfter(&ObservationDialog::animateIn);
    const int result = dialog.ShowModal();
    m_activeDialog = nullptr;

    if (result == wxID_OK) {
        Observation observation = dialog.observation();
        return ObserveResult{ObserveResultKind::Submitted, observation, dialog.intervalSeconds()};
    }

    if (result == ID_SNOOZE) {
        return ObserveResult{ObserveResultKind::Snoozed, std::nullopt, dialog.intervalSeconds()};
    }

    if (result == ID_QUIT) {
        return ObserveResult{ObserveResultKind::Quit, std::nullopt, dialog.intervalSeconds()};
    }

    return ObserveResult{ObserveResultKind::Skipped, std::nullopt, dialog.intervalSeconds()};
}

void WxDialogDriver::showStatisticsIfActive()
{
    if (m_activeDialog != nullptr && !m_activeDialog->isStatisticsOpen()) {
        m_activeDialog->showStatistics();
    }
}
