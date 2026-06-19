#ifndef WX_DIALOG_DRIVER_H
#define WX_DIALOG_DRIVER_H

#include "RenderDriver.h"

#include <wx/window.h>

class RemindDialog;

class WxDialogDriver : public RenderDriver {
public:
    explicit WxDialogDriver(wxWindow* parent);

    ObserveResult prompt(const RemindPromptDefaults& defaults) override;
    void requestHistoryIfActive();

private:
    wxWindow* m_parent;
    RemindDialog* m_activeDialog = nullptr;
};

#endif
