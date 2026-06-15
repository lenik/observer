#include "ObserverApp.h"

#include "ObserverFrame.h"

#include <exception>
#include <cstdio>

bool ObserverApp::OnInit()
{
    if (!wxApp::OnInit()) {
        return false;
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
