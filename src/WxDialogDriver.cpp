#include "WxDialogDriver.h"

#include "RemindDialog.h"

namespace {

constexpr int ID_SNOOZE = wxID_HIGHEST + 1;
constexpr int ID_QUIT = wxID_HIGHEST + 2;
constexpr int ID_HISTORY = wxID_HIGHEST + 3;
constexpr int ID_BROWSER = wxID_HIGHEST + 4;

ObserveResult resultFromDialog(int result, RemindDialog &dialog) {
    if (result == wxID_OK) {
        return ObserveResult{ObserveResultKind::Submitted, dialog.observation(), std::nullopt,
                             std::nullopt, std::nullopt, std::nullopt, dialog.intervalSeconds()};
    }
    if (result == ID_SNOOZE) {
        return ObserveResult{ObserveResultKind::Snoozed, std::nullopt, std::nullopt, std::nullopt,
                             std::nullopt, std::nullopt, dialog.intervalSeconds()};
    }
    if (result == ID_QUIT) {
        return ObserveResult{ObserveResultKind::Quit, std::nullopt, std::nullopt, std::nullopt,
                             std::nullopt, std::nullopt, dialog.intervalSeconds()};
    }
    if (result == ID_BROWSER) {
        ObserveResult observeResult{ObserveResultKind::Browser, std::nullopt,
                                    dialog.captureResumeDefaults(), std::nullopt, std::nullopt,
                                    std::nullopt, dialog.intervalSeconds()};
        if (dialog.externalBrowserUrl().has_value()) {
            observeResult.externalBrowserUrl = dialog.externalBrowserUrl();
        } else {
            observeResult.browserPrompt = dialog.browserPrompt();
            observeResult.browserSearchQuote = dialog.browserSearchQuote();
        }
        return observeResult;
    }
    return ObserveResult{ObserveResultKind::Skipped, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt, std::nullopt, dialog.intervalSeconds()};
}

} // namespace

WxDialogDriver::WxDialogDriver(wxWindow *parent) : m_parent(parent) {}

ObserveResult WxDialogDriver::prompt(const RemindPromptDefaults &defaults) {
    RemindDialog dialog(m_parent, defaults);
    m_activeDialog = &dialog;
    dialog.ShowModal();
    m_activeDialog = nullptr;
    const int result = dialog.modalResultCode();
    return resultFromDialog(result, dialog);
}

void WxDialogDriver::requestHistoryIfActive() {
    if (m_activeDialog != nullptr) {
        m_activeDialog->requestHistory();
    }
}
