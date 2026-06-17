#ifndef DEEPSEEK_LAUNCHER_H
#define DEEPSEEK_LAUNCHER_H

#include <wx/string.h>

extern wxString g_deepSeekBrowserPrompt;

bool launchDeepSeekBrowser(const wxString &prompt);
bool parseDeepSeekBrowserArg(int argc, char **argv, wxString &prompt);

#endif
