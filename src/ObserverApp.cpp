#include "ObserverApp.h"

#include "AppConfig.h"
#include "DeepSeekWebViewSetup.h"
#include "LayoutDiag.h"
#include "SystemFrame.h"

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

    prepareDeepSeekWebViewEnvironment();
    SetExitOnFrameDelete(false);

    if (SystemFrame::notifyExistingInstance()) {
        std::fprintf(stderr, "warning: oremind is already running; waking existing instance.\n");
        return false;
    }

    try {
        auto* frame = new SystemFrame();
        SetTopWindow(frame);
    } catch (const std::exception& ex) {
        wxMessageBox(wxString::FromUTF8(ex.what()), "Observer Startup Error", wxOK | wxICON_ERROR);
        return false;
    }

    return true;
}
