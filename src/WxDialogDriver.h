#ifndef WX_DIALOG_DRIVER_H
#define WX_DIALOG_DRIVER_H

#include "RenderDriver.h"

#include <wx/window.h>

class WxDialogDriver : public RenderDriver {
public:
    explicit WxDialogDriver(wxWindow* parent);

    ObserveResult prompt(const ObservePromptDefaults& defaults) override;

private:
    wxWindow* m_parent;
};

#endif
