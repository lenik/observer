#ifndef UI_THEME_H
#define UI_THEME_H

#include <wx/colour.h>

#include <string>

struct Hsl {
    int h = 0;
    int s = 0;
    int l = 0;

    constexpr Hsl() = default;
    constexpr Hsl(int hue, int saturation, int lightness)
        : h(hue), s(saturation), l(lightness) {}

    operator wxColour() const;
};

struct UiThemeColors {
    Hsl windowBg;
    Hsl windowFg;
    Hsl windowShadow;
    Hsl panelBg;
    Hsl surfaceBg;
    Hsl mutedFg;
    Hsl actionFg;
    Hsl footerFg;
    Hsl promptBg;
    Hsl promptFg;
    Hsl intervalBg;
    Hsl intervalFg;
    Hsl quoteBg;
    Hsl quoteShadow;
    Hsl quoteGlow;
    Hsl quoteText;
    Hsl ratingLabel;
    Hsl ratingTrack;
    Hsl ratingKnobRing;
    Hsl ratingKnobFill;
    Hsl ratingKnobCover;
    Hsl ratingMarkerInactive;
    Hsl ratingMarkerActive;
    Hsl cardBg;
    Hsl selectedBg;
    Hsl todayBg;
    Hsl grid;
    Hsl border;
    Hsl weekdayHeader;
    Hsl chartHighlight;
    Hsl listFg;
};

std::string normalizeUiTheme(const std::string &name);
bool uiThemeIsDark(const std::string &theme);
const UiThemeColors &uiThemeColors(const std::string &theme);

#endif
