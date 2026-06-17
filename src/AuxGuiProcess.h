#ifndef AUX_GUI_PROCESS_H
#define AUX_GUI_PROCESS_H

#include <wx/string.h>

struct AuxGuiLaunchRequest {
    bool active = false;

    enum class InitialWindow {
        None,
        History,
        DeepSeek,
    } initial = InitialWindow::None;

    wxString deepSeekPrompt;
};

extern AuxGuiLaunchRequest g_auxGuiLaunchRequest;

bool parseAuxGuiLaunchRequest(int argc, char **argv, AuxGuiLaunchRequest &request);
bool forkAuxGuiDaemon(const char *argv0);
bool launchAuxGuiHistory();
bool launchAuxGuiDeepSeek(const wxString &prompt);
bool runAuxGuiSession();

#endif
