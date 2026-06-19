#include "UiTheme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace {

constexpr bool isDigit(char ch) { return ch >= '0' && ch <= '9'; }

constexpr double clamp01(double value) {
    return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}

constexpr int roundToInt(double value) {
    return static_cast<int>(value >= 0.0 ? value + 0.5 : value - 0.5);
}

constexpr void skipSpacesCommas(std::string_view text, std::size_t &index) {
    while (index < text.size() && (text[index] == ' ' || text[index] == ',')) {
        ++index;
    }
}

constexpr int parseInt(std::string_view text, std::size_t &index) {
    int value = 0;
    while (index < text.size() && isDigit(text[index])) {
        value = value * 10 + (text[index] - '0');
        ++index;
    }
    return value;
}

constexpr void skipPercent(std::string_view text, std::size_t &index) {
    if (index < text.size() && text[index] == '%') {
        ++index;
    }
}

wxColour hslToWxColour(int hue, int saturation, int lightness, unsigned char alpha = 255) {
    double h = std::fmod(static_cast<double>(hue), 360.0);
    if (h < 0.0) {
        h += 360.0;
    }

    const double s = clamp01(static_cast<double>(saturation) / 100.0);
    const double l = clamp01(static_cast<double>(lightness) / 100.0);

    if (s <= 0.0) {
        const unsigned char grey = static_cast<unsigned char>(roundToInt(l * 255.0));
        return wxColour(grey, grey, grey, alpha);
    }

    const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    const double p = 2.0 * l - q;

    auto channel = [&](double offset) {
        double t = h / 360.0 + offset;
        if (t < 0.0) {
            t += 1.0;
        }
        if (t > 1.0) {
            t -= 1.0;
        }
        if (t < 1.0 / 6.0) {
            return p + (q - p) * 6.0 * t;
        }
        if (t < 1.0 / 2.0) {
            return q;
        }
        if (t < 2.0 / 3.0) {
            return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        }
        return p;
    };

    return wxColour(static_cast<unsigned char>(roundToInt(channel(1.0 / 3.0) * 255.0)),
                    static_cast<unsigned char>(roundToInt(channel(0.0) * 255.0)),
                    static_cast<unsigned char>(roundToInt(channel(-1.0 / 3.0) * 255.0)), alpha);
}

constexpr Hsl parseHslString(std::string_view text) {
    std::size_t index = 0;
    while (index < text.size() && text[index] != '(') {
        ++index;
    }
    if (index < text.size()) {
        ++index;
    }

    skipSpacesCommas(text, index);
    const int hue = parseInt(text, index);
    skipPercent(text, index);
    skipSpacesCommas(text, index);
    const int saturation = parseInt(text, index);
    skipPercent(text, index);
    skipSpacesCommas(text, index);
    const int lightness = parseInt(text, index);
    skipPercent(text, index);

    return {hue, saturation, lightness};
}

} // namespace

Hsl::operator wxColour() const { return hslToWxColour(h, s, l); }

namespace ui_theme_literals {

constexpr Hsl operator""_hsl(const char *text, std::size_t length) {
    return parseHslString(std::string_view(text, length));
}

} // namespace ui_theme_literals

using ui_theme_literals::operator""_hsl;

namespace {

const UiThemeColors kDark = {
    // #15181E #181B20 #EDEFF3 #F6CF74 #E7EFF8 - midnight, calm
    .windowBg = "hsl(220, 18%, 10%)"_hsl,
    .windowFg = "hsl(214, 20%, 94%)"_hsl,
    .windowShadow = "hsl(220, 30%, 4%)"_hsl,
    .panelBg = "hsl(220, 16%, 11%)"_hsl,
    .surfaceBg = "hsl(220, 20%, 9%)"_hsl,
    .mutedFg = "hsl(216, 12%, 65%)"_hsl,
    .actionFg = "hsl(214, 14%, 72%)"_hsl,
    .footerFg = "hsl(216, 10%, 59%)"_hsl,
    .promptBg = "hsl(220, 15%, 14%)"_hsl,
    .promptFg = "hsl(0, 0%, 100%)"_hsl,
    .intervalBg = "hsl(220, 12%, 13%)"_hsl,
    .intervalFg = "hsl(216, 8%, 69%)"_hsl,
    .quoteBg = "hsl(218, 22%, 12%)"_hsl,
    .quoteShadow = "hsl(220, 12%, 25%)"_hsl,
    .quoteGlow = "hsl(42, 88%, 71%)"_hsl,
    .quoteText = "hsl(210, 18%, 96%)"_hsl,
    .ratingLabel = "hsl(216, 14%, 63%)"_hsl,
    .ratingTrack = "hsl(222, 17%, 12%)"_hsl,
    .ratingKnobRing = "hsl(218, 16%, 13%)"_hsl,
    .ratingKnobFill = "hsl(220, 15%, 11%)"_hsl,
    .ratingKnobCover = "hsl(218, 18%, 13%)"_hsl,
    .ratingMarkerInactive = "hsl(216, 14%, 14%)"_hsl,
    .ratingMarkerActive = "hsl(214, 55%, 94%)"_hsl,
    .cardBg = "hsl(220, 14%, 13%)"_hsl,
    .selectedBg = "hsl(220, 20%, 20%)"_hsl,
    .todayBg = "hsl(36, 44%, 20%)"_hsl,
    .grid = "hsl(220, 14%, 27%)"_hsl,
    .border = "hsl(220, 14%, 33%)"_hsl,
    .weekdayHeader = "hsl(216, 10%, 55%)"_hsl,
    .chartHighlight = "hsl(214, 100%, 98%)"_hsl,
    .listFg = "hsl(214, 20%, 93%)"_hsl,
};

const UiThemeColors kLight = {
    // #F3F5F7 #FFFFFF #1B2027 #FFEED1 #49505A - clean, airy
    .windowBg = "hsl(214, 20%, 96%)"_hsl,
    .windowFg = "hsl(214, 18%, 13%)"_hsl,
    .windowShadow = "hsl(214, 22%, 38%)"_hsl,
    .panelBg = "hsl(210, 20%, 96%)"_hsl,
    .surfaceBg = "hsl(214, 20%, 99%)"_hsl,
    .mutedFg = "hsl(216, 10%, 36%)"_hsl,
    .actionFg = "hsl(216, 10%, 32%)"_hsl,
    .footerFg = "hsl(216, 10%, 41%)"_hsl,
    .promptBg = "hsl(0, 0%, 100%)"_hsl,
    .promptFg = "hsl(220, 20%, 8%)"_hsl,
    .intervalBg = "hsl(214, 16%, 94%)"_hsl,
    .intervalFg = "hsl(216, 8%, 34%)"_hsl,
    .quoteBg = "hsl(208, 24%, 94%)"_hsl,
    .quoteShadow = "hsl(216, 10%, 77%)"_hsl,
    .quoteGlow = "hsl(40, 58%, 44%)"_hsl,
    .quoteText = "hsl(214, 18%, 12%)"_hsl,
    .ratingLabel = "hsl(216, 12%, 34%)"_hsl,
    .ratingTrack = "hsl(216, 17%, 94%)"_hsl,
    .ratingKnobRing = "hsl(216, 14%, 93%)"_hsl,
    .ratingKnobFill = "hsl(212, 18%, 95%)"_hsl,
    .ratingKnobCover = "hsl(210, 22%, 97%)"_hsl,
    .ratingMarkerInactive = "hsl(204, 12%, 92%)"_hsl,
    .ratingMarkerActive = "hsl(214, 55%, 94%)"_hsl,
    .cardBg = "hsl(214, 18%, 99%)"_hsl,
    .selectedBg = "hsl(214, 50%, 94%)"_hsl,
    .todayBg = "hsl(38, 100%, 91%)"_hsl,
    .grid = "hsl(216, 12%, 86%)"_hsl,
    .border = "hsl(216, 14%, 78%)"_hsl,
    .weekdayHeader = "hsl(216, 8%, 55%)"_hsl,
    .chartHighlight = "hsl(214, 18%, 15%)"_hsl,
    .listFg = "hsl(214, 18%, 11%)"_hsl,
};

const UiThemeColors kInnocent = {
    // #FFFEFE #FFF0F5 #F3EEFA #E8F4FF #D8C4D0 - bridal white, blush, lilac, pearl blue
    .windowBg = "hsl(340, 30%, 98%)"_hsl,
    .windowFg = "hsl(280, 12%, 35%)"_hsl,
    .windowShadow = "hsl(300, 18%, 42%)"_hsl,
    .panelBg = "hsl(342, 26%, 97%)"_hsl,
    .surfaceBg = "hsl(0, 0%, 100%)"_hsl,
    .mutedFg = "hsl(300, 8%, 52%)"_hsl,
    .actionFg = "hsl(330, 25%, 58%)"_hsl,
    .footerFg = "hsl(280, 10%, 55%)"_hsl,
    .promptBg = "hsl(355, 15%, 99%)"_hsl,
    .promptFg = "hsl(280, 15%, 30%)"_hsl,
    .intervalBg = "hsl(260, 35%, 95%)"_hsl,
    .intervalFg = "hsl(270, 20%, 48%)"_hsl,
    .quoteBg = "hsl(330, 40%, 97%)"_hsl,
    .quoteShadow = "hsl(300, 15%, 88%)"_hsl,
    .quoteGlow = "hsl(210, 40%, 88%)"_hsl,
    .quoteText = "hsl(280, 12%, 32%)"_hsl,
    .ratingLabel = "hsl(300, 10%, 42%)"_hsl,
    .ratingTrack = "hsl(330, 25%, 95%)"_hsl,
    .ratingKnobRing = "hsl(320, 20%, 92%)"_hsl,
    .ratingKnobFill = "hsl(338, 28%, 96%)"_hsl,
    .ratingKnobCover = "hsl(345, 24%, 99%)"_hsl,
    .ratingMarkerInactive = "hsl(300, 18%, 93%)"_hsl,
    .ratingMarkerActive = "hsl(330, 30%, 65%)"_hsl,
    .cardBg = "hsl(350, 20%, 99%)"_hsl,
    .selectedBg = "hsl(260, 40%, 94%)"_hsl,
    .todayBg = "hsl(330, 50%, 94%)"_hsl,
    .grid = "hsl(300, 12%, 90%)"_hsl,
    .border = "hsl(320, 15%, 85%)"_hsl,
    .weekdayHeader = "hsl(270, 15%, 45%)"_hsl,
    .chartHighlight = "hsl(282, 14%, 28%)"_hsl,
    .listFg = "hsl(278, 11%, 30%)"_hsl,
};

const UiThemeColors kMaiden = {
    // #B58A8A #937D8D #D4AF37 #4A5D4E #58111A - romantic, vintage
    .windowBg = "hsl(0, 18%, 94%)"_hsl,
    .windowFg = "hsl(352, 68%, 21%)"_hsl,
    .windowShadow = "hsl(352, 35%, 32%)"_hsl,
    .panelBg = "hsl(0, 16%, 94%)"_hsl,
    .surfaceBg = "hsl(0, 10%, 98%)"_hsl,
    .mutedFg = "hsl(316, 9%, 42%)"_hsl,
    .actionFg = "hsl(0, 22%, 52%)"_hsl,
    .footerFg = "hsl(316, 8%, 46%)"_hsl,
    .promptBg = "hsl(0, 8%, 99%)"_hsl,
    .promptFg = "hsl(352, 68%, 18%)"_hsl,
    .intervalBg = "hsl(316, 12%, 90%)"_hsl,
    .intervalFg = "hsl(316, 9%, 38%)"_hsl,
    .quoteBg = "hsl(350, 14%, 92%)"_hsl,
    .quoteShadow = "hsl(0, 12%, 80%)"_hsl,
    .quoteGlow = "hsl(46, 65%, 62%)"_hsl,
    .quoteText = "hsl(352, 68%, 22%)"_hsl,
    .ratingLabel = "hsl(316, 8%, 40%)"_hsl,
    .ratingTrack = "hsl(0, 13%, 92%)"_hsl,
    .ratingKnobRing = "hsl(343, 15%, 91%)"_hsl,
    .ratingKnobFill = "hsl(355, 12%, 93%)"_hsl,
    .ratingKnobCover = "hsl(0, 10%, 96%)"_hsl,
    .ratingMarkerInactive = "hsl(350, 12%, 90%)"_hsl,
    .ratingMarkerActive = "hsl(46, 65%, 48%)"_hsl,
    .cardBg = "hsl(316, 6%, 98%)"_hsl,
    .selectedBg = "hsl(316, 15%, 88%)"_hsl,
    .todayBg = "hsl(46, 70%, 88%)"_hsl,
    .grid = "hsl(316, 8%, 82%)"_hsl,
    .border = "hsl(0, 15%, 74%)"_hsl,
    .weekdayHeader = "hsl(133, 11%, 38%)"_hsl,
    .chartHighlight = "hsl(348, 65%, 23%)"_hsl,
    .listFg = "hsl(352, 60%, 22%)"_hsl,
};

const UiThemeColors kGirl = {
    // #FFB7B2 #9FD3C7 #BCE6FF #FFE4E1 #FFD97D - youthful, vibrant
    .windowBg = "hsl(6, 100%, 94%)"_hsl,
    .windowFg = "hsl(4, 55%, 28%)"_hsl,
    .windowShadow = "hsl(330, 45%, 38%)"_hsl,
    .panelBg = "hsl(4, 85%, 92%)"_hsl,
    .surfaceBg = "hsl(0, 0%, 100%)"_hsl,
    .mutedFg = "hsl(166, 20%, 40%)"_hsl,
    .actionFg = "hsl(4, 100%, 62%)"_hsl,
    .footerFg = "hsl(166, 15%, 44%)"_hsl,
    .promptBg = "hsl(0, 0%, 99%)"_hsl,
    .promptFg = "hsl(330, 50%, 60%)"_hsl,
    .intervalBg = "hsl(202, 100%, 90%)"_hsl,
    .intervalFg = "hsl(202, 70%, 42%)"_hsl,
    .quoteBg = "hsl(330, 65%, 60%)"_hsl,
    .quoteShadow = "hsl(4, 40%, 78%)"_hsl,
    .quoteGlow = "hsl(330, 100%, 85%)"_hsl,
    .quoteText = "hsl(328, 45%, 96%)"_hsl,
    .ratingLabel = "hsl(166, 18%, 36%)"_hsl,
    .ratingTrack = "hsl(8, 60%, 92%)"_hsl,
    .ratingKnobRing = "hsl(11, 44%, 90%)"_hsl,
    .ratingKnobFill = "hsl(8, 78%, 93%)"_hsl,
    .ratingKnobCover = "hsl(5, 90%, 95%)"_hsl,
    .ratingMarkerInactive = "hsl(0, 39%, 90%)"_hsl,
    .ratingMarkerActive = "hsl(4, 100%, 68%)"_hsl,
    .cardBg = "hsl(202, 40%, 98%)"_hsl,
    .selectedBg = "hsl(202, 100%, 87%)"_hsl,
    .todayBg = "hsl(42, 100%, 84%)"_hsl,
    .grid = "hsl(166, 30%, 80%)"_hsl,
    .border = "hsl(4, 60%, 78%)"_hsl,
    .weekdayHeader = "hsl(166, 20%, 42%)"_hsl,
    .chartHighlight = "hsl(4, 48%, 32%)"_hsl,
    .listFg = "hsl(4, 55%, 24%)"_hsl,
};

const UiThemeColors kMorandi = {
    // #F2EDE8 #E8E2F0 #C8D8E8 #E8C8D0 #6E8E9A - linen, lavender, powder blue, blush, slate
    .windowBg = "hsl(35, 18%, 92%)"_hsl,
    .windowFg = "hsl(230, 12%, 28%)"_hsl,
    .windowShadow = "hsl(230, 14%, 36%)"_hsl,
    .panelBg = "hsl(38, 16%, 91%)"_hsl,
    .surfaceBg = "hsl(0, 0%, 98%)"_hsl,
    .mutedFg = "hsl(270, 12%, 48%)"_hsl,
    .actionFg = "hsl(210, 38%, 48%)"_hsl,
    .footerFg = "hsl(280, 10%, 52%)"_hsl,
    .promptBg = "hsl(0, 0%, 100%)"_hsl,
    .promptFg = "hsl(230, 12%, 22%)"_hsl,
    .intervalBg = "hsl(200, 28%, 90%)"_hsl,
    .intervalFg = "hsl(210, 42%, 42%)"_hsl,
    .quoteBg = "hsl(330, 22%, 94%)"_hsl,
    .quoteShadow = "hsl(330, 18%, 80%)"_hsl,
    .quoteGlow = "hsl(155, 28%, 68%)"_hsl,
    .quoteText = "hsl(225, 14%, 26%)"_hsl,
    .ratingLabel = "hsl(230, 10%, 38%)"_hsl,
    .ratingTrack = "hsl(270, 16%, 91%)"_hsl,
    .ratingKnobRing = "hsl(210, 30%, 86%)"_hsl,
    .ratingKnobFill = "hsl(35, 15%, 90%)"_hsl,
    .ratingKnobCover = "hsl(40, 14%, 93%)"_hsl,
    .ratingMarkerInactive = "hsl(270, 14%, 88%)"_hsl,
    .ratingMarkerActive = "hsl(210, 34%, 46%)"_hsl,
    .cardBg = "hsl(35, 10%, 97%)"_hsl,
    .selectedBg = "hsl(210, 32%, 88%)"_hsl,
    .todayBg = "hsl(330, 24%, 88%)"_hsl,
    .grid = "hsl(270, 10%, 84%)"_hsl,
    .border = "hsl(210, 22%, 78%)"_hsl,
    .weekdayHeader = "hsl(155, 18%, 42%)"_hsl,
    .chartHighlight = "hsl(210, 42%, 44%)"_hsl,
    .listFg = "hsl(232, 11%, 26%)"_hsl,
};

const UiThemeColors kGithub = {
    // #111417 #16191D #E8EBEE #348DF4 #4DCB6F - developer, focused
    .windowBg = "hsl(215, 15%, 8%)"_hsl,
    .windowFg = "hsl(210, 15%, 92%)"_hsl,
    .windowShadow = "hsl(215, 25%, 3%)"_hsl,
    .panelBg = "hsl(212, 14%, 9%)"_hsl,
    .surfaceBg = "hsl(215, 12%, 11%)"_hsl,
    .mutedFg = "hsl(210, 10%, 62%)"_hsl,
    .actionFg = "hsl(212, 90%, 58%)"_hsl,
    .footerFg = "hsl(210, 8%, 58%)"_hsl,
    .promptBg = "hsl(215, 12%, 12%)"_hsl,
    .promptFg = "hsl(210, 15%, 94%)"_hsl,
    .intervalBg = "hsl(215, 12%, 14%)"_hsl,
    .intervalFg = "hsl(212, 80%, 65%)"_hsl,
    .quoteBg = "hsl(210, 18%, 10%)"_hsl,
    .quoteShadow = "hsl(215, 10%, 20%)"_hsl,
    .quoteGlow = "hsl(136, 55%, 55%)"_hsl,
    .quoteText = "hsl(208, 12%, 93%)"_hsl,
    .ratingLabel = "hsl(210, 10%, 65%)"_hsl,
    .ratingTrack = "hsl(214, 14%, 10%)"_hsl,
    .ratingKnobRing = "hsl(210, 14%, 11%)"_hsl,
    .ratingKnobFill = "hsl(214, 14%, 9%)"_hsl,
    .ratingKnobCover = "hsl(216, 12%, 11%)"_hsl,
    .ratingMarkerInactive = "hsl(214, 12%, 12%)"_hsl,
    .ratingMarkerActive = "hsl(212, 90%, 62%)"_hsl,
    .cardBg = "hsl(217, 10%, 13%)"_hsl,
    .selectedBg = "hsl(215, 20%, 18%)"_hsl,
    .todayBg = "hsl(45, 40%, 18%)"_hsl,
    .grid = "hsl(215, 10%, 22%)"_hsl,
    .border = "hsl(215, 10%, 24%)"_hsl,
    .weekdayHeader = "hsl(210, 10%, 54%)"_hsl,
    .chartHighlight = "hsl(210, 20%, 96%)"_hsl,
    .listFg = "hsl(210, 12%, 90%)"_hsl,
};

const UiThemeColors kIos = {
    // #EFEFF4 #FFFFFF #8E8E93 #5AC8FA #007AFF - apple gray, glass, water
    .windowBg = "hsl(240, 18%, 95%)"_hsl,
    .windowFg = "hsl(240, 3%, 11%)"_hsl,
    .windowShadow = "hsl(240, 12%, 38%)"_hsl,
    .panelBg = "hsl(238, 16%, 94%)"_hsl,
    .surfaceBg = "hsl(240, 20%, 98%)"_hsl,
    .mutedFg = "hsl(240, 2%, 57%)"_hsl,
    .actionFg = "hsl(211, 100%, 50%)"_hsl,
    .footerFg = "hsl(240, 3%, 54%)"_hsl,
    .promptBg = "hsl(0, 0%, 100%)"_hsl,
    .promptFg = "hsl(238, 4%, 12%)"_hsl,
    .intervalBg = "hsl(240, 11%, 91%)"_hsl,
    .intervalFg = "hsl(205, 95%, 52%)"_hsl,
    .quoteBg = "hsl(242, 20%, 96%)"_hsl,
    .quoteShadow = "hsl(240, 5%, 79%)"_hsl,
    .quoteGlow = "hsl(199, 94%, 67%)"_hsl,
    .quoteText = "hsl(240, 4%, 13%)"_hsl,
    .ratingLabel = "hsl(240, 2%, 39%)"_hsl,
    .ratingTrack = "hsl(240, 15%, 94%)"_hsl,
    .ratingKnobRing = "hsl(240, 12%, 92%)"_hsl,
    .ratingKnobFill = "hsl(242, 14%, 93%)"_hsl,
    .ratingKnobCover = "hsl(240, 12%, 97%)"_hsl,
    .ratingMarkerInactive = "hsl(240, 14%, 93%)"_hsl,
    .ratingMarkerActive = "hsl(214, 100%, 48%)"_hsl,
    .cardBg = "hsl(242, 12%, 99%)"_hsl,
    .selectedBg = "hsl(214, 100%, 92%)"_hsl,
    .todayBg = "hsl(209, 100%, 96%)"_hsl,
    .grid = "hsl(240, 13%, 89%)"_hsl,
    .border = "hsl(240, 6%, 83%)"_hsl,
    .weekdayHeader = "hsl(240, 3%, 52%)"_hsl,
    .chartHighlight = "hsl(240, 4%, 14%)"_hsl,
    .listFg = "hsl(240, 3%, 10%)"_hsl,
};

const UiThemeColors kMsdos = {
    // #0000AA #55FFFF #FFFF55 #FF55FF #AAAAAA - dos blue, cyan, yellow, magenta, gray
    .windowBg = "hsl(240, 100%, 33%)"_hsl,
    .windowFg = "hsl(0, 0%, 100%)"_hsl,
    .windowShadow = "hsl(240, 100%, 12%)"_hsl,
    .panelBg = "hsl(240, 95%, 30%)"_hsl,
    .surfaceBg = "hsl(240, 90%, 24%)"_hsl,
    .mutedFg = "hsl(180, 100%, 75%)"_hsl,
    .actionFg = "hsl(60, 100%, 67%)"_hsl,
    .footerFg = "hsl(0, 0%, 83%)"_hsl,
    .promptBg = "hsl(240, 75%, 38%)"_hsl,
    .promptFg = "hsl(180, 20%, 98%)"_hsl,
    .intervalBg = "hsl(240, 85%, 28%)"_hsl,
    .intervalFg = "hsl(180, 95%, 68%)"_hsl,
    .quoteBg = "hsl(240, 88%, 28%)"_hsl,
    .quoteShadow = "hsl(240, 100%, 18%)"_hsl,
    .quoteGlow = "hsl(175, 90%, 68%)"_hsl,
    .quoteText = "hsl(180, 30%, 96%)"_hsl,
    .ratingLabel = "hsl(60, 90%, 72%)"_hsl,
    .ratingTrack = "hsl(240, 80%, 28%)"_hsl,
    .ratingKnobRing = "hsl(180, 60%, 45%)"_hsl,
    .ratingKnobFill = "hsl(240, 90%, 31%)"_hsl,
    .ratingKnobCover = "hsl(240, 82%, 26%)"_hsl,
    .ratingMarkerInactive = "hsl(240, 70%, 40%)"_hsl,
    .ratingMarkerActive = "hsl(55, 95%, 65%)"_hsl,
    .cardBg = "hsl(240, 85%, 26%)"_hsl,
    .selectedBg = "hsl(240, 100%, 48%)"_hsl,
    .todayBg = "hsl(300, 100%, 70%)"_hsl,
    .grid = "hsl(240, 75%, 26%)"_hsl,
    .border = "hsl(180, 80%, 65%)"_hsl,
    .weekdayHeader = "hsl(60, 95%, 70%)"_hsl,
    .chartHighlight = "hsl(180, 100%, 78%)"_hsl,
    .listFg = "hsl(0, 0%, 98%)"_hsl,
};

const UiThemeColors kWindows = {
    // #F2F2F2 #FFFFFF #005EBD #1F1F1F #0066CC - familiar, desktop
    .windowBg = "hsl(0, 0%, 95%)"_hsl,
    .windowFg = "hsl(0, 0%, 12%)"_hsl,
    .windowShadow = "hsl(210, 18%, 36%)"_hsl,
    .panelBg = "hsl(210, 8%, 94%)"_hsl,
    .surfaceBg = "hsl(0, 0%, 100%)"_hsl,
    .mutedFg = "hsl(0, 0%, 42%)"_hsl,
    .actionFg = "hsl(210, 100%, 37%)"_hsl,
    .footerFg = "hsl(0, 0%, 46%)"_hsl,
    .promptBg = "hsl(0, 0%, 99%)"_hsl,
    .promptFg = "hsl(0, 0%, 8%)"_hsl,
    .intervalBg = "hsl(210, 20%, 94%)"_hsl,
    .intervalFg = "hsl(210, 90%, 36%)"_hsl,
    .quoteBg = "hsl(0, 0%, 93%)"_hsl,
    .quoteShadow = "hsl(0, 0%, 78%)"_hsl,
    .quoteGlow = "hsl(210, 80%, 48%)"_hsl,
    .quoteText = "hsl(0, 0%, 10%)"_hsl,
    .ratingLabel = "hsl(0, 0%, 36%)"_hsl,
    .ratingTrack = "hsl(180, 3%, 93%)"_hsl,
    .ratingKnobRing = "hsl(204, 11%, 91%)"_hsl,
    .ratingKnobFill = "hsl(210, 6%, 94%)"_hsl,
    .ratingKnobCover = "hsl(0, 0%, 96%)"_hsl,
    .ratingMarkerInactive = "hsl(220, 6%, 90%)"_hsl,
    .ratingMarkerActive = "hsl(210, 100%, 40%)"_hsl,
    .cardBg = "hsl(210, 6%, 99%)"_hsl,
    .selectedBg = "hsl(210, 50%, 90%)"_hsl,
    .todayBg = "hsl(45, 80%, 88%)"_hsl,
    .grid = "hsl(0, 0%, 86%)"_hsl,
    .border = "hsl(0, 0%, 80%)"_hsl,
    .weekdayHeader = "hsl(0, 0%, 48%)"_hsl,
    .chartHighlight = "hsl(0, 0%, 14%)"_hsl,
    .listFg = "hsl(0, 0%, 7%)"_hsl,
};


const UiThemeColors &themePalette(const std::string &theme) {
    if (theme == "innocent") {
        return kInnocent;
    }
    if (theme == "maiden") {
        return kMaiden;
    }
    if (theme == "girl") {
        return kGirl;
    }
    if (theme == "morandi") {
        return kMorandi;
    }
    if (theme == "github") {
        return kGithub;
    }
    if (theme == "ios") {
        return kIos;
    }
    if (theme == "msdos") {
        return kMsdos;
    }
    if (theme == "windows") {
        return kWindows;
    }
    if (theme == "light") {
        return kLight;
    }
    return kDark;
}

} // namespace

std::string normalizeUiTheme(const std::string &name) {
    if (name.empty()) {
        return "";
    }

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (lower.rfind("windows", 0) == 0) {
        return "windows";
    }
    if (lower.rfind("msdos", 0) == 0 || lower.rfind("dos", 0) == 0) {
        return "msdos";
    }
    if (lower.rfind("github", 0) == 0) {
        return "github";
    }
    if (lower.rfind("morandi", 0) == 0) {
        return "morandi";
    }
    if (lower.rfind("ios", 0) == 0) {
        return "ios";
    }
    if (lower.rfind("innocent", 0) == 0) {
        return "innocent";
    }
    if (lower.rfind("maiden", 0) == 0) {
        return "maiden";
    }
    if (lower.rfind("girl", 0) == 0 || lower.rfind("lady", 0) == 0) {
        return "girl";
    }
    if (lower.rfind("light", 0) == 0) {
        return "light";
    }
    if (lower.rfind("dark", 0) == 0) {
        return "dark";
    }
    return "";
}

bool uiThemeIsDark(const std::string &theme) {
    const std::string normalized = normalizeUiTheme(theme);
    return normalized == "dark" || normalized == "github" || normalized == "msdos";
}

const UiThemeColors &uiThemeColors(const std::string &theme) {
    const std::string normalized = normalizeUiTheme(theme);
    if (normalized.empty()) {
        return themePalette("dark");
    }
    return themePalette(normalized);
}
