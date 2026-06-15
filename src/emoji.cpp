#include "emoji.h"
#include "formatting.h"

#include <wx/string.h>

#include <cmath>
#include <cstdlib>
#include <ctime>

wxString energyEmoji(double value) {
    if (value < 1.0) {
        return wxString::FromUTF8("⏻");
    }
    if (value < 2.5) {
        return wxString::FromUTF8("🪫");
    }
    if (value < 4.5) {
        return wxString::FromUTF8("🔋");
    }
    return wxString::FromUTF8("⚡");
}

wxString moodEmoji(double value) {
    if (value < 1.0) {
        return wxString::FromUTF8("😢");
    }
    if (value < 2.0) {
        return wxString::FromUTF8("🙁");
    }
    if (value < 3.0) {
        return wxString::FromUTF8("😐");
    }
    if (value < 4.0) {
        return wxString::FromUTF8("🙂");
    }
    return wxString::FromUTF8("😄");
}

wxString groundingEmoji(double value) {
    if (value < 1.0) {
        return wxString::FromUTF8("🔬");
    }
    if (value < 2.0) {
        return wxString::FromUTF8("🧪");
    }
    if (value < 3.0) {
        return wxString::FromUTF8("🪙");
    }
    if (value < 4.0) {
        return wxString::FromUTF8("💵");
    }
    return wxString::FromUTF8("💰");
}

wxString scoreWithEmoji(const wxString &emoji, double value) {
    return emoji + " " + wxString::FromUTF8(formatDouble(value).c_str());
}
