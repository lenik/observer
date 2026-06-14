#include "ObservationDialog.h"

#include "StatisticsDialog.h"

#include <bas/locale/i18n.h>

#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/caret.h>
#include <wx/display.h>
#include <wx/stc/stc.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cerrno>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <functional>
#include <thread>

namespace {

constexpr int ID_SUBMIT = wxID_OK;
constexpr int ID_SKIP = wxID_CANCEL;
constexpr int ID_SNOOZE = wxID_HIGHEST + 1;
constexpr int ID_QUIT = wxID_HIGHEST + 2;

int clampScore(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 5) {
        return 5;
    }
    return value;
}

double clampIntervalSeconds(double value)
{
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 86400.0) {
        return 86400.0;
    }
    return value;
}

wxString toWxUtf8(const std::string& value)
{
    return wxString::FromUTF8(value.c_str());
}

wxString formatIntervalValue(double value)
{
    if (value < 0.0) {
        value = 0.0;
    }

    std::ostringstream out;
    out << std::setprecision(15) << value;
    std::string text = out.str();
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    if (text.empty()) {
        text = "0";
    }
    return wxString::FromUTF8(text.c_str());
}

double parseIntervalValue(const wxString& text)
{
    wxCharBuffer buffer = text.utf8_str();
    const char* raw = buffer.data();
    if (raw == nullptr) {
        return 0.0;
    }

    char* end = nullptr;
    errno = 0;
    double value = std::strtod(raw, &end);
    if (errno != 0 || end == raw) {
        return 0.0;
    }
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') {
        ++end;
    }
    if (*end != '\0') {
        return 0.0;
    }
    return value;
}

double easeOutCubic(double t)
{
    const double inverse = 1.0 - t;
    return 1.0 - inverse * inverse * inverse;
}

std::string trimmedUtf8(wxString text)
{
    text.Trim(true);
    text.Trim(false);
    wxCharBuffer buffer = text.utf8_str();
    return buffer.data() != nullptr ? std::string(buffer.data()) : std::string();
}

}

class QuoteCanvas : public wxPanel {
public:
    QuoteCanvas(wxWindow* parent, const std::string& quote, bool darkTheme, std::function<void()> onClick)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(720, 150)),
          darkTheme_(darkTheme),
          quote_(toWxUtf8(quote)),
          onClick_(std::move(onClick))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetCursor(wxCursor(wxCURSOR_HAND));
        Bind(wxEVT_PAINT, &QuoteCanvas::onPaint, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            if (onClick_) {
                onClick_();
            }
        });
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            Refresh();
            event.Skip();
        });
    }

    void setQuote(const std::string& quote)
    {
        quote_ = toWxUtf8(quote);
        Refresh();
    }

private:
    static bool isWhitespace(wxUniChar ch)
    {
        const wxUint32 value = ch.GetValue();
        return value == ' ' || value == '\t' || value == '\n' || value == '\r';
    }

    static bool isCjk(wxUniChar ch)
    {
        const wxUint32 value = ch.GetValue();
        return (value >= 0x3400 && value <= 0x4dbf)
            || (value >= 0x4e00 && value <= 0x9fff)
            || (value >= 0xf900 && value <= 0xfaff)
            || (value >= 0x3040 && value <= 0x30ff)
            || (value >= 0xac00 && value <= 0xd7af);
    }

    static bool isWordChar(wxUniChar ch)
    {
        const wxUint32 value = ch.GetValue();
        return (value >= 'A' && value <= 'Z')
            || (value >= 'a' && value <= 'z')
            || (value >= '0' && value <= '9')
            || value == '\''
            || value == '-';
    }

    static bool isClosingPunctuation(wxUniChar ch)
    {
        const wxUint32 value = ch.GetValue();
        return value == '.' || value == ',' || value == ';' || value == ':' || value == '!'
            || value == '?' || value == ')' || value == ']' || value == '}'
            || value == 0x3002 || value == 0xff0c || value == 0xff1b || value == 0xff1a
            || value == 0xff01 || value == 0xff1f || value == 0x3001 || value == 0x300d
            || value == 0x300f || value == 0x3011 || value == 0xff09 || value == 0x201d
            || value == 0x2019;
    }

    static wxString trimRight(wxString value)
    {
        value.Trim(true);
        return value;
    }

    static wxString trimLeft(wxString value)
    {
        value.Trim(false);
        return value;
    }

    std::vector<wxString> tokenize(const wxString& text)
    {
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

    bool fits(wxDC& dc, const wxString& text, int maxWidth)
    {
        int width = 0;
        int height = 0;
        dc.GetTextExtent(text, &width, &height);
        return width <= maxWidth;
    }

    void pushLine(std::vector<wxString>& lines, wxString& current)
    {
        current = trimRight(current);
        if (!current.empty()) {
            lines.push_back(current);
        }
        current.clear();
    }

    std::vector<wxString> wrapText(wxDC& dc, const wxString& text, int maxWidth)
    {
        std::vector<wxString> lines;
        wxString current;
        for (const wxString& token : tokenize(text)) {
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

    void drawWrapped(wxDC& dc, const std::vector<wxString>& lines, const wxRect& rect)
    {
        int lineHeight = 0;
        dc.GetTextExtent("M", nullptr, &lineHeight);
        lineHeight += lineHeight / 4;
        const int totalHeight = lineHeight * static_cast<int>(lines.size());
        int y = rect.GetTop() + std::max(0, (rect.GetHeight() - totalHeight) / 2);
        for (const wxString& line : lines) {
            int width = 0;
            int height = 0;
            dc.GetTextExtent(line, &width, &height);
            const int x = rect.GetLeft() + std::max(0, (rect.GetWidth() - width) / 2);
            dc.DrawText(line, x, y);
            y += lineHeight;
        }
    }

    void onPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        const wxSize size = GetClientSize();

        dc.SetBackground(wxBrush(darkTheme_ ? wxColour(24, 27, 33) : wxColour(244, 246, 248)));
        dc.Clear();

        int emWidth = 12;
        int emHeight = 20;
        GetTextExtent("M", &emWidth, &emHeight);
        const int padX = emWidth * 3 / 2;
        const int padY = emHeight;

        wxFont font = GetFont();
        font.SetPointSize(font.GetPointSize() + 16);
        font.SetWeight(wxFONTWEIGHT_BOLD);
        dc.SetFont(font);

        wxRect textShadowRect(padX + 3, padY + 3, size.GetWidth() - padX * 2, size.GetHeight() - padY * 2);
        wxRect textGlowRect(padX + 1, padY + 1, size.GetWidth() - padX * 2, size.GetHeight() - padY * 2);
        const wxRect textRect(padX, padY, size.GetWidth() - padX * 2, size.GetHeight() - padY * 2);
        const std::vector<wxString> lines = wrapText(dc, quote_, textRect.GetWidth());
        dc.SetTextForeground(darkTheme_ ? wxColour(55, 60, 72) : wxColour(190, 196, 205));
        drawWrapped(dc, lines, textShadowRect);
        dc.SetTextForeground(darkTheme_ ? wxColour(245, 203, 115) : wxColour(180, 138, 42));
        drawWrapped(dc, lines, textGlowRect);
        dc.SetTextForeground(darkTheme_ ? wxColour(255, 255, 255) : wxColour(25, 29, 36));
        drawWrapped(dc, lines, textRect);
    }

    bool darkTheme_;
    wxString quote_;
    std::function<void()> onClick_;
};

class RatingControl : public wxPanel {
public:
    RatingControl(wxWindow* parent, const std::vector<wxString>& emojis, const wxColour& emojiColour,
        const wxString& label, double value, bool darkTheme)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(200, 138)),
          emojis_(emojis),
          emojiColour_(emojiColour),
          label_(label),
          darkTheme_(darkTheme),
          committedLine_(lineFromScore(value)),
          hoverLine_(committedLine_)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &RatingControl::onPaint, this);
        Bind(wxEVT_MOTION, &RatingControl::onMotion, this);
        Bind(wxEVT_LEFT_DOWN, &RatingControl::onLeftDown, this);
        Bind(wxEVT_LEAVE_WINDOW, &RatingControl::onLeave, this);
    }

    double value() const
    {
        return committedLine_;
    }

    void adjust(double delta)
    {
        committedLine_ = std::max(0.0, std::min(5.0, std::round((committedLine_ + delta) * 2.0) / 2.0));
        hoverLine_ = committedLine_;
        hoverActive_ = false;
        Refresh();
    }

private:
    wxString currentEmoji(double value) const
    {
        if (emojis_.empty()) {
            return "";
        }
        const int index = std::max(0, std::min(static_cast<int>(emojis_.size()) - 1,
            static_cast<int>(std::round((value / 5.0) * (static_cast<int>(emojis_.size()) - 1)))));
        return emojis_[index];
    }

    static double lineFromScore(double value)
    {
        if (value < 0.0) {
            return 0.0;
        }
        if (value > 5.0) {
            return 5.0;
        }
        return value;
    }

    wxRect ratingRect() const
    {
        const wxSize size = GetClientSize();
        return wxRect(20, size.GetHeight() - 48, size.GetWidth() - 40, 30);
    }

    double lineFromX(int x) const
    {
        const wxRect rect = ratingRect();
        double ratio = static_cast<double>(x - rect.GetLeft()) / static_cast<double>(rect.GetWidth());
        if (ratio < 0.0) {
            ratio = 0.0;
        }
        if (ratio > 1.0) {
            ratio = 1.0;
        }
        return ratio * 5.0;
    }

    int xFromLine(double line) const
    {
        const wxRect rect = ratingRect();
        return rect.GetLeft() + static_cast<int>((line / 5.0) * rect.GetWidth());
    }

    void onMotion(wxMouseEvent& event)
    {
        hoverLine_ = lineFromX(event.GetX());
        hoverActive_ = true;
        Refresh();
    }

    void onLeftDown(wxMouseEvent& event)
    {
        committedLine_ = lineFromX(event.GetX());
        hoverLine_ = committedLine_;
        hoverActive_ = true;
        Refresh();
    }

    void onLeave(wxMouseEvent&)
    {
        hoverActive_ = false;
        Refresh();
    }

    void drawCirclePart(wxDC& dc, const wxRect& circle, const wxColour& colour, const wxRect& clip)
    {
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

    void onPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        const wxSize size = GetClientSize();
        dc.SetBackground(wxBrush(darkTheme_ ? wxColour(24, 27, 33) : wxColour(244, 246, 248)));
        dc.Clear();

        wxFont emojiFont = GetFont();
        emojiFont.SetPointSize(emojiFont.GetPointSize() + 20);
        emojiFont.SetFaceName("Noto Color Emoji");
        dc.SetFont(emojiFont);
        dc.SetTextForeground(emojiColour_);
        const double visibleLine = hoverActive_ ? hoverLine_ : committedLine_;
        dc.DrawLabel(currentEmoji(visibleLine), wxRect(0, 4, size.GetWidth(), 48), wxALIGN_CENTER);

        wxFont labelFont = GetFont();
        labelFont.SetPointSize(labelFont.GetPointSize() + 1);
        labelFont.SetWeight(wxFONTWEIGHT_BOLD);
        dc.SetFont(labelFont);
        dc.SetTextForeground(darkTheme_ ? wxColour(156, 164, 176) : wxColour(82, 90, 102));
        dc.DrawLabel(label_, wxRect(0, 53, size.GetWidth(), 24), wxALIGN_CENTER);

        const wxRect rect = ratingRect();
        const int radius = 12;
        const double activeLine = hoverActive_ ? hoverLine_ : committedLine_;
        const int lineX = xFromLine(activeLine);
        const int middleX = xFromLine(2.5);
        const int fillLeft = std::min(lineX, middleX);
        const int fillRight = std::max(lineX, middleX);
        const wxRect fillClip(fillLeft, rect.GetTop() - 4, fillRight - fillLeft, rect.GetHeight() + 8);
        const wxColour fillColour = lineX < middleX ? wxColour(232, 73, 73) : wxColour(247, 197, 72);

        dc.SetPen(wxPen(darkTheme_ ? wxColour(35, 39, 47) : wxColour(205, 211, 220), 1));
        dc.DrawLine(rect.GetLeft(), rect.GetTop() + rect.GetHeight() / 2, rect.GetRight(), rect.GetTop() + rect.GetHeight() / 2);

        for (int i = 0; i < 5; ++i) {
            const int centerX = rect.GetLeft() + static_cast<int>((i + 0.5) * rect.GetWidth() / 5.0);
            const int centerY = rect.GetTop() + rect.GetHeight() / 2;
            wxRect circle(centerX - radius, centerY - radius, radius * 2, radius * 2);

            dc.SetPen(wxPen(darkTheme_ ? wxColour(28, 31, 38) : wxColour(180, 187, 198), 2));
            dc.SetBrush(wxBrush(wxColour(17, 17, 17)));
            dc.DrawEllipse(circle);
            drawCirclePart(dc, circle, fillColour, fillClip);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(darkTheme_ ? wxColour(24, 27, 33) : wxColour(244, 246, 248), 2));
            dc.DrawEllipse(circle);
        }

        dc.SetPen(wxPen(hoverActive_ ? wxColour(236, 242, 250) : wxColour(128, 136, 150), 2));
        dc.DrawLine(lineX, rect.GetTop() - 7, lineX, rect.GetBottom() + 7);
    }

    std::vector<wxString> emojis_;
    wxColour emojiColour_;
    wxString label_;
    bool darkTheme_;
    double committedLine_;
    double hoverLine_;
    bool hoverActive_ = false;
};

ObservationDialog::ObservationDialog(wxWindow* parent, const ObservePromptDefaults& defaults)
    : wxDialog(parent, wxID_ANY, "Observer", wxDefaultPosition, wxDefaultSize,
          wxRESIZE_BORDER | wxSTAY_ON_TOP),
      quote_(defaults.quote),
      promptedAt_(currentTimestamp()),
      quoteRng_(static_cast<std::mt19937::result_type>(
          std::chrono::high_resolution_clock::now().time_since_epoch().count()))
{
    auto* root = new wxBoxSizer(wxVERTICAL);
    const bool darkTheme = defaults.theme != "light";
    theme_ = defaults.theme;
    quotes_ = defaults.quotes;
    history_ = defaults.history;
    weekStartsMonday_ = defaults.weekStartsMonday;
    quoteIndex_ = defaults.quoteIndex;
    if (quotes_.empty()) {
        quotes_.push_back(quote_);
        quoteIndex_ = 0;
    } else if (quoteIndex_ >= quotes_.size()) {
        quoteIndex_ = 0;
        quote_ = quotes_[quoteIndex_];
    }

    wxFont baseFont = GetFont();
    baseFont.SetPointSize(baseFont.GetPointSize() + 7);
    SetFont(baseFont);
    SetBackgroundColour(darkTheme ? wxColour(20, 23, 29) : wxColour(244, 246, 248));
    SetForegroundColour(darkTheme ? wxColour(235, 239, 245) : wxColour(28, 32, 38));
    finalOpacity_ = std::max(0, std::min(255, static_cast<int>(std::round(defaults.opacityPercent * 255.0 / 100.0))));

    int emWidth = 12;
    int emHeight = 20;
    GetTextExtent("M", &emWidth, &emHeight);
    const int outerMargin = std::max(10, emWidth);
    const int blockGap = std::max(8, emHeight / 2);

    root->AddSpacer(blockGap);
    quoteCanvas_ = new QuoteCanvas(this, quote_, darkTheme, [this]() { showRandomQuote(); });
    root->Add(quoteCanvas_, 1, wxLEFT | wxRIGHT | wxEXPAND, outerMargin);

    auto* ratingRow = new wxBoxSizer(wxHORIZONTAL);
    energyRating_ = new RatingControl(this,
        {wxString::FromUTF8("⏻"), wxString::FromUTF8("🪫"), wxString::FromUTF8("🔋"), wxString::FromUTF8("⚡")},
        wxColour(255, 112, 190),
        wxString::FromUTF8(_("energy")), defaults.energy, darkTheme);
    moodRating_ = new RatingControl(this,
        {wxString::FromUTF8("😢"), wxString::FromUTF8("🙁"), wxString::FromUTF8("😐"), wxString::FromUTF8("🙂"), wxString::FromUTF8("😄")},
        wxColour(255, 205, 72),
        wxString::FromUTF8(_("mood")), defaults.mood, darkTheme);
    groundingRating_ = new RatingControl(this,
        {wxString::FromUTF8("🔬"), wxString::FromUTF8("🧪"), wxString::FromUTF8("🪙"), wxString::FromUTF8("💵"), wxString::FromUTF8("💰")},
        wxColour(91, 214, 123),
        wxString::FromUTF8(_("grounding")), defaults.grounding, darkTheme);
    ratingRow->Add(energyRating_, 0, wxRIGHT, 22);
    ratingRow->Add(moodRating_, 0, wxRIGHT, 22);
    ratingRow->Add(groundingRating_, 0);
    root->Add(ratingRow, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM | wxALIGN_CENTER_HORIZONTAL, outerMargin);

    intervalCtrl_ = new wxTextCtrl(this, wxID_ANY,
        formatIntervalValue(clampIntervalSeconds(defaults.intervalSeconds) / 60.0),
        wxDefaultPosition, wxSize(emWidth * 5, -1),
        wxTE_PROCESS_ENTER | wxALIGN_RIGHT | wxBORDER_NONE);
    intervalCtrl_->SetForegroundColour(darkTheme ? wxColour(170, 176, 184) : wxColour(78, 84, 94));
    intervalCtrl_->SetBackgroundColour(darkTheme ? wxColour(28, 32, 39) : wxColour(235, 238, 242));
    intervalCtrl_->SetToolTip(wxString::FromUTF8(_("Win+Alt+G wake immediately")));

    const wxColour promptBg = darkTheme ? wxColour(30, 34, 42) : wxColour(255, 255, 255);
    const wxColour promptFg = darkTheme ? wxColour(255, 255, 255) : wxColour(15, 18, 24);
    auto* promptPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    auto* promptSizer = new wxBoxSizer(wxVERTICAL);
    const int promptMarginX = emWidth / 2;
    const int promptMarginY = emHeight / 2;
    const int textWidth = 680;
    
    wxFont inputFont = GetFont();
    inputFont.SetPointSize(inputFont.GetPointSize() + 16);

    wxClientDC dc(promptPanel);
    dc.SetFont(inputFont);
    wxFontMetrics metrics = dc.GetFontMetrics();
    int inputLineHeight = metrics.height; 
    activityCtrl_ = new wxStyledTextCtrl(promptPanel, wxID_ANY, wxDefaultPosition, wxSize(textWidth, inputLineHeight),
        wxBORDER_NONE);

    activityCtrl_->StyleClearAll();
    activityCtrl_->SetFont(inputFont);
    
    // 2. 【核心】绝对锁死单行（干掉所有换行行为）
    activityCtrl_->SetWrapMode(wxSTC_WRAP_NONE); // 100% 禁止单行文本因过长而自动换行
    activityCtrl_->SetUndoCollection(true);      // 保持撤销队列正常
    activityCtrl_->SetMarginWidth(0, 0);
    activityCtrl_->SetMarginWidth(1, 0);
    activityCtrl_->SetMarginWidth(2, 0);
    activityCtrl_->SetUseHorizontalScrollBar(false);
    activityCtrl_->SetUseVerticalScrollBar(false);
    // 5. 设置左右水平 Padding
    activityCtrl_->SetMarginLeft(15);
    activityCtrl_->SetMarginRight(15);

    activityCtrl_->SetCaretForeground(promptFg);
    activityCtrl_->SetCaretLineVisible(false);
    // 可选：如果希望光标所在的整行背景色更亮一点，可以取消下面两行的注释
    // activityCtrl_->SetCaretLineBackground(promptBg.ChangeLightness(110)); 
    // activityCtrl_->SetCaretLineVisibleAlways(true);

    activityCtrl_->StyleSetForeground(wxSTC_STYLE_DEFAULT, promptFg);
    activityCtrl_->StyleSetBackground(wxSTC_STYLE_DEFAULT, promptBg);
    activityCtrl_->StyleSetFont(wxSTC_STYLE_DEFAULT, inputFont);
    activityCtrl_->StyleClearAll();

    // 最后同步底层面板的底色，防止边缘出现闪烁、撕裂或白边
    activityCtrl_->SetBackgroundColour(promptBg);

    auto* promptInputRow = new wxBoxSizer(wxHORIZONTAL);
    promptInputRow->AddSpacer(promptMarginX);
    promptInputRow->Add(activityCtrl_, 1, wxALIGN_CENTER_VERTICAL);
    promptInputRow->AddSpacer(promptMarginX);
    promptSizer->AddSpacer(promptMarginY);
    promptSizer->Add(promptInputRow, 1, wxEXPAND);
    promptSizer->AddSpacer(promptMarginY);
    promptPanel->SetSizer(promptSizer);
    promptPanel->SetMinSize(wxSize(-1, emHeight * 2 + promptMarginY * 2));
    root->Add(promptPanel, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, outerMargin);

    auto* bottomRow = new wxBoxSizer(wxHORIZONTAL);
    wxFont hintFont = GetFont();
    hintFont.SetPointSize(std::max(5, hintFont.GetPointSize() - 7));
    hintFont.SetStyle(wxFONTSTYLE_ITALIC);
    const wxColour footerColour = darkTheme ? wxColour(142, 149, 158) : wxColour(96, 104, 116);
    const wxColour actionColour = darkTheme ? wxColour(175, 183, 194) : wxColour(72, 80, 92);
    auto makeLabel = [this, &hintFont](const wxString& text, const wxColour& colour) {
        auto* label = new wxStaticText(this, wxID_ANY, text);
        label->SetFont(hintFont);
        label->SetForegroundColour(colour);
        label->SetCursor(wxCursor(wxCURSOR_HAND));
        return label;
    };
    auto makeSeparator = [this, &hintFont, footerColour]() {
        auto* label = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("·"));
        label->SetFont(hintFont);
        label->SetForegroundColour(footerColour);
        return label;
    };

    submitLabel_ = makeLabel(wxString::FromUTF8(_("Enter submit")), actionColour);
    submitLabel_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { submit(); });
    skipLabel_ = makeLabel(wxString::FromUTF8(_("Esc skip")), actionColour);
    skipLabel_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { skip(); });
    auto* historyLabel = makeLabel(wxString::FromUTF8(_("^H History")), actionColour);
    historyLabel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { showStatistics(); });
    nextPromptLabel_ = makeLabel(wxString::FromUTF8(_("Next prompt")), footerColour);
    nextPromptLabel_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { toggleIntervalUnit(); });
    intervalUnitLabel_ = makeLabel(wxString::FromUTF8(_("minutes later")), footerColour);
    intervalUnitLabel_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { toggleIntervalUnit(); });
    intervalCtrl_->SetFont(hintFont);
    quitLabel_ = makeLabel(wxString::FromUTF8(_("Ctrl+Q quit")), actionColour);
    quitLabel_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { quit(); });

    bottomRow->AddStretchSpacer(1);
    bottomRow->Add(submitLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottomRow->Add(makeSeparator(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottomRow->Add(skipLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottomRow->Add(makeSeparator(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottomRow->Add(historyLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottomRow->Add(makeSeparator(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottomRow->Add(nextPromptLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottomRow->Add(intervalCtrl_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottomRow->Add(intervalUnitLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottomRow->Add(makeSeparator(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    bottomRow->Add(quitLabel_, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(bottomRow, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, outerMargin);

    Bind(wxEVT_CHAR_HOOK, &ObservationDialog::onCharHook, this);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { skip(); });
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        Refresh();
        event.Skip();
    });

    SetSizerAndFit(root);
    SetMinSize(wxSize(560, 360));
    // SetTransparent(140);
    const wxSize dialogSize = GetSize();
    int displayIndex = wxDisplay::GetFromWindow(this);
    if (displayIndex == wxNOT_FOUND) {
        displayIndex = 0;
    }
    wxDisplay display(displayIndex);
    const wxRect workArea = display.GetClientArea();
    int positionEmWidth = 10;
    int positionEmHeight = 18;
    GetTextExtent("M", &positionEmWidth, &positionEmHeight);
    const int margin = positionEmHeight * 2;
    animationFinalPosition_ = wxPoint(
        workArea.GetLeft() + (workArea.GetWidth() - dialogSize.GetWidth()) / 2,
        workArea.GetBottom() - dialogSize.GetHeight() - margin);
    animationStartPosition_ = wxPoint(animationFinalPosition_.x, workArea.GetBottom() + 8);
    Move(animationStartPosition_);
    hasTransparency_ = SetTransparent(0);
    activityCtrl_->SetFocus();
}

Observation ObservationDialog::observation() const
{
    return Observation{
        promptedAt_,
        currentTimestamp(),
        energyRating_->value(),
        moodRating_->value(),
        groundingRating_->value(),
        activityText(),
        quote_,
    };
}

double ObservationDialog::intervalSeconds() const
{
    double value = parseIntervalValue(intervalCtrl_->GetValue());
    if (!intervalInSeconds_) {
        value *= 60.0;
    }
    return clampIntervalSeconds(value);
}

void ObservationDialog::onCharHook(wxKeyEvent& event)
{
    const int keyCode = event.GetKeyCode();
    if (keyCode == WXK_RETURN || keyCode == WXK_NUMPAD_ENTER) {
        if (!event.ShiftDown()) {
            submit();
            return;
        }
    }
    if (keyCode == WXK_F1) {
        energyRating_->adjust(-0.5);
        return;
    }
    if (keyCode == WXK_F2) {
        energyRating_->adjust(0.5);
        return;
    }
    if (keyCode == WXK_F3) {
        moodRating_->adjust(-0.5);
        return;
    }
    if (keyCode == WXK_F4) {
        moodRating_->adjust(0.5);
        return;
    }
    if (keyCode == WXK_F5) {
        groundingRating_->adjust(-0.5);
        return;
    }
    if (keyCode == WXK_F6) {
        groundingRating_->adjust(0.5);
        return;
    }
    if (keyCode == WXK_ESCAPE) {
        skip();
        return;
    }
    if ((event.ControlDown() || event.CmdDown()) && (keyCode == 'S' || keyCode == 's')) {
        snooze();
        return;
    }
    if ((event.ControlDown() || event.CmdDown()) && (keyCode == 'Q' || keyCode == 'q')) {
        quit();
        return;
    }
    if ((event.ControlDown() || event.CmdDown()) && (keyCode == 'H' || keyCode == 'h')) {
        showStatistics();
        return;
    }
    event.Skip();
}

void ObservationDialog::submit()
{
    finishWithResult(ID_SUBMIT);
}

void ObservationDialog::skip()
{
    finishWithResult(ID_SKIP);
}

void ObservationDialog::snooze()
{
    finishWithResult(ID_SNOOZE);
}

void ObservationDialog::quit()
{
    finishWithResult(ID_QUIT);
}

void ObservationDialog::showStatistics()
{
    StatisticsDialog dialog(this, history_, theme_, weekStartsMonday_);
    dialog.ShowModal();
    activityCtrl_->SetFocus();
}

void ObservationDialog::showRandomQuote()
{
    if (quotes_.empty()) {
        return;
    }
    std::uniform_int_distribution<std::size_t> dist(0, quotes_.size() - 1);
    std::size_t nextIndex = dist(quoteRng_);
    if (quotes_.size() > 1 && nextIndex == quoteIndex_) {
        nextIndex = (nextIndex + 1) % quotes_.size();
    }
    quoteIndex_ = nextIndex;
    quote_ = quotes_[quoteIndex_];
    if (quoteCanvas_ != nullptr) {
        quoteCanvas_->setQuote(quote_);
    }
}

void ObservationDialog::toggleIntervalUnit()
{
    if (intervalCtrl_ == nullptr || intervalUnitLabel_ == nullptr) {
        return;
    }

    const double seconds = intervalSeconds();
    intervalInSeconds_ = !intervalInSeconds_;
    intervalCtrl_->ChangeValue(formatIntervalValue(intervalInSeconds_ ? seconds : seconds / 60.0));
    intervalUnitLabel_->SetLabel(wxString::FromUTF8(intervalInSeconds_ ? _("seconds later") : _("minutes later")));
    Layout();
}

void ObservationDialog::animateIn()
{
    constexpr int Frames = 12;
    constexpr int FrameDelayMs = 8;
    for (int i = 0; i <= Frames; ++i) {
        const double t = easeOutCubic(static_cast<double>(i) / Frames);
        const int y = animationStartPosition_.y
            + static_cast<int>((animationFinalPosition_.y - animationStartPosition_.y) * t);
        Move(animationFinalPosition_.x, y);
        if (hasTransparency_) {
            SetTransparent(static_cast<unsigned char>(finalOpacity_ * t));
        }
        Update();
        wxYieldIfNeeded();
        std::this_thread::sleep_for(std::chrono::milliseconds(FrameDelayMs));
    }
    Move(animationFinalPosition_);
    if (hasTransparency_) {
        SetTransparent(static_cast<unsigned char>(finalOpacity_));
    }
    activityCtrl_->SetFocus();
}

void ObservationDialog::animateOut()
{
    constexpr int Frames = 10;
    constexpr int FrameDelayMs = 8;
    for (int i = 0; i <= Frames; ++i) {
        const double t = static_cast<double>(i) / Frames;
        const double eased = t * t;
        const int y = animationFinalPosition_.y
            + static_cast<int>((animationStartPosition_.y - animationFinalPosition_.y) * eased);
        Move(animationFinalPosition_.x, y);
        if (hasTransparency_) {
            SetTransparent(static_cast<unsigned char>(finalOpacity_ * (1.0 - t)));
        }
        Update();
        wxYieldIfNeeded();
        std::this_thread::sleep_for(std::chrono::milliseconds(FrameDelayMs));
    }
}

void ObservationDialog::finishWithResult(int result)
{
    if (closing_) {
        return;
    }
    closing_ = true;
    animateOut();
    EndModal(result);
}

std::string ObservationDialog::activityText() const
{
    return trimmedUtf8(activityCtrl_ != nullptr ? activityCtrl_->GetText() : wxString());
}

std::string ObservationDialog::currentTimestamp() const
{
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
