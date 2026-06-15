#ifndef OBSERVER_EMOJI_H
#define OBSERVER_EMOJI_H

#include <wx/string.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <ctime>

wxString energyEmoji(double value);
wxString moodEmoji(double value);
wxString groundingEmoji(double value);
wxString scoreWithEmoji(const wxString &emoji, double value);

#endif