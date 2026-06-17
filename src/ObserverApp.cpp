#include "ObserverApp.h"

#include "AppConfig.h"
#include "DeepSeekBrowserFrame.h"
#include "DeepSeekLauncher.h"
#include "LayoutDiag.h"
#include "ObserverFrame.h"

#include <exception>
#include <cstdio>
#include <cstdlib>

bool ObserverApp::OnInit()
{
    if (!wxApp::OnInit()) {
        return false;
    }

    wxInitAllImageHandlers();

    if (appConfig().diagMode) {
        const int status = runLayoutDiagnostics("layout.toml");
        std::exit(status);
    }

    if (!g_deepSeekBrowserPrompt.empty()) {
        auto *frame = new DeepSeekBrowserFrame(g_deepSeekBrowserPrompt);
        g_deepSeekBrowserPrompt.clear();
        if (!frame->isReady()) {
            wxMessageBox("WebView is unavailable on this system.", "DeepSeek", wxOK | wxICON_ERROR);
            return false;
        }
        frame->Show(true);
        frame->activateWindow();
        SetTopWindow(frame);
        SetExitOnFrameDelete(true);
        return true;
    }

    SetExitOnFrameDelete(false);

    if (ObserverFrame::notifyExistingInstance()) {
        std::fprintf(stderr, "warning: oremind is already running; waking existing instance.\n");
        return false;
    }

    try {
        auto* frame = new ObserverFrame();
        SetTopWindow(frame);
    } catch (const std::exception& ex) {
        wxMessageBox(wxString::FromUTF8(ex.what()), "Observer Startup Error", wxOK | wxICON_ERROR);
        return false;
    }

    return true;
}
