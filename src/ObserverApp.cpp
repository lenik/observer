#include "ObserverApp.h"

#include "ObserverFrame.h"

#include <exception>

bool ObserverApp::OnInit()
{
    if (!wxApp::OnInit()) {
        return false;
    }

    SetExitOnFrameDelete(false);

    try {
        auto* frame = new ObserverFrame();
        SetTopWindow(frame);
    } catch (const std::exception& ex) {
        wxMessageBox(wxString::FromUTF8(ex.what()), "Observer Startup Error", wxOK | wxICON_ERROR);
        return false;
    }

    return true;
}
