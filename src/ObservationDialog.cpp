#include "ObservationDialog.h"

#include "AppConfig.h"
#include "AppIcon.h"
#include "AuxGuiProcess.h"
#include "ObserverFrame.h"
#include "ObservationStore.h"
#include "UiTheme.h"
#include "formatting.h"

#include <bas/locale/i18n.h>

#include <wx/button.h>
#include <wx/caret.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/fontenum.h>
#include <wx/graphics.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>
#include <wx/utils.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

namespace {

constexpr int ID_SUBMIT = wxID_OK;
constexpr int ID_SKIP = wxID_CANCEL;
constexpr int ID_SNOOZE = wxID_HIGHEST + 1;
constexpr int ID_QUIT = wxID_HIGHEST + 2;

int clampScore(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 5) {
        return 5;
    }
    return value;
}

double clampIntervalSeconds(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 86400.0) {
        return 86400.0;
    }
    return value;
}

wxString toWxUtf8(const std::string &value) { return wxString::FromUTF8(value.c_str()); }

double easeOutCubic(double t) {
    const double inverse = 1.0 - t;
    return 1.0 - inverse * inverse * inverse;
}

std::string trimmedUtf8(wxString text) {
    text.Trim(true);
    text.Trim(false);
    wxCharBuffer buffer = text.utf8_str();
    return buffer.data() != nullptr ? std::string(buffer.data()) : std::string();
}

std::string activityForEdit(const std::string &activity) {
    std::string result;
    result.reserve(activity.size());
    for (char ch : activity) {
        if (ch == '\n' || ch == '\r') {
            if (!result.empty() && result.back() != ' ') {
                result.push_back(' ');
            }
            continue;
        }
        result.push_back(ch);
    }
    return result;
}

double scoreShortcutDelta(const wxKeyEvent &event, double baseDelta) {
    double magnitude = 0.5;
    if (event.AltDown()) {
        magnitude = 5.0;
    } else if (event.ControlDown() || event.CmdDown()) {
        magnitude = 1.0;
    }
    double delta = magnitude;
    if (baseDelta < 0.0) {
        delta = -delta;
    }
    if (event.ShiftDown()) {
        delta = -delta;
    }
    return delta;
}

wxString resolveChineseSerifFace() {
    static const wxString kCandidates[] = {
        wxString::FromUTF8("报宋"),
        "FZBaSongS",
        "BaoSong",
        wxString::FromUTF8("仿宋"),
        "FangSong",
        "STFangsong",
        "FZFangSong-Z02",
        wxString::FromUTF8("宋体"),
        "SimSun",
        "STSong",
        "Songti SC",
        "Noto Serif CJK SC",
        "Source Han Serif SC",
        "AR PL UMing CN",
    };

    static wxString cached;
    static bool resolved = false;
    if (resolved) {
        return cached;
    }
    resolved = true;

    wxFontEnumerator enumerator;
    enumerator.EnumerateFacenames(wxFONTENCODING_SYSTEM, wxFONTWEIGHT_NORMAL);
    const wxArrayString &available = enumerator.GetFacenames();
    for (const wxString &candidate : kCandidates) {
        for (const wxString &face : available) {
            if (face.IsSameAs(candidate, false)) {
                cached = face;
                return cached;
            }
        }
    }
    for (const wxString &candidate : kCandidates) {
        for (const wxString &face : available) {
            if (face.Lower().Contains(candidate.Lower()) ||
                candidate.Lower().Contains(face.Lower())) {
                cached = face;
                return cached;
            }
        }
    }
    return cached;
}

wxFont chineseSerifFont(const wxFont &base) {
    wxFont font = base;
    font.SetFamily(wxFONTFAMILY_ROMAN);
    font.SetWeight(wxFONTWEIGHT_NORMAL);
    font.SetStyle(wxFONTSTYLE_NORMAL);
    const wxString face = resolveChineseSerifFace();
    if (!face.empty()) {
        font.SetFaceName(face);
    }
    return font;
}

std::string urlEncodeUtf8(const wxString &text) {
    wxCharBuffer buffer = text.utf8_str();
    if (buffer.data() == nullptr) {
        return "";
    }

    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(buffer.data());
         *cursor != '\0'; ++cursor) {
        const unsigned char byte = *cursor;
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' ||
            byte == '~') {
            out << std::nouppercase << std::dec << static_cast<char>(byte);
            out << std::uppercase << std::hex << std::setfill('0');
        } else {
            out << '%' << std::setw(2) << static_cast<int>(byte);
        }
    }
    return out.str();
}

std::string effectiveAppLocale() {
    if (!appConfig().locale.empty()) {
        return appConfig().locale;
    }
    const char *language = std::getenv("LANGUAGE");
    if (language != nullptr && *language != '\0') {
        std::string value = language;
        const size_t colon = value.find(':');
        if (colon != std::string::npos) {
            value = value.substr(0, colon);
        }
        return value;
    }
    return "en";
}

std::string googleSearchHlTag(const std::string &locale) {
    std::string hl = locale.empty() ? "en" : locale;
    for (char &ch : hl) {
        if (ch == '_') {
            ch = '-';
        }
    }
    return hl;
}

std::string jsonEscapeUtf8(const wxString &text) {
    wxCharBuffer buffer = text.utf8_str();
    if (buffer.data() == nullptr) {
        return "";
    }

    std::ostringstream out;
    for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(buffer.data());
         *cursor != '\0'; ++cursor) {
        const unsigned char byte = *cursor;
        switch (byte) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (byte < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(byte) << std::dec;
            } else {
                out << static_cast<char>(byte);
            }
            break;
        }
    }
    return out.str();
}

std::string buildDoubaoChatUrl(const wxString &question) {
    const std::string action = "{\"pluginId\":\"Send_Message\",\"payload\":{\"text\":\"" +
                               jsonEscapeUtf8(question) + "\"}}";
    return "https://www.doubao.com/chat/url-action?action=" +
           urlEncodeUtf8(wxString::FromUTF8(action.c_str()));
}

bool isSimplifiedChineseLocale(const std::string &locale) {
    return locale == "zh_CN" || locale.rfind("zh_CN.", 0) == 0;
}

void openQuoteAiAssistant(const std::string &quote) {
    if (trim(quote).empty()) {
        return;
    }

    const wxString prompt =
        wxString::Format(wxString::FromUTF8(_("Explain in depth: %s (answer in English)")),
                         wxString::FromUTF8(quote.c_str()));
    const std::string encoded = urlEncodeUtf8(prompt);
    const std::string locale = effectiveAppLocale();
    if (isSimplifiedChineseLocale(locale)) {
        wxTheApp->CallAfter([prompt]() {
            if (launchAuxGuiDeepSeek(prompt)) {
                return;
            }
            wxLaunchDefaultBrowser(wxString::FromUTF8(buildDoubaoChatUrl(prompt).c_str()));
        });
        return;
    }

    const std::string hl = googleSearchHlTag(locale);
    const wxString url = wxString::Format("https://www.google.com/search?udm=50&hl=%s&q=%s",
                                          wxString::FromUTF8(hl.c_str()).c_str(), encoded.c_str());
    wxLaunchDefaultBrowser(url);
}

} // namespace

namespace {

constexpr int kShadowSize = 20;
constexpr int kCornerRadius = 12;

class TransparentPanel : public wxPanel {
  public:
    explicit TransparentPanel(wxWindow *parent) : wxPanel() {
        SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
        Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    }
};

} // namespace

class QuoteCanvas : public wxPanel {
  public:
    QuoteCanvas(wxWindow *parent, const std::string &quote, const UiThemeColors &colors,
                std::function<void()> onClick, std::function<void()> onLayoutChanged)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(kBaseWidth, kBaseHeight)),
          m_colors(colors), m_quote(toWxUtf8(quote)), m_onClick(std::move(onClick)),
          m_onLayoutChanged(std::move(onLayoutChanged)) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(m_colors.quoteBg);
        SetCursor(wxCursor(wxCURSOR_HAND));
        SetToolTip(wxString::FromUTF8(_("Right-click to ask AI")));
        Bind(wxEVT_PAINT, &QuoteCanvas::onPaint, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
            if (m_onClick) {
                m_onClick();
            }
        });
        Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent &) {
            wxCharBuffer buffer = m_quote.utf8_str();
            if (buffer.data() != nullptr) {
                openQuoteAiAssistant(buffer.data());
            }
        });
        Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
            Refresh();
            event.Skip();
        });
    }

    void relayoutQuote() {
        updateLayout();
        Refresh();
    }

    void setQuote(const std::string &quote) {
        m_quote = toWxUtf8(quote);
        relayoutQuote();
    }

    struct LayoutMetrics {
        int layoutWidth = 0;
        int layoutHeight = 0;
        int lineHeight = 0;
        int padX = 0;
        int padY = 0;
        int lineCount = 0;
        double aspectRatio = 0.0;
        std::vector<std::string> lines;
    };

    LayoutMetrics layoutMetrics() const {
        LayoutMetrics metrics;
        metrics.layoutWidth = m_layoutWidth;
        metrics.layoutHeight = m_layoutHeight;
        metrics.lineHeight = m_lineHeight;
        metrics.padX = m_padX;
        metrics.padY = m_padY;
        metrics.lineCount = static_cast<int>(m_layoutLines.size());
        metrics.aspectRatio =
            m_layoutHeight > 0 ? static_cast<double>(m_layoutWidth) / m_layoutHeight : 0.0;
        metrics.lines.reserve(m_layoutLines.size());
        for (const wxString &line : m_layoutLines) {
            metrics.lines.push_back(line.ToUTF8().data());
        }
        return metrics;
    }

  private:
    static constexpr int kBaseWidth = 720;
    static constexpr int kBaseHeight = 150;
    static constexpr int kBaseFontExtra = 16;
    static constexpr int kMaxQuoteLines = 4;

    static int measureLineHeight(wxDC &dc) {
        int lineHeight = 0;
        dc.GetTextExtent("M", nullptr, &lineHeight);
        return lineHeight + lineHeight / 4;
    }

    static int measureTextWidth(wxDC &dc, const wxString &text) {
        int width = 0;
        int height = 0;
        dc.GetTextExtent(text, &width, &height);
        return width;
    }

    static int maxLineTextWidth(wxDC &dc, const std::vector<wxString> &lines) {
        int maxWidth = 0;
        for (const wxString &line : lines) {
            maxWidth = std::max(maxWidth, measureTextWidth(dc, line));
        }
        return maxWidth;
    }

    void setupDrawFont(wxDC &dc) {
        m_drawFont = chineseSerifFont(GetFont());
        m_drawFont.SetPointSize(m_drawFont.GetPointSize() + kBaseFontExtra);
        dc.SetFont(m_drawFont);
        m_lineHeight = measureLineHeight(dc);
    }

    static bool isClosingPunctuation(wxUniChar ch) {
        const wxUint32 value = ch.GetValue();
        return value == '.' || value == ',' || value == ';' || value == ':' || value == '!' ||
               value == '?' || value == ')' || value == ']' || value == '}' || value == 0x3002 ||
               value == 0xff0c || value == 0xff1b || value == 0xff1a || value == 0xff01 ||
               value == 0xff1f || value == 0x3001 || value == 0x300d || value == 0x300f ||
               value == 0x3011 || value == 0xff09 || value == 0x201d || value == 0x2019;
    }

    static bool isPreferredBreakAfter(wxUniChar ch) {
        return isClosingPunctuation(ch) || ch == ' ' || ch == '\t';
    }

    static std::vector<int> prefixTextWidths(wxDC &dc, const wxString &text) {
        std::vector<int> prefix(text.length() + 1, 0);
        for (std::size_t i = 0; i < text.length(); ++i) {
            prefix[i + 1] = prefix[i] + measureTextWidth(dc, text.SubString(i, i));
        }
        return prefix;
    }

    static wxString extractLine(const wxString &text, std::size_t start, std::size_t endInclusive) {
        while (start < text.length() && start <= endInclusive && isWhitespace(text[start])) {
            ++start;
        }
        if (start > endInclusive || start >= text.length()) {
            return wxString();
        }
        wxString line = text.SubString(start, endInclusive);
        line.Trim(true);
        return line;
    }

    static std::optional<std::vector<wxString>>
    wrapEvenAtPunctuation(wxDC &dc, const wxString &text, int lineCount, int maxTextWidth) {
        if (lineCount <= 0) {
            return std::nullopt;
        }
        if (lineCount == 1) {
            if (measureTextWidth(dc, text) <= maxTextWidth) {
                return std::vector<wxString>{text};
            }
            return std::nullopt;
        }

        const std::size_t len = text.length();
        if (len == 0) {
            return std::vector<wxString>{wxString()};
        }

        const std::vector<int> prefix = prefixTextWidths(dc, text);
        const int totalWidth = prefix[len];
        std::vector<std::size_t> breaks;
        breaks.reserve(static_cast<std::size_t>(lineCount - 1));

        for (int k = 1; k < lineCount; ++k) {
            const int targetWidth = totalWidth * k / lineCount;
            const std::size_t minAfter = breaks.empty() ? 0 : breaks.back() + 1;
            const std::size_t minRemaining = static_cast<std::size_t>(lineCount - k);
            if (len < minRemaining) {
                return std::nullopt;
            }
            const std::size_t maxIdx = len - minRemaining;

            std::size_t best = minAfter;
            int bestScore = INT_MAX;
            for (std::size_t end = minAfter; end <= maxIdx; ++end) {
                const int widthAtBreak = prefix[end + 1];
                int score = std::abs(widthAtBreak - targetWidth);
                if (!isPreferredBreakAfter(text[end])) {
                    score += 10000;
                }
                if (score < bestScore) {
                    bestScore = score;
                    best = end;
                }
            }
            breaks.push_back(best);
        }

        std::vector<wxString> lines;
        lines.reserve(static_cast<std::size_t>(lineCount));
        std::size_t start = 0;
        for (std::size_t end : breaks) {
            const wxString line = extractLine(text, start, end);
            if (line.empty()) {
                return std::nullopt;
            }
            if (measureTextWidth(dc, line) > maxTextWidth) {
                return std::nullopt;
            }
            lines.push_back(line);
            start = end + 1;
        }
        const wxString lastLine = extractLine(text, start, len - 1);
        if (lastLine.empty() || measureTextWidth(dc, lastLine) > maxTextWidth) {
            return std::nullopt;
        }
        lines.push_back(lastLine);
        return lines;
    }

    void layoutQuote(wxDC &dc) {
        setupDrawFont(dc);
        const int maxTextWidth = kBaseWidth - 2 * m_padX;

        m_layoutLines.clear();
        if (measureTextWidth(dc, m_quote) <= maxTextWidth) {
            m_layoutLines.push_back(m_quote);
        } else {
            for (int lineCount = 2; lineCount <= kMaxQuoteLines; ++lineCount) {
                if (std::optional<std::vector<wxString>> lines =
                        wrapEvenAtPunctuation(dc, m_quote, lineCount, maxTextWidth)) {
                    m_layoutLines = std::move(*lines);
                    break;
                }
            }
            if (m_layoutLines.empty()) {
                m_layoutLines = wrapText(dc, m_quote, maxTextWidth);
            }
        }

        m_layoutWidth = maxLineTextWidth(dc, m_layoutLines) + 2 * m_padX;
        m_layoutHeight = static_cast<int>(m_layoutLines.size()) * m_lineHeight + 2 * m_padY;
    }

    void updateLayout() {
        wxClientDC dc(this);
        int emWidth = 12;
        int emHeight = 20;
        GetTextExtent("M", &emWidth, &emHeight);
        m_padX = emWidth * 3 / 2;
        m_padY = emHeight;

        layoutQuote(dc);

        SetMinSize(wxSize(m_layoutWidth, m_layoutHeight));
        if (m_onLayoutChanged) {
            m_onLayoutChanged();
        }
    }
    static bool isWhitespace(wxUniChar ch) {
        const wxUint32 value = ch.GetValue();
        return value == ' ' || value == '\t' || value == '\n' || value == '\r';
    }

    static bool isCjk(wxUniChar ch) {
        const wxUint32 value = ch.GetValue();
        return (value >= 0x3400 && value <= 0x4dbf) || (value >= 0x4e00 && value <= 0x9fff) ||
               (value >= 0xf900 && value <= 0xfaff) || (value >= 0x3040 && value <= 0x30ff) ||
               (value >= 0xac00 && value <= 0xd7af);
    }

    static bool isWordChar(wxUniChar ch) {
        const wxUint32 value = ch.GetValue();
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
               (value >= '0' && value <= '9') || value == '\'' || value == '-';
    }

    static wxString trimRight(wxString value) {
        value.Trim(true);
        return value;
    }

    static wxString trimLeft(wxString value) {
        value.Trim(false);
        return value;
    }

    std::vector<wxString> tokenize(const wxString &text) {
        std::vector<wxString> tokens;
        for (std::size_t i = 0; i < text.length();) {
            wxUniChar ch = text[i];
            if (isWhitespace(ch)) {
                if (!tokens.empty() && tokens.back() != " ") {
                    tokens.push_back(" ");
                }
                ++i;
                continue;
            }

            if (isWordChar(ch)) {
                wxString token;
                while (i < text.length() && isWordChar(text[i])) {
                    token += text[i];
                    ++i;
                }
                tokens.push_back(token);
                continue;
            }

            wxString token;
            token += ch;
            tokens.push_back(token);
            ++i;
        }
        return tokens;
    }

    bool fits(wxDC &dc, const wxString &text, int maxWidth) {
        int width = 0;
        int height = 0;
        dc.GetTextExtent(text, &width, &height);
        return width <= maxWidth;
    }

    void pushLine(std::vector<wxString> &lines, wxString &current) {
        current = trimRight(current);
        if (!current.empty()) {
            lines.push_back(current);
        }
        current.clear();
    }

    std::vector<wxString> wrapText(wxDC &dc, const wxString &text, int maxWidth) {
        std::vector<wxString> lines;
        wxString current;
        for (const wxString &token : tokenize(text)) {
            wxString candidate = current + token;
            if (fits(dc, candidate, maxWidth)) {
                current = candidate;
                continue;
            }

            const bool closingPunctuation = token.length() == 1 && isClosingPunctuation(token[0]);
            if (closingPunctuation) {
                if (!current.empty()) {
                    current += token;
                } else if (!lines.empty()) {
                    lines.back() += token;
                } else {
                    current = token;
                }
                continue;
            }

            if (!current.empty()) {
                pushLine(lines, current);
                current = trimLeft(token);
            } else {
                for (std::size_t i = 0; i < token.length(); ++i) {
                    wxString charCandidate = current + token[i];
                    if (!current.empty() && !fits(dc, charCandidate, maxWidth)) {
                        pushLine(lines, current);
                    }
                    current += token[i];
                }
            }
        }
        if (!current.empty()) {
            pushLine(lines, current);
        }
        if (lines.empty()) {
            lines.push_back("");
        }
        return lines;
    }

    void drawWrapped(wxDC &dc, const std::vector<wxString> &lines, const wxRect &rect,
                     int lineHeight) const {
        int y = rect.GetTop();
        for (const wxString &line : lines) {
            int width = 0;
            int height = 0;
            dc.GetTextExtent(line, &width, &height);
            const int x = rect.GetLeft() + std::max(0, (rect.GetWidth() - width) / 2);
            dc.DrawText(line, x, y);
            y += lineHeight;
        }
    }

    void onPaint(wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(this);
        const wxSize size = GetClientSize();

        dc.SetBackground(wxBrush(m_colors.quoteBg));
        dc.Clear();

        dc.SetFont(m_drawFont);

        wxRect textShadowRect(m_padX + 3, m_padY + 3, size.GetWidth() - m_padX * 2,
                              size.GetHeight() - m_padY * 2);
        wxRect textGlowRect(m_padX + 1, m_padY + 1, size.GetWidth() - m_padX * 2,
                            size.GetHeight() - m_padY * 2);
        const wxRect textRect(m_padX, m_padY, size.GetWidth() - m_padX * 2,
                              size.GetHeight() - m_padY * 2);
        dc.SetTextForeground(m_colors.quoteShadow);
        drawWrapped(dc, m_layoutLines, textShadowRect, m_lineHeight);
        dc.SetTextForeground(m_colors.quoteGlow);
        drawWrapped(dc, m_layoutLines, textGlowRect, m_lineHeight);
        dc.SetTextForeground(m_colors.quoteText);
        drawWrapped(dc, m_layoutLines, textRect, m_lineHeight);
    }

    UiThemeColors m_colors;
    wxString m_quote;
    std::function<void()> m_onClick;
    std::function<void()> m_onLayoutChanged;
    std::vector<wxString> m_layoutLines;
    wxFont m_drawFont;
    int m_layoutWidth = kBaseWidth;
    int m_layoutHeight = kBaseHeight;
    int m_lineHeight = 20;
    int m_padX = 18;
    int m_padY = 20;
};

class RatingControl : public wxPanel {
  public:
    RatingControl(wxWindow *parent, const std::vector<wxString> &emojis,
                  const wxColour &emojiColour, const wxString &label, double value,
                  const UiThemeColors &colors)
        : wxPanel(parent, wxID_ANY), m_emojis(emojis), m_emojiColour(emojiColour), m_label(label),
          m_colors(colors), m_committedLine(lineFromScore(value)), m_hoverLine(m_committedLine) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(m_colors.panelBg);
        Bind(wxEVT_PAINT, &RatingControl::onPaint, this);
        Bind(wxEVT_SIZE, &RatingControl::onSize, this);
        Bind(wxEVT_MOTION, &RatingControl::onMotion, this);
        Bind(wxEVT_LEFT_DOWN, &RatingControl::onLeftDown, this);
        Bind(wxEVT_LEAVE_WINDOW, &RatingControl::onLeave, this);
        refreshLayoutMetrics();
    }

    double value() const { return m_committedLine; }

    void adjust(double delta) {
        m_committedLine =
            std::max(0.0, std::min(5.0, std::round((m_committedLine + delta) * 2.0) / 2.0));
        m_hoverLine = m_committedLine;
        m_hoverActive = false;
        Refresh();
    }

  private:
    struct LayoutMetrics {
        wxSize minSize;
        int emojiY = 0;
        int labelY = 0;
        int labelH = 0;
        wxRect ratingRect;
    };

    wxString currentEmoji(double value) const {
        if (m_emojis.empty()) {
            return "";
        }
        const int index =
            std::max(0, std::min(static_cast<int>(m_emojis.size()) - 1,
                                 static_cast<int>(std::round(
                                     (value / 5.0) * (static_cast<int>(m_emojis.size()) - 1)))));
        return m_emojis[index];
    }

    static double lineFromScore(double value) {
        if (value < 0.0) {
            return 0.0;
        }
        if (value > 5.0) {
            return 5.0;
        }
        return value;
    }

    wxFont emojiFont() const {
        wxFont font = GetFont();
        font.SetPointSize(std::min(font.GetPointSize() + 8, 30));
        font.SetFaceName("Noto Color Emoji");
        return font;
    }

    wxFont labelFont() const { return chineseSerifFont(GetFont()); }

    LayoutMetrics measureLayout(wxDC &dc, int width) const {
        constexpr int padX = 20;
        constexpr int padTop = 10;
        constexpr int padBottom = 10;
        constexpr int gap = 8;
        constexpr int ratingH = 30;

        dc.SetFont(emojiFont());
        wxCoord emojiW = 0;
        wxCoord emojiH = 0;
        dc.GetTextExtent(currentEmoji(m_committedLine), &emojiW, &emojiH);

        dc.SetFont(labelFont());
        wxCoord labelW = 0;
        wxCoord labelH = 0;
        dc.GetTextExtent(m_label, &labelW, &labelH);

        const int contentWidth = std::max({emojiW, labelW, 120}) + padX * 2;
        const int panelWidth = std::max(200, std::max(width, contentWidth));

        int y = padTop;
        const int emojiY = y;
        y += emojiH + gap;
        const int labelY = y;
        y += labelH + gap;
        const int ratingY = y;
        y += ratingH + padBottom;

        LayoutMetrics metrics;
        metrics.minSize = wxSize(panelWidth, y);
        metrics.emojiY = emojiY;
        metrics.labelY = labelY;
        metrics.labelH = labelH;
        metrics.ratingRect = wxRect(padX, ratingY, panelWidth - padX * 2, ratingH);
        return metrics;
    }

    void refreshLayoutMetrics() {
        wxClientDC dc(this);
        const LayoutMetrics metrics = measureLayout(dc, GetClientSize().GetWidth());
        m_emojiY = metrics.emojiY;
        m_labelY = metrics.labelY;
        m_labelH = metrics.labelH;
        m_ratingRect = metrics.ratingRect;
        SetMinSize(metrics.minSize);
        if (GetClientSize().GetHeight() < metrics.minSize.GetHeight()) {
            SetSize(metrics.minSize);
        }
    }

    void onSize(wxSizeEvent &event) {
        event.Skip();
        refreshLayoutMetrics();
        Refresh();
    }

    wxRect ratingRect() const { return m_ratingRect; }

    double lineFromX(int x) const {
        const wxRect rect = ratingRect();
        double ratio =
            static_cast<double>(x - rect.GetLeft()) / static_cast<double>(rect.GetWidth());
        if (ratio < 0.0) {
            ratio = 0.0;
        }
        if (ratio > 1.0) {
            ratio = 1.0;
        }
        return ratio * 5.0;
    }

    int xFromLine(double line) const {
        const wxRect rect = ratingRect();
        return rect.GetLeft() + static_cast<int>((line / 5.0) * rect.GetWidth());
    }

    void onMotion(wxMouseEvent &event) {
        m_hoverLine = lineFromX(event.GetX());
        m_hoverActive = true;
        Refresh();
    }

    void onLeftDown(wxMouseEvent &event) {
        m_committedLine = lineFromX(event.GetX());
        m_hoverLine = m_committedLine;
        m_hoverActive = true;
        Refresh();
    }

    void onLeave(wxMouseEvent &) {
        m_hoverActive = false;
        Refresh();
    }

    void drawCirclePart(wxDC &dc, const wxRect &circle, const wxColour &colour,
                        const wxRect &clip) {
        wxRect clipped = circle.Intersect(clip);
        if (clipped.IsEmpty()) {
            return;
        }
        dc.SetClippingRegion(clipped);
        dc.SetBrush(wxBrush(colour));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawEllipse(circle);
        dc.DestroyClippingRegion();
    }

    void onPaint(wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(this);
        const wxSize size = GetClientSize();
        dc.SetBackground(wxBrush(m_colors.panelBg));
        dc.Clear();

        const LayoutMetrics metrics = measureLayout(dc, size.GetWidth());
        m_emojiY = metrics.emojiY;
        m_labelY = metrics.labelY;
        m_labelH = metrics.labelH;
        m_ratingRect = metrics.ratingRect;

        dc.SetFont(emojiFont());
        dc.SetTextForeground(m_emojiColour);
        const double visibleLine = m_hoverActive ? m_hoverLine : m_committedLine;
        const wxString emoji = currentEmoji(visibleLine);
        wxCoord emojiW = 0;
        wxCoord emojiH = 0;
        dc.GetTextExtent(emoji, &emojiW, &emojiH);
        const int emojiX = std::max(0, (size.GetWidth() - emojiW) / 2);
        dc.DrawText(emoji, emojiX, m_emojiY);

        dc.SetFont(labelFont());
        dc.SetTextForeground(m_colors.ratingLabel);
        dc.DrawLabel(m_label, wxRect(0, m_labelY, size.GetWidth(), m_labelH), wxALIGN_CENTER);

        const wxRect rect = m_ratingRect;
        const int radius = 12;
        const double activeLine = m_hoverActive ? m_hoverLine : m_committedLine;
        const int lineX = xFromLine(activeLine);
        const int middleX = xFromLine(2.5);
        const int fillLeft = std::min(lineX, middleX);
        const int fillRight = std::max(lineX, middleX);
        const wxRect fillClip(fillLeft, rect.GetTop() - 4, fillRight - fillLeft,
                              rect.GetHeight() + 8);
        const wxColour fillColour =
            lineX < middleX ? wxColour(232, 73, 73) : wxColour(247, 197, 72);

        const wxColour knobFill = m_colors.ratingKnobFill;
        const wxColour knobRing = m_colors.ratingKnobRing;
        const wxColour trackColour = m_colors.ratingTrack;

        dc.SetPen(wxPen(trackColour, 1));
        dc.DrawLine(rect.GetLeft(), rect.GetTop() + rect.GetHeight() / 2, rect.GetRight(),
                    rect.GetTop() + rect.GetHeight() / 2);

        for (int i = 0; i < 5; ++i) {
            const int centerX =
                rect.GetLeft() + static_cast<int>((i + 0.5) * rect.GetWidth() / 5.0);
            const int centerY = rect.GetTop() + rect.GetHeight() / 2;
            wxRect circle(centerX - radius, centerY - radius, radius * 2, radius * 2);

            dc.SetPen(wxPen(knobRing, 1));
            dc.SetBrush(wxBrush(knobFill));
            dc.DrawEllipse(circle);
            drawCirclePart(dc, circle, fillColour, fillClip);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(knobRing, 1));
            dc.DrawEllipse(circle);
        }

        const wxColour markerColour =
            m_hoverActive ? m_colors.ratingMarkerActive : m_colors.ratingMarkerInactive;
        dc.SetPen(wxPen(markerColour, m_hoverActive ? 2 : 1));
        dc.DrawLine(lineX, rect.GetTop() - 7, lineX, rect.GetBottom() + 7);
    }

    std::vector<wxString> m_emojis;
    wxColour m_emojiColour;
    wxString m_label;
    UiThemeColors m_colors;
    double m_committedLine;
    double m_hoverLine;
    bool m_hoverActive = false;
    int m_emojiY = 0;
    int m_labelY = 0;
    int m_labelH = 0;
    wxRect m_ratingRect;
};

ObservationDialog::ObservationDialog(wxWindow *parent, const ObservePromptDefaults &defaults)
    : wxDialog(), m_quote(defaults.editing.has_value() && !defaults.editing->quote.empty()
                              ? defaults.editing->quote
                              : defaults.quote),
      m_promptedAt(defaults.editing.has_value() ? defaults.editing->promptedAt
                                                : currentTimestamp()),
      m_editing(defaults.editing), m_editMode(defaults.editing.has_value()),
      m_store(defaults.store),
      m_quoteRng(static_cast<std::mt19937::result_type>(
          std::chrono::high_resolution_clock::now().time_since_epoch().count())) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Create(parent, wxID_ANY,
           defaults.editing.has_value() ? wxString::FromUTF8(_("Edit record"))
                                        : wxString("Observer"),
           wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxSTAY_ON_TOP);

    SetTransparent(255);
    
    const UiThemeColors &colors = uiThemeColors(defaults.theme);
    SetBackgroundColour(wxColour(0, 0, 0, 0));
    SetForegroundColour(colors.windowFg);
    m_chromeBg = colors.windowBg;
    m_borderColour = colors.border;
    m_shadowColour = colors.windowShadow;
    Bind(wxEVT_PAINT, &ObservationDialog::paintWindowChrome, this);

    m_contentPanel = new TransparentPanel(this);
    auto *root = new wxBoxSizer(wxVERTICAL);
    m_theme = defaults.theme;
    m_quotes = defaults.quotes;
    m_store = defaults.store;
    m_weekStartsMonday = defaults.weekStartsMonday;
    m_quoteIndex = defaults.quoteIndex;
    const wxIcon appIcon = observerAppIcon(32);
    if (appIcon.IsOk()) {
        SetIcon(appIcon);
    }
    if (m_quotes.empty()) {
        m_quotes.push_back(m_quote);
        m_quoteIndex = 0;
    } else if (m_quoteIndex >= m_quotes.size()) {
        m_quoteIndex = 0;
        m_quote = m_quotes[m_quoteIndex];
    }

    wxFont baseFont = GetFont();
    baseFont.SetPointSize(baseFont.GetPointSize() + 3);
    SetFont(baseFont);
    m_contentPanel->SetFont(baseFont);

    int emWidth = 12;
    int emHeight = 20;
    GetTextExtent("M", &emWidth, &emHeight);
    const int outerMargin = std::max(10, emWidth);
    const int blockGap = std::max(8, emHeight / 2);

    root->AddSpacer(blockGap);
    m_quoteCanvas = new QuoteCanvas(
        m_contentPanel, m_quote, colors, [this]() { showRandomQuote(); },
        [this]() { refitDialogLayout(); });
    root->Add(m_quoteCanvas, 0, wxLEFT | wxRIGHT | wxEXPAND, outerMargin);

    auto *ratingRow = new wxBoxSizer(wxHORIZONTAL);
    m_energyRating = new RatingControl(m_contentPanel,
                                       {wxString::FromUTF8("⏻"), wxString::FromUTF8("🪫"),
                                        wxString::FromUTF8("🔋"), wxString::FromUTF8("⚡")},
                                       wxColour(255, 112, 190), wxString::FromUTF8(_("energy")),
                                       defaults.energy, colors);
    m_moodRating = new RatingControl(
        m_contentPanel,
        {wxString::FromUTF8("😢"), wxString::FromUTF8("🙁"), wxString::FromUTF8("😐"),
         wxString::FromUTF8("🙂"), wxString::FromUTF8("😄")},
        wxColour(255, 205, 72), wxString::FromUTF8(_("mood")), defaults.mood, colors);
    m_groundingRating = new RatingControl(
        m_contentPanel,
        {wxString::FromUTF8("🔬"), wxString::FromUTF8("🧪"), wxString::FromUTF8("🪙"),
         wxString::FromUTF8("💵"), wxString::FromUTF8("💰")},
        wxColour(91, 214, 123), wxString::FromUTF8(_("grounding")), defaults.grounding, colors);
    ratingRow->Add(m_energyRating, 0, wxRIGHT, 22);
    ratingRow->Add(m_moodRating, 0, wxRIGHT, 22);
    ratingRow->Add(m_groundingRating, 0);
    root->Add(ratingRow, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM | wxALIGN_CENTER_HORIZONTAL,
              outerMargin);

    std::string intervalStr =
        formatIntervalValue(clampIntervalSeconds(defaults.intervalSeconds) / 60.0);
    if (!m_editMode) {
        m_intervalCtrl =
            new wxTextCtrl(m_contentPanel, wxID_ANY, intervalStr, wxDefaultPosition,
                           wxSize(emWidth * 5, -1), wxTE_PROCESS_ENTER | wxALIGN_RIGHT | wxBORDER_NONE);
        m_intervalCtrl->SetForegroundColour(colors.intervalFg);
        m_intervalCtrl->SetBackgroundColour(colors.intervalBg);
        m_intervalCtrl->SetToolTip(
            wxString::FromUTF8(_("Launch oremind again to wake immediately")));
    }

    const wxColour promptBg = colors.promptBg;
    const wxColour promptFg = colors.promptFg;
    auto *promptPanel =
        new wxPanel(m_contentPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    promptPanel->SetBackgroundColour(promptBg);
    auto *promptSizer = new wxBoxSizer(wxVERTICAL);
    const int promptMarginX = 15;
    const int promptMarginY = 0;
    const int textWidth = 680;

    wxFont inputFont = chineseSerifFont(GetFont());
    inputFont.SetPointSize(inputFont.GetPointSize() + 12);

    wxClientDC dc(promptPanel);
    dc.SetFont(inputFont);
    wxFontMetrics metrics = dc.GetFontMetrics();
    int inputLineHeight = metrics.height;
    m_activityCtrl = new wxStyledTextCtrl(promptPanel, wxID_ANY, wxDefaultPosition,
                                          wxSize(textWidth, inputLineHeight), wxBORDER_NONE);

    m_activityCtrl->StyleClearAll();
    m_activityCtrl->SetFont(inputFont);

    // 2. 【核心】绝对锁死单行（干掉所有换行行为）
    m_activityCtrl->SetWrapMode(wxSTC_WRAP_NONE); // 100% 禁止单行文本因过长而自动换行
    m_activityCtrl->SetUndoCollection(true);      // 保持撤销队列正常
    m_activityCtrl->SetMarginWidth(0, 0);
    m_activityCtrl->SetMarginWidth(1, 0);
    m_activityCtrl->SetMarginWidth(2, 0);
    m_activityCtrl->SetUseHorizontalScrollBar(false);
    m_activityCtrl->SetUseVerticalScrollBar(false);
    // 5. 设置左右水平 Padding
    m_activityCtrl->SetMarginLeft(0);
    m_activityCtrl->SetMarginRight(0);

    m_activityCtrl->SetCaretForeground(promptFg);
    m_activityCtrl->SetCaretLineVisible(false);
    // 可选：如果希望光标所在的整行背景色更亮一点，可以取消下面两行的注释
    // activityCtrl_->SetCaretLineBackground(promptBg.ChangeLightness(110));
    // activityCtrl_->SetCaretLineVisibleAlways(true);

    m_activityCtrl->StyleSetForeground(wxSTC_STYLE_DEFAULT, promptFg);
    m_activityCtrl->StyleSetBackground(wxSTC_STYLE_DEFAULT, promptBg);
    m_activityCtrl->StyleSetFont(wxSTC_STYLE_DEFAULT, inputFont);
    m_activityCtrl->StyleClearAll();

    // 最后同步底层面板的底色，防止边缘出现闪烁、撕裂或白边
    m_activityCtrl->SetBackgroundColour(promptBg);

    if (m_editMode && m_editing.has_value()) {
        m_activityCtrl->SetText(wxString::FromUTF8(activityForEdit(m_editing->activity).c_str()));
    }

    auto *promptInputRow = new wxBoxSizer(wxHORIZONTAL);
    promptInputRow->AddSpacer(promptMarginX);
    promptInputRow->Add(m_activityCtrl, 1, wxALIGN_CENTER_VERTICAL);
    promptInputRow->AddSpacer(promptMarginX);
    promptSizer->AddSpacer(promptMarginY);
    promptSizer->Add(promptInputRow, 1, wxEXPAND);
    promptSizer->AddSpacer(promptMarginY);
    promptPanel->SetSizer(promptSizer);
    promptPanel->SetMinSize(wxSize(-1, emHeight * 2 + promptMarginY * 2));

    auto *bottomBlock = new wxBoxSizer(wxVERTICAL);
    bottomBlock->Add(promptPanel, 0, wxEXPAND);
    bottomBlock->AddSpacer(std::max(4, blockGap / 2));

    auto *bottomRow = new wxBoxSizer(wxHORIZONTAL);
    wxFont hintFont = GetFont();
    hintFont.SetPointSize(std::max(5, hintFont.GetPointSize() - 4));
    hintFont.SetStyle(wxFONTSTYLE_ITALIC);
    const wxColour footerColour = colors.footerFg;
    const wxColour actionColour = colors.actionFg;
    auto makeLabel = [this, &hintFont](const wxString &text, const wxColour &colour) {
        auto *label = new wxStaticText(m_contentPanel, wxID_ANY, text);
        label->SetFont(hintFont);
        label->SetForegroundColour(colour);
        label->SetCursor(wxCursor(wxCURSOR_HAND));
        return label;
    };
    auto makeSeparator = [this, &hintFont, footerColour]() {
        auto *label = new wxStaticText(m_contentPanel, wxID_ANY, wxString::FromUTF8("·"));
        label->SetFont(hintFont);
        label->SetForegroundColour(footerColour);
        return label;
    };

    m_submitLabel = makeLabel(wxString::FromUTF8(m_editMode ? _("Enter save") : _("Enter submit")),
                              actionColour);
    m_submitLabel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { submit(); });
    m_skipLabel =
        makeLabel(wxString::FromUTF8(m_editMode ? _("Esc cancel") : _("Esc skip")), actionColour);
    m_skipLabel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { skip(); });
    wxStaticText *historyLabel = nullptr;
    if (!m_editMode) {
        historyLabel = makeLabel(wxString::FromUTF8(_("F1 History")), actionColour);
        historyLabel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { showStatistics(); });
        m_nextPromptLabel = makeLabel(wxString::FromUTF8(_("Next prompt")), footerColour);
        m_nextPromptLabel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { toggleIntervalUnit(); });
        m_intervalUnitLabel = makeLabel(wxString::FromUTF8(_("minutes later")), footerColour);
        m_intervalUnitLabel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { toggleIntervalUnit(); });
        m_intervalCtrl->SetFont(hintFont);
        m_quitLabel = makeLabel(wxString::FromUTF8(_("Ctrl+Q quit")), actionColour);
        m_quitLabel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { quit(); });
    }

    bottomRow->AddStretchSpacer(1);
    bottomRow->Add(m_submitLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottomRow->Add(makeSeparator(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottomRow->Add(m_skipLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    if (historyLabel != nullptr) {
        bottomRow->Add(makeSeparator(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        bottomRow->Add(historyLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    }
    if (!m_editMode) {
        bottomRow->Add(makeSeparator(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        bottomRow->Add(m_nextPromptLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        bottomRow->Add(m_intervalCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        bottomRow->Add(m_intervalUnitLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        bottomRow->Add(makeSeparator(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        bottomRow->Add(m_quitLabel, 0, wxALIGN_CENTER_VERTICAL);
    }
    bottomRow->SetMinSize(wxSize(-1, emHeight));

    bottomBlock->Add(bottomRow, 0, wxEXPAND);
    root->AddStretchSpacer(1);
    root->Add(bottomBlock, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, outerMargin);

    Bind(wxEVT_CHAR_HOOK, &ObservationDialog::onCharHook, this);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { skip(); });
    Bind(wxEVT_SHOW, &ObservationDialog::onFirstShow, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        Layout();
        Refresh();
        event.Skip();
    });

    auto *shell = new wxBoxSizer(wxVERTICAL);
    m_contentPanel->SetSizer(root);
    shell->Add(m_contentPanel, 1, wxEXPAND | wxALL, kShadowSize);
    SetSizer(shell);
    SetSizerAndFit(shell);
    SetMinSize(wxSize(560, 360));
    m_quoteCanvas->relayoutQuote();
    updateAnimationAnchors();
    if (m_editMode) {
        Move(m_animationFinalPosition);
    } else {
        Move(m_animationStartPosition);
    }
    m_activityCtrl->SetFocus();
}

Observation ObservationDialog::observation() const {
    Observation result{
        m_editing.has_value() ? m_editing->id : 0,
        m_promptedAt,
        m_editing.has_value() ? m_editing->submittedAt : currentTimestamp(),
        m_energyRating->value(),
        m_moodRating->value(),
        m_groundingRating->value(),
        activityText(),
        m_quote,
    };
    return result;
}

double ObservationDialog::intervalSeconds() const {
    if (m_intervalCtrl == nullptr) {
        return clampIntervalSeconds(appConfig().intervalSeconds);
    }
    double value = parseIntervalValue(m_intervalCtrl->GetValue().ToStdString());
    if (!m_intervalInSeconds) {
        value *= 60.0;
    }
    return clampIntervalSeconds(value);
}

void ObservationDialog::onCharHook(wxKeyEvent &event) {
    const int keyCode = event.GetKeyCode();
    if (keyCode == WXK_RETURN || keyCode == WXK_NUMPAD_ENTER) {
        if (!event.ShiftDown()) {
            submit();
            return;
        }
    }
    if (keyCode == WXK_F1) {
        if (!m_editMode) {
            showStatistics();
        }
        return;
    }
    if (keyCode == WXK_F2) {
        m_groundingRating->adjust(scoreShortcutDelta(event, -0.5));
        return;
    }
    if (keyCode == WXK_F3) {
        m_moodRating->adjust(scoreShortcutDelta(event, -0.5));
        return;
    }
    if (keyCode == WXK_F4) {
        m_energyRating->adjust(scoreShortcutDelta(event, -0.5));
        return;
    }
    if (keyCode == WXK_F5) {
        m_energyRating->adjust(scoreShortcutDelta(event, 0.5));
        return;
    }
    if (keyCode == WXK_F6) {
        m_moodRating->adjust(scoreShortcutDelta(event, 0.5));
        return;
    }
    if (keyCode == WXK_F7) {
        m_groundingRating->adjust(scoreShortcutDelta(event, 0.5));
        return;
    }
    if (keyCode == WXK_F8) {
        if (!m_editMode) {
            snooze();
        }
        return;
    }
    if (keyCode == WXK_ESCAPE) {
        skip();
        return;
    }
    if ((event.ControlDown() || event.CmdDown()) && (keyCode == 'Q' || keyCode == 'q')) {
        if (!m_editMode) {
            quit();
        }
        return;
    }
    event.Skip();
}

void ObservationDialog::submit() { finishWithResult(ID_SUBMIT); }

void ObservationDialog::skip() { finishWithResult(ID_SKIP); }

void ObservationDialog::snooze() { finishWithResult(ID_SNOOZE); }

void ObservationDialog::quit() { finishWithResult(ID_QUIT); }

void ObservationDialog::showStatistics() {
    if (m_statisticsOpen) {
        return;
    }

    m_statisticsOpen = true;
    ObserverFrame::sendIpcCommand("HISTORY\n");
    m_statisticsOpen = false;
    if (m_activityCtrl != nullptr) {
        m_activityCtrl->SetFocus();
    }
}

void ObservationDialog::updateAnimationAnchors() {
    int displayIndex = wxDisplay::GetFromWindow(this);
    if (displayIndex == wxNOT_FOUND) {
        displayIndex = 0;
    }
    wxDisplay display(displayIndex);
    const wxRect workArea = display.GetClientArea();
    int positionEmHeight = 18;
    GetTextExtent("M", nullptr, &positionEmHeight);
    const int margin = positionEmHeight * 2;

    Layout();
    const wxSize dialogSize = GetSize();
    int x = workArea.GetLeft() + (workArea.GetWidth() - dialogSize.GetWidth()) / 2;
    int y;
    if (dialogSize.GetHeight() >= workArea.GetHeight() - 2 * margin) {
        y = workArea.GetTop() + margin;
    } else {
        y = workArea.GetBottom() - dialogSize.GetHeight() - margin;
        y = std::max(workArea.GetTop() + margin, y);
    }

    m_animationFinalPosition = wxPoint(x, y);
    m_animationStartPosition = wxPoint(m_animationFinalPosition.x, workArea.GetBottom() + 8);
}

namespace {

constexpr double kMinWindowRatio = 1.4;
constexpr double kMaxWindowRatio = 1.618033988749895;

int ratioWidth(int height, double ratio) {
    return static_cast<int>(std::lround(static_cast<double>(height) * ratio));
}

} // namespace

wxSize ObservationDialog::goldenRatioWindowSize(const wxSize &contentMin) const {
    int width = contentMin.GetWidth();
    int height = contentMin.GetHeight();
    if (width <= 0 || height <= 0) {
        return contentMin;
    }

    const double ratio = static_cast<double>(width) / static_cast<double>(height);
    if (ratio >= kMinWindowRatio && ratio <= kMaxWindowRatio) {
        return contentMin;
    }

    if (ratio < kMinWindowRatio) {
        width = ratioWidth(height, kMinWindowRatio);
    } else {
        height = static_cast<int>(std::ceil(static_cast<double>(width) / kMaxWindowRatio));
    }

    return wxSize(width, height);
}

void ObservationDialog::onFirstShow(wxShowEvent &event) {
    if (!event.IsShown()) {
        event.Skip();
        return;
    }
    event.Skip();
    if (m_editMode || m_animationStarted) {
        return;
    }
    m_animationStarted = true;
    animateIn();
}

void ObservationDialog::paintWindowChrome(wxPaintEvent &event) {
    wxAutoBufferedPaintDC dc(this);
    const wxSize size = GetClientSize();
    if (size.x <= 0 || size.y <= 0) {
        event.Skip();
        return;
    }

    dc.Clear();

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) {
        event.Skip();
        return;
    }

    const int contentX = kShadowSize;
    const int contentY = kShadowSize;
    const int contentW = size.x - kShadowSize * 2;
    const int contentH = size.y - kShadowSize * 2;
    if (contentW <= 0 || contentH <= 0) {
        event.Skip();
        return;
    }

    gc->SetPen(*wxTRANSPARENT_PEN);
    for (int layer = 0; layer < kShadowSize; ++layer) {
        const int alpha = (kShadowSize - layer) * 2;
        gc->SetBrush(wxColour(m_shadowColour.Red(), m_shadowColour.Green(), m_shadowColour.Blue(),
                              alpha));
        gc->DrawRoundedRectangle(contentX - layer, contentY - layer, contentW + layer * 2,
                                 contentH + layer * 2, kCornerRadius + layer);
    }

    gc->SetPen(wxPen(m_borderColour, 1));
    gc->SetBrush(m_chromeBg);
    gc->DrawRoundedRectangle(contentX, contentY, contentW, contentH, kCornerRadius);

    event.Skip();
}

void ObservationDialog::refitDialogLayout() {
    Layout();
    if (wxSizer *sizer = GetSizer()) {
        const wxSize contentMin = sizer->CalcMin();
        SetClientSize(goldenRatioWindowSize(contentMin));
        Layout();
    }
    updateAnimationAnchors();
    Refresh();
    if (IsShown()) {
        Move(m_animationFinalPosition);
    }
}

void ObservationDialog::showRandomQuote() {
    if (m_quotes.empty()) {
        return;
    }
    std::uniform_int_distribution<std::size_t> dist(0, m_quotes.size() - 1);
    std::size_t nextIndex = dist(m_quoteRng);
    if (m_quotes.size() > 1 && nextIndex == m_quoteIndex) {
        nextIndex = (nextIndex + 1) % m_quotes.size();
    }
    m_quoteIndex = nextIndex;
    m_quote = m_quotes[m_quoteIndex];
    if (m_quoteCanvas != nullptr) {
        m_quoteCanvas->setQuote(m_quote);
    }
}

void ObservationDialog::toggleIntervalUnit() {
    if (m_intervalCtrl == nullptr || m_intervalUnitLabel == nullptr) {
        return;
    }

    const double seconds = intervalSeconds();
    m_intervalInSeconds = !m_intervalInSeconds;
    m_intervalCtrl->ChangeValue(
        formatIntervalValue(m_intervalInSeconds ? seconds : seconds / 60.0));
    m_intervalUnitLabel->SetLabel(
        wxString::FromUTF8(m_intervalInSeconds ? _("seconds later") : _("minutes later")));
    Layout();
}

void ObservationDialog::animateIn() {
    constexpr int Frames = 12;
    constexpr int FrameDelayMs = 8;
    for (int i = 0; i <= Frames; ++i) {
        const double t = easeOutCubic(static_cast<double>(i) / Frames);
        const int y =
            m_animationStartPosition.y +
            static_cast<int>((m_animationFinalPosition.y - m_animationStartPosition.y) * t);
        Move(m_animationFinalPosition.x, y);
        Update();
        wxYieldIfNeeded();
        std::this_thread::sleep_for(std::chrono::milliseconds(FrameDelayMs));
    }
    Move(m_animationFinalPosition);
    m_activityCtrl->SetFocus();
}

void ObservationDialog::animateOut() {
    constexpr int Frames = 10;
    constexpr int FrameDelayMs = 8;
    for (int i = 0; i <= Frames; ++i) {
        const double t = static_cast<double>(i) / Frames;
        const double eased = t * t;
        const int y =
            m_animationFinalPosition.y +
            static_cast<int>((m_animationStartPosition.y - m_animationFinalPosition.y) * eased);
        Move(m_animationFinalPosition.x, y);
        Update();
        wxYieldIfNeeded();
        std::this_thread::sleep_for(std::chrono::milliseconds(FrameDelayMs));
    }
}

void ObservationDialog::finishWithResult(int result) {
    if (m_closing) {
        return;
    }
    m_closing = true;
    animateOut();
    EndModal(result);
}

std::string ObservationDialog::activityText() const {
    return trimmedUtf8(m_activityCtrl != nullptr ? m_activityCtrl->GetText() : wxString());
}

std::string ObservationDialog::currentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream out;
    out << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S%z");
    return out.str();
}

namespace {

wxRect quoteCanvasRect(wxWindow *dialog, wxWindow *quoteCanvas) {
    if (dialog == nullptr || quoteCanvas == nullptr) {
        return {};
    }

    wxPoint pos = quoteCanvas->GetPosition();
    for (wxWindow *parent = quoteCanvas->GetParent(); parent != nullptr && parent != dialog;
         parent = parent->GetParent()) {
        pos += parent->GetPosition();
    }
    return wxRect(pos, quoteCanvas->GetSize());
}

} // namespace

ObservationLayoutSnapshot ObservationDialog::captureLayoutSnapshot() const {
    ObservationLayoutSnapshot snapshot;
    snapshot.quote = m_quote;
    const wxSize client = GetClientSize();
    snapshot.windowWidth = client.GetWidth();
    snapshot.windowHeight = client.GetHeight();

    const wxRect quote = quoteCanvasRect(const_cast<ObservationDialog *>(this), m_quoteCanvas);
    snapshot.quoteX = quote.x;
    snapshot.quoteY = quote.y;
    snapshot.quoteWidth = quote.width;
    snapshot.quoteHeight = quote.height;
    return snapshot;
}

ObservationLayoutSnapshot
ObservationDialog::captureLayoutSnapshot(const ObservePromptDefaults &defaults) {
    ObservationDialog dialog(nullptr, defaults);
    dialog.Layout();
    dialog.refitDialogLayout();
    return dialog.captureLayoutSnapshot();
}
