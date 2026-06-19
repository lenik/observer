#include "WxDialogDriver.h"

#include "HistoryFrame.h"
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
    if (result == ID_HISTORY) {
        return ObserveResult{ObserveResultKind::History, std::nullopt, dialog.captureResumeDefaults(),
                             std::nullopt, std::nullopt, std::nullopt, dialog.intervalSeconds()};
    }
    return ObserveResult{ObserveResultKind::Skipped, std::nullopt, std::nullopt, std::nullopt,
                         std::nullopt, std::nullopt, dialog.intervalSeconds()};
}

} // namespace

WxDialogDriver::WxDialogDriver(wxWindow *parent) : m_parent(parent) {}

ObserveResult WxDialogDriver::prompt(const RemindPromptDefaults &defaults) {
    RemindPromptDefaults current = defaults;
    for (;;) {
        RemindDialog dialog(m_parent, current);
        m_activeDialog = &dialog;
        dialog.ShowModal();
        m_activeDialog = nullptr;
        const int result = dialog.modalResultCode();
        ObserveResult observeResult = resultFromDialog(result, dialog);
        if (observeResult.kind != ObserveResultKind::History) {
            return observeResult;
        }

        if (observeResult.resume.has_value()) {
            current = *observeResult.resume;
        }

        ObservationStore *store = current.store;
        if (store == nullptr) {
            return observeResult;
        }

        try {
            HistoryFrame history(m_parent, store, current.theme, current.weekStartsMonday,
                                 current.quotes);
            history.setReturnToRemindOnDismiss(true);
            history.ShowModal();
            if (history.returnToRemind()) {
                continue;
            }
        } catch (const std::exception &ex) {
            wxMessageBox(wxString::FromUTF8(ex.what()), "Observer Statistics Error",
                         wxOK | wxICON_ERROR, m_parent);
        }

        return ObserveResult{ObserveResultKind::Skipped, std::nullopt, std::nullopt, std::nullopt,
                             std::nullopt, std::nullopt, current.intervalSeconds};
    }
}

void WxDialogDriver::requestHistoryIfActive() {
    if (m_activeDialog != nullptr) {
        m_activeDialog->requestHistory();
    }
}
