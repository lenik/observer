#include "HistoryFrame.h"

#include "config.h"
#include "ObservationDialog.h"
#include "ObservationStore.h"
#include "calendar.h"
#include "emoji.h"
#include "formatting.h"
#include "lunar.h"
#include "UiTheme.h"
#include "wx/gtk/font.h"

#include <bas/locale/i18n.h>

#include <wx/artprov.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/spinctrl.h>
#include <wx/toolbar.h>

#include <wx/generic/listctrl.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <type_traits>

namespace {

enum {
    ID_MENU_CALENDAR = wxID_HIGHEST + 800,
    ID_MENU_DAY,
    ID_MENU_WEEK,
    ID_MENU_MONTH,
    ID_MENU_YEAR,
    ID_MENU_TODAY,
    ID_MENU_PREVIOUS,
    ID_MENU_NEXT,
    ID_RECORD_EDIT,
    ID_RECORD_DELETE,
};

struct ChartBucket {
    wxString label;
    int records = 0;
    int emgRecords = 0;
    long durationSeconds = 0;
    double energySum = 0.0;
    double moodSum = 0.0;
    double groundingSum = 0.0;
};

struct ChartSample {
    double x = 0.0;
    double energy = 0.0;
    double mood = 0.0;
    double grounding = 0.0;
    std::string key;
};

struct DaySummary {
    int records = 0;
    int nonEmptyRecords = 0;
    int emgRecords = 0;
    double energySum = 0.0;
    double moodSum = 0.0;
    double groundingSum = 0.0;
};

wxDateTime periodStart(wxDateTime value, HistoryFrame::ViewMode mode, bool weekStartsMonday) {
    switch (mode) {
    case HistoryFrame::ViewMode::Day:
        return startOfDay(value);
    case HistoryFrame::ViewMode::Week:
        return startOfWeek(value, weekStartsMonday);
    case HistoryFrame::ViewMode::Month:
        return startOfMonth(value);
    case HistoryFrame::ViewMode::Year:
        return startOfYear(value);
    case HistoryFrame::ViewMode::Calendar:
        return startOfMonth(value);
    }
    return startOfDay(value);
}

wxDateTime periodEnd(const wxDateTime &start, HistoryFrame::ViewMode mode) {
    wxDateTime end = start;
    switch (mode) {
    case HistoryFrame::ViewMode::Day:
        end.Add(wxDateSpan::Days(1));
        break;
    case HistoryFrame::ViewMode::Week:
        end.Add(wxDateSpan::Weeks(1));
        break;
    case HistoryFrame::ViewMode::Month:
        end.Add(wxDateSpan::Months(1));
        break;
    case HistoryFrame::ViewMode::Year:
        end.Add(wxDateSpan::Years(1));
        break;
    case HistoryFrame::ViewMode::Calendar:
        end.Add(wxDateSpan::Months(1));
        break;
    }
    return end;
}

long durationSeconds(const Observation &observation) {
    const std::time_t prompted = toTimeT(observation.promptedAt);
    const std::time_t submitted = toTimeT(observation.submittedAt);
    if (prompted == static_cast<std::time_t>(-1) || submitted == static_cast<std::time_t>(-1)) {
        return 0;
    }
    return std::max(0L, static_cast<long>(std::difftime(submitted, prompted)));
}

std::vector<ChartBucket> makeBuckets(HistoryFrame::ViewMode mode, wxDateTime anchor,
                                     bool eachYear, bool weekStartsMonday) {
    std::vector<ChartBucket> buckets;
    switch (mode) {
    case HistoryFrame::ViewMode::Day:
        for (int hour = 0; hour < 24; ++hour) {
            buckets.push_back({wxString::Format("%02d", hour)});
        }
        break;
    case HistoryFrame::ViewMode::Week:
        if (weekStartsMonday) {
            for (const char *label : {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"}) {
                buckets.push_back({wxString::FromUTF8(label)});
            }
        } else {
            for (const char *label : {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}) {
                buckets.push_back({wxString::FromUTF8(label)});
            }
        }
        break;
    case HistoryFrame::ViewMode::Month:
        for (int day = 1;
             day <= (eachYear ? daysInMonth(anchor) : maxDaysForMonth(anchor.GetMonth())); ++day) {
            buckets.push_back({wxString::Format("%d", day)});
        }
        break;
    case HistoryFrame::ViewMode::Year:
        for (int month = 0; month < 12; ++month) {
            wxDateTime date(1, static_cast<wxDateTime::Month>(month), anchor.GetYear());
            buckets.push_back({date.Format("%b")});
        }
        break;
    case HistoryFrame::ViewMode::Calendar:
        break;
    }
    return buckets;
}

int bucketIndex(const Observation &observation, HistoryFrame::ViewMode mode,
                bool weekStartsMonday) {
    const wxDateTime when = toDateTime(observation.promptedAt);
    switch (mode) {
    case HistoryFrame::ViewMode::Day:
        return parseLocalTime(observation.promptedAt).tm_hour;
    case HistoryFrame::ViewMode::Week:
        return weekdayIndex(when, weekStartsMonday);
    case HistoryFrame::ViewMode::Month:
        return when.GetDay() - 1;
    case HistoryFrame::ViewMode::Year:
        return static_cast<int>(when.GetMonth());
    case HistoryFrame::ViewMode::Calendar:
        return 0;
    }
    return 0;
}

double sampleAxisX(const Observation &observation, HistoryFrame::ViewMode mode,
                   bool weekStartsMonday) {
    const std::tm local = parseLocalTime(observation.promptedAt);
    const double dayFraction =
        (local.tm_hour * 3600.0 + local.tm_min * 60.0 + local.tm_sec) / 86400.0;
    switch (mode) {
    case HistoryFrame::ViewMode::Day:
        return dayFraction * 24.0;
    case HistoryFrame::ViewMode::Week:
        return weekdayIndex(toDateTime(observation.promptedAt), weekStartsMonday) + dayFraction;
    case HistoryFrame::ViewMode::Month:
    case HistoryFrame::ViewMode::Year:
    case HistoryFrame::ViewMode::Calendar:
        return bucketIndex(observation, mode, weekStartsMonday) + 0.5;
    }
    return 0.0;
}

std::string observationKey(const Observation &observation) {
    return observation.promptedAt + "\n" + observation.submittedAt + "\n" + observation.activity;
}

wxString periodTitle(HistoryFrame::ViewMode mode, wxDateTime anchor, bool eachYear) {
    switch (mode) {
    case HistoryFrame::ViewMode::Day:
        return wxString::FromUTF8(_("Day statistics")) + " " +
               (eachYear ? anchor.FormatISODate() : anchor.Format("%m-%d"));
    case HistoryFrame::ViewMode::Week:
        return wxString::FromUTF8(_("Week statistics")) + " " +
               (eachYear ? wxString::Format("%d ", anchor.GetYear()) : "") +
               wxString::FromUTF8(ordinal(isoWeek(anchor)).c_str());
    case HistoryFrame::ViewMode::Month:
        return wxString::FromUTF8(_("Month statistics")) + " " +
               (eachYear ? wxString::Format("%d ", anchor.GetYear()) : "") + anchor.Format("%b");
    case HistoryFrame::ViewMode::Year:
        return wxString::FromUTF8(_("Year statistics")) + " " +
               wxString::Format("%d", anchor.GetYear());
    case HistoryFrame::ViewMode::Calendar:
        return wxString::FromUTF8(_("Calendar")) + " " + anchor.Format("%Y %b");
    }
    return "";
}

std::map<std::string, DaySummary> summarizeDays(const std::vector<Observation> &observations) {
    std::map<std::string, DaySummary> summaries;
    for (const Observation &observation : observations) {
        if (observation.promptedAt.size() < 10) {
            continue;
        }
        DaySummary &summary = summaries[observation.promptedAt.substr(0, 10)];
        ++summary.records;
        if (!trim(observation.activity).empty()) {
            ++summary.nonEmptyRecords;
        }
        if (!observationEmgMissing(observation)) {
            ++summary.emgRecords;
            summary.energySum += observation.energy;
            summary.moodSum += observation.mood;
            summary.groundingSum += observation.grounding;
        }
    }
    return summaries;
}

wxColour withAlpha(const wxColour &color, unsigned char alpha) {
    return wxColour(color.Red(), color.Green(), color.Blue(), alpha);
}

wxColour blendOver(const wxColour &base, const wxColour &overlay, double overlayAlpha) {
    const double alpha = std::clamp(overlayAlpha, 0.0, 1.0);
    const double inverse = 1.0 - alpha;
    return wxColour(
        static_cast<unsigned char>(std::round(base.Red() * inverse + overlay.Red() * alpha)),
        static_cast<unsigned char>(std::round(base.Green() * inverse + overlay.Green() * alpha)),
        static_cast<unsigned char>(std::round(base.Blue() * inverse + overlay.Blue() * alpha)));
}

template <typename PaintDC>
void paintGlassRounded(PaintDC &dc, const wxRect &rect, const UiThemeColors &colors, const wxColour &tint,
                       double tintAlpha, int radius) {
    const wxColour base = colors.windowBg;
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(base);
    if (radius > 0) {
        dc.DrawRoundedRectangle(rect, radius);
    } else {
        dc.DrawRectangle(rect);
    }

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) {
        dc.SetBrush(blendOver(base, tint, tintAlpha));
        if (radius > 0) {
            dc.DrawRoundedRectangle(rect, radius);
        } else {
            dc.DrawRectangle(rect);
        }
        return;
    }

    gc->SetBrush(withAlpha(tint, static_cast<unsigned char>(std::round(std::clamp(tintAlpha, 0.0, 1.0) * 255.0))));
    if (radius > 0) {
        gc->DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);
    } else {
        gc->DrawRectangle(rect.x, rect.y, rect.width, rect.height);
    }
}

class ThemeGlassPanel : public wxPanel {
  public:
    ThemeGlassPanel(wxWindow *parent, const UiThemeColors &colors, Hsl fillTone, int radius = 12,
                    double alpha = 0.72)
        : wxPanel(parent, wxID_ANY), m_colors(colors), m_fillTone(fillTone), m_radius(radius),
          m_alpha(alpha) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &ThemeGlassPanel::onPaint, this);
        Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
            Refresh();
            event.Skip();
        });
    }

    wxColour glassFill() const { return blendOver(m_colors.windowBg, m_fillTone, m_alpha); }

  private:
    void onPaint(wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(this);
        paintGlassRounded(dc, GetClientRect(), m_colors, m_fillTone, m_alpha, m_radius);
    }

    UiThemeColors m_colors;
    Hsl m_fillTone;
    int m_radius = 12;
    double m_alpha = 0.72;
};

class ThemeIconBadge : public wxPanel {
  public:
    ThemeIconBadge(wxWindow *parent, const UiThemeColors &colors, const wxBitmap &bitmap)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(36, 36)), m_colors(colors),
          m_bitmap(bitmap) {
        SetMinSize(wxSize(36, 36));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &ThemeIconBadge::onPaint, this);
    }

  private:
    void onPaint(wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(this);
        const wxRect rect = GetClientRect();
        paintGlassRounded(dc, rect, m_colors, m_colors.surfaceBg, 0.58, 8);
        if (m_bitmap.IsOk()) {
            const int x = rect.x + (rect.width - m_bitmap.GetWidth()) / 2;
            const int y = rect.y + (rect.height - m_bitmap.GetHeight()) / 2;
            dc.DrawBitmap(m_bitmap, x, y, true);
        }
    }

    UiThemeColors m_colors;
    wxBitmap m_bitmap;
};

class WheelPickerPanel : public wxPanel {
  public:
    WheelPickerPanel(wxWindow *parent, std::vector<wxString> labels, int selected,
                     const UiThemeColors &colors)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(170, 260)),
          m_labels(std::move(labels)), m_selected(selected), m_colors(colors) {
        SetMinSize(wxSize(150, 240));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &WheelPickerPanel::onPaint, this);
        Bind(wxEVT_MOUSEWHEEL, &WheelPickerPanel::onMouseWheel, this);
        Bind(wxEVT_LEFT_DOWN, &WheelPickerPanel::onLeftDown, this);
        Bind(wxEVT_LEFT_UP, &WheelPickerPanel::onLeftUp, this);
        Bind(wxEVT_MOTION, &WheelPickerPanel::onMouseMove, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, &WheelPickerPanel::onMouseCaptureLost, this);
        Bind(wxEVT_CHAR_HOOK, &WheelPickerPanel::onCharHook, this);
    }

    int selected() const { return m_selected; }

    void setSelected(int selected) {
        if (m_labels.empty()) {
            m_selected = 0;
            return;
        }
        m_selected = std::clamp(selected, 0, static_cast<int>(m_labels.size()) - 1);
        Refresh();
    }

  private:
    static constexpr int RowHeight = 40;

    void move(int delta) { setSelected(m_selected + delta); }

    void onPaint(wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(this);
        const wxSize size = GetClientSize();
        const wxColour bg = m_colors.windowBg;
        const wxColour fg = m_colors.windowFg;
        const wxColour muted = m_colors.mutedFg;
        const wxColour selectedBg = m_colors.selectedBg;
        const wxColour border = m_colors.border;

        dc.SetBackground(wxBrush(bg));
        dc.Clear();

        const int centerY = size.GetHeight() / 2;
        wxRect selectedRect(8, centerY - RowHeight / 2, size.GetWidth() - 16, RowHeight);
        dc.SetPen(wxPen(border));
        dc.SetBrush(wxBrush(selectedBg));
        dc.DrawRoundedRectangle(selectedRect, 10);

        wxFont font = GetFont();
        font.SetPointSize(font.GetPointSize() + 3);
        wxFont selectedFont = font;
        selectedFont.SetWeight(wxFONTWEIGHT_BOLD);

        for (int offset = -4; offset <= 4; ++offset) {
            const int index = m_selected + offset;
            if (index < 0 || index >= static_cast<int>(m_labels.size())) {
                continue;
            }
            const int y = centerY + offset * RowHeight - RowHeight / 2;
            if (y > size.GetHeight() || y + RowHeight < 0) {
                continue;
            }

            dc.SetFont(offset == 0 ? selectedFont : font);
            dc.SetTextForeground(offset == 0 ? fg : muted);
            const wxString &label = m_labels[index];
            const wxSize extent = dc.GetTextExtent(label);
            dc.DrawText(label, (size.GetWidth() - extent.GetWidth()) / 2,
                        y + (RowHeight - extent.GetHeight()) / 2);
        }
    }

    void onMouseWheel(wxMouseEvent &event) {
        move(event.GetWheelRotation() > 0 ? -1 : 1);
        SetFocus();
    }

    void onLeftDown(wxMouseEvent &event) {
        SetFocus();
        m_dragging = true;
        m_dragMoved = false;
        m_lastY = event.GetY();
        m_dragRemainder = 0;
        if (!HasCapture()) {
            CaptureMouse();
        }
    }

    void onLeftUp(wxMouseEvent &event) {
        if (!m_dragMoved) {
            const int centerY = GetClientSize().GetHeight() / 2;
            const int offset = static_cast<int>(
                std::round((event.GetY() - centerY) / static_cast<double>(RowHeight)));
            move(offset);
        }
        m_dragging = false;
        if (HasCapture()) {
            ReleaseMouse();
        }
    }

    void onMouseMove(wxMouseEvent &event) {
        if (!m_dragging || !event.Dragging() || !event.LeftIsDown()) {
            event.Skip();
            return;
        }
        const int dy = event.GetY() - m_lastY;
        m_lastY = event.GetY();
        m_dragRemainder += dy;
        if (std::abs(m_dragRemainder) >= RowHeight) {
            const int steps = m_dragRemainder / RowHeight;
            move(-steps);
            m_dragRemainder -= steps * RowHeight;
            m_dragMoved = true;
        } else if (std::abs(m_dragRemainder) > 4) {
            m_dragMoved = true;
        }
    }

    void onMouseCaptureLost(wxMouseCaptureLostEvent &) { m_dragging = false; }

    void onCharHook(wxKeyEvent &event) {
        const int keyCode = event.GetKeyCode();
        if (keyCode == WXK_UP) {
            move(-1);
            return;
        }
        if (keyCode == WXK_DOWN) {
            move(1);
            return;
        }
        event.Skip();
    }

    std::vector<wxString> m_labels;
    int m_selected = 0;
    UiThemeColors m_colors;
    bool m_dragging = false;
    bool m_dragMoved = false;
    int m_lastY = 0;
    int m_dragRemainder = 0;
};

wxDateTime chooseMonth(wxWindow *parent, wxDateTime current, const UiThemeColors &colors) {
    wxDialog dialog(parent, wxID_ANY, wxString::FromUTF8(_("Month")), wxDefaultPosition,
                    wxSize(420, 360), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    dialog.SetBackgroundColour(colors.windowBg);
    dialog.SetForegroundColour(colors.windowFg);

    std::vector<wxString> years;
    for (int year = 1900; year <= 2100; ++year) {
        years.push_back(wxString::Format("%d", year));
    }
    std::vector<wxString> months;
    for (int month = 0; month < 12; ++month) {
        wxDateTime labelDate(1, static_cast<wxDateTime::Month>(month), current.GetYear());
        months.push_back(labelDate.Format("%b"));
    }

    auto *root = new wxBoxSizer(wxVERTICAL);
    auto *pickerRow = new wxBoxSizer(wxHORIZONTAL);
    auto *yearPicker = new WheelPickerPanel(&dialog, years, current.GetYear() - 1900, colors);
    auto *monthPicker =
        new WheelPickerPanel(&dialog, months, static_cast<int>(current.GetMonth()), colors);
    pickerRow->Add(yearPicker, 1, wxEXPAND | wxRIGHT, 10);
    pickerRow->Add(monthPicker, 1, wxEXPAND | wxLEFT, 10);
    root->Add(pickerRow, 1, wxALL | wxEXPAND, 16);

    auto *buttons = new wxStdDialogButtonSizer();
    auto *ok = new wxButton(&dialog, wxID_OK);
    auto *cancel = new wxButton(&dialog, wxID_CANCEL);
    buttons->AddButton(ok);
    buttons->AddButton(cancel);
    buttons->Realize();
    root->Add(buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);

    dialog.Bind(wxEVT_CHAR_HOOK, [&](wxKeyEvent &event) {
        const int keyCode = event.GetKeyCode();
        if (keyCode == WXK_RETURN || keyCode == WXK_NUMPAD_ENTER) {
            dialog.EndModal(wxID_OK);
            return;
        }
        if (keyCode == WXK_ESCAPE) {
            dialog.EndModal(wxID_CANCEL);
            return;
        }
        if (keyCode == WXK_LEFT) {
            yearPicker->SetFocus();
            return;
        }
        if (keyCode == WXK_RIGHT) {
            monthPicker->SetFocus();
            return;
        }
        event.Skip();
    });
    ok->SetDefault();
    dialog.SetSizerAndFit(root);
    dialog.CentreOnParent();
    yearPicker->SetFocus();

    if (dialog.ShowModal() == wxID_OK) {
        return wxDateTime(1, static_cast<wxDateTime::Month>(monthPicker->selected()),
                          1900 + yearPicker->selected());
    }
    return current;
}

} // namespace

class ObservationRecordTable : public wxGenericListCtrl {
  public:
    ObservationRecordTable(wxWindow *parent, const UiThemeColors &colors)
        : wxGenericListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT),
          m_colors(colors) {
        m_rowBg = blendOver(colors.windowBg, colors.surfaceBg, 0.74);
        m_rowAltBg = blendOver(colors.windowBg, colors.cardBg, 0.38);
        SetBackgroundColour(m_rowBg);
        SetForegroundColour(m_colors.listFg);
        SetTextColour(m_colors.listFg);
        Bind(wxEVT_LIST_COL_CLICK, &ObservationRecordTable::onColumnClick, this);
        Bind(wxEVT_LIST_ITEM_SELECTED, &ObservationRecordTable::onItemSelected, this);
        Bind(wxEVT_LIST_ITEM_DESELECTED, &ObservationRecordTable::onItemDeselected, this);
        Bind(wxEVT_LIST_ITEM_ACTIVATED, &ObservationRecordTable::onItemActivated, this);
        Bind(wxEVT_LIST_KEY_DOWN, &ObservationRecordTable::onListKeyDown, this);
        Bind(wxEVT_RIGHT_DOWN, &ObservationRecordTable::onRightDown, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
            int flags = 0;
            const long item = HitTest(event.GetPosition(), flags);
            if (item == wxNOT_FOUND) {
                clearSelection();
            }
            SetFocus();
            event.Skip();
        });
    }

    void setRecords(const std::vector<Observation> &records, long selectIndex = -1) {
        m_records = records;
        rebuild();
        if (selectIndex >= 0 && !m_rows.empty()) {
            selectRow(std::min(selectIndex, static_cast<long>(m_rows.size()) - 1));
        } else if (m_onSelection) {
            m_onSelection(nullptr);
        }
    }

    long selectedIndex() const {
        return GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    }

    bool hasSelection() const {
        return selectedIndex() >= 0;
    }

    std::vector<Observation> selectedRecords() const {
        std::vector<Observation> records;
        for (long item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); item >= 0;
             item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) {
            if (item < static_cast<long>(m_rows.size())) {
                records.push_back(m_rows[static_cast<std::size_t>(item)]);
            }
        }
        return records;
    }

    void setPeriodNavigationHandler(std::function<void(int keyCode)> handler) {
        m_onPeriodNavigation = std::move(handler);
    }

    void clearSelection() {
        SetItemState(-1, 0, wxLIST_STATE_SELECTED);
        notifySelectionChange();
    }

    void selectRow(long index) {
        SetItemState(-1, 0, wxLIST_STATE_SELECTED);
        if (index < 0 || index >= static_cast<long>(m_rows.size())) {
            if (m_onSelection) {
                m_onSelection(nullptr);
            }
            return;
        }
        SetItemState(index, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        EnsureVisible(index);
        if (m_onSelection) {
            m_onSelection(&m_rows[static_cast<std::size_t>(index)]);
        }
    }

    void setSelectionHandler(std::function<void(const Observation *)> handler) {
        m_onSelection = std::move(handler);
    }

    void setRecordHandlers(std::function<void(const Observation &)> onEdit,
                           std::function<void(const Observation &)> onDelete) {
        m_onEdit = std::move(onEdit);
        m_onDelete = std::move(onDelete);
    }

    void setBulkDeleteHandler(std::function<void()> handler) {
        m_onBulkDelete = std::move(handler);
    }

    const Observation *selectedRecord() const {
        const long selected = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (selected < 0 || selected >= static_cast<long>(m_rows.size())) {
            return nullptr;
        }
        return &m_rows[static_cast<std::size_t>(selected)];
    }

  private:
    enum Column {
        Prompted = 0,
        Submitted,
        Duration,
        Energy,
        Mood,
        Grounding,
        Average,
        Activity,
        Quote,
    };

    wxString columnLabel(int column) const {
        wxString label;
        switch (column) {
        case Prompted:
            label = wxString::FromUTF8(_("Prompted"));
            break;
        case Submitted:
            label = wxString::FromUTF8(_("Submitted"));
            break;
        case Duration:
            label = wxString::FromUTF8(_("Duration"));
            break;
        case Energy:
            label = wxString::FromUTF8(_("Energy"));
            break;
        case Mood:
            label = wxString::FromUTF8(_("Mood"));
            break;
        case Grounding:
            label = wxString::FromUTF8(_("Grounding"));
            break;
        case Average:
            label = wxString::FromUTF8(_("Avg"));
            break;
        case Activity:
            label = wxString::FromUTF8(_("Activity"));
            break;
        case Quote:
            label = wxString::FromUTF8(_("Quote"));
            break;
        default:
            label = "";
            break;
        }
        if (column == m_sortColumn) {
            label += m_ascending ? wxString::FromUTF8(" ▲") : wxString::FromUTF8(" ▼");
        }
        return label;
    }

    void appendColumns() {
        AppendColumn(columnLabel(Prompted), wxLIST_FORMAT_CENTER, 80);
        AppendColumn(columnLabel(Submitted), wxLIST_FORMAT_CENTER, 80);
        AppendColumn(columnLabel(Duration), wxLIST_FORMAT_CENTER, 70);
        AppendColumn(columnLabel(Energy), wxLIST_FORMAT_CENTER, 70);
        AppendColumn(columnLabel(Mood), wxLIST_FORMAT_CENTER, 70);
        AppendColumn(columnLabel(Grounding), wxLIST_FORMAT_CENTER, 70);
        AppendColumn(columnLabel(Average), wxLIST_FORMAT_CENTER, 45);
        AppendColumn(columnLabel(Activity), wxLIST_FORMAT_CENTER, 500);
        AppendColumn(columnLabel(Quote), wxLIST_FORMAT_CENTER, 300);
    }

    static wxListColumnFormat cellAlign(int column) {
        switch (column) {
        case Energy:
        case Mood:
        case Grounding:
        case Average:
            return wxLIST_FORMAT_RIGHT;
        case Activity:
        case Quote:
            return wxLIST_FORMAT_LEFT;
        default:
            return wxLIST_FORMAT_CENTER;
        }
    }

    void setCellText(long row, int column, const wxString &text) {
        wxListItem item;
        item.SetId(row);
        item.SetColumn(column);
        item.SetText(text);
        item.SetAlign(cellAlign(column));
        item.SetMask(wxLIST_MASK_TEXT);
        SetItem(item);
    }

    static double avg(const Observation &observation) {
        return (observation.energy + observation.mood + observation.grounding) / 3.0;
    }

    int compare(const Observation &a, const Observation &b) const {
        switch (m_sortColumn) {
        case Prompted:
            return a.promptedAt.compare(b.promptedAt);
        case Submitted:
            return a.submittedAt.compare(b.submittedAt);
        case Duration:
            return (durationSeconds(a) > durationSeconds(b)) -
                   (durationSeconds(a) < durationSeconds(b));
        case Energy:
            return (a.energy > b.energy) - (a.energy < b.energy);
        case Mood:
            return (a.mood > b.mood) - (a.mood < b.mood);
        case Grounding:
            return (a.grounding > b.grounding) - (a.grounding < b.grounding);
        case Average:
            return (avg(a) > avg(b)) - (avg(a) < avg(b));
        case Activity:
            return trim(a.activity).compare(trim(b.activity));
        case Quote:
            return a.quote.compare(b.quote);
        default:
            return 0;
        }
    }

    void rebuild() {
        ClearAll();
        appendColumns();
        m_rows = m_records;
        std::sort(m_rows.begin(), m_rows.end(), [this](const Observation &a, const Observation &b) {
            const int result = compare(a, b);
            if (result == 0) {
                return a.promptedAt > b.promptedAt;
            }
            return m_ascending ? result < 0 : result > 0;
        });

        long index = 0;
        for (const Observation &observation : m_rows) {
            InsertItem(index, wxString());
            setCellText(index, Prompted, timeOfDay(observation.promptedAt));
            setCellText(index, Submitted, timeOfDay(observation.submittedAt));
            setCellText(index, Duration,
                        wxString::FromUTF8(formatDuration(durationSeconds(observation)).c_str()));
            setCellText(index, Energy,
                        scoreWithEmoji(energyEmoji(observation.energy), observation.energy));
            setCellText(index, Mood, scoreWithEmoji(moodEmoji(observation.mood), observation.mood));
            setCellText(index, Grounding,
                        scoreWithEmoji(groundingEmoji(observation.grounding), observation.grounding));
            setCellText(index, Average,
                        wxString::FromUTF8(formatDouble(avg(observation)).c_str()));
            setCellText(index, Activity, wxString::FromUTF8(trim(observation.activity).c_str()));
            setCellText(index, Quote, wxString::FromUTF8(observation.quote.c_str()));
            SetItemTextColour(index, m_colors.listFg);
            SetItemBackgroundColour(index, index % 2 == 0 ? m_rowBg : m_rowAltBg);
            ++index;
        }
    }

    void onColumnClick(wxListEvent &event) {
        const int column = event.GetColumn();
        if (column == m_sortColumn) {
            m_ascending = !m_ascending;
        } else {
            m_sortColumn = column;
            m_ascending = column != Average;
        }
        rebuild();
    }

    void onItemSelected(wxListEvent &event) {
        event.Skip();
        notifySelectionChange();
    }

    void onItemDeselected(wxListEvent &event) {
        event.Skip();
        notifySelectionChange();
    }

    void notifySelectionChange() {
        if (!m_onSelection) {
            return;
        }
        if (const Observation *record = selectedRecord()) {
            m_onSelection(record);
        } else {
            m_onSelection(nullptr);
        }
    }

    void onItemActivated(wxListEvent &event) {
        const long index = event.GetIndex();
        if (m_onEdit && index >= 0 && index < static_cast<long>(m_rows.size())) {
            m_onEdit(m_rows[static_cast<std::size_t>(index)]);
        }
    }

    void onRightDown(wxMouseEvent &event) {
        int flags = 0;
        const long item = HitTest(event.GetPosition(), flags);
        if (item == wxNOT_FOUND) {
            event.Skip();
            return;
        }
        SetItemState(item, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        notifySelectionChange();

        wxMenu menu;
        if (m_onEdit) {
            menu.Append(ID_RECORD_EDIT, wxString::FromUTF8(_("Edit")));
        }
        if (m_onDelete) {
            menu.Append(ID_RECORD_DELETE, wxString::FromUTF8(_("Delete")));
        }
        if (menu.GetMenuItemCount() == 0) {
            event.Skip();
            return;
        }

        const int choice = PopupMenu(&menu);
        if (choice == ID_RECORD_EDIT && m_onEdit) {
            m_onEdit(m_rows[static_cast<std::size_t>(item)]);
        } else if (choice == ID_RECORD_DELETE && m_onDelete) {
            m_onDelete(m_rows[static_cast<std::size_t>(item)]);
        }
    }

    void onListKeyDown(wxListEvent &event) {
        const int keyCode = event.GetKeyCode();
        if (keyCode == WXK_UP || keyCode == WXK_DOWN) {
            if (!hasSelection()) {
                if (m_onPeriodNavigation) {
                    m_onPeriodNavigation(keyCode);
                }
                return;
            }
            if (wxGetKeyState(WXK_SHIFT) || wxGetKeyState(WXK_CONTROL)) {
                event.Skip();
                return;
            }
            const long count = GetItemCount();
            if (count > 0) {
                long selected = selectedIndex();
                if (selected < 0) {
                    selected = 0;
                } else {
                    selected += keyCode == WXK_UP ? -1 : 1;
                    selected = std::clamp(selected, 0L, count - 1);
                }
                SetItemState(-1, 0, wxLIST_STATE_SELECTED);
                SetItemState(selected, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                EnsureVisible(selected);
                notifySelectionChange();
            }
            return;
        }
        if (keyCode == WXK_DELETE) {
            if (m_onBulkDelete) {
                m_onBulkDelete();
            }
            return;
        }
        event.Skip();
    }

    std::vector<Observation> m_records;
    std::vector<Observation> m_rows;
    std::function<void(const Observation *)> m_onSelection;
    std::function<void(const Observation &)> m_onEdit;
    std::function<void(const Observation &)> m_onDelete;
    std::function<void()> m_onBulkDelete;
    std::function<void(int keyCode)> m_onPeriodNavigation;
    UiThemeColors m_colors;
    wxColour m_rowBg;
    wxColour m_rowAltBg;
    int m_sortColumn = Average;
    bool m_ascending = false;
};

class ObservationCalendarPanel : public wxPanel {
  public:
    ObservationCalendarPanel(wxWindow *parent, const UiThemeColors &colors, bool weekStartsMonday)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(620, 430)), m_colors(colors),
          m_weekStartsMonday(weekStartsMonday) {
        SetMinSize(wxSize(620, 430));
        SetMaxSize(wxSize(620, 430));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &ObservationCalendarPanel::onPaint, this);
        Bind(wxEVT_LEFT_DOWN, &ObservationCalendarPanel::onLeftDown, this);
    }

    void setObservations(const std::vector<Observation> &observations) {
        m_summaries = summarizeDays(observations);
        Refresh();
    }

    void setMonth(wxDateTime date) {
        m_anchor = startOfMonth(date);
        Refresh();
    }

    void setSelected(wxDateTime date) {
        m_selected = startOfDay(date);
        Refresh();
    }

    void setDaySelectedHandler(std::function<void(wxDateTime)> handler) {
        m_onDaySelected = std::move(handler);
    }

    void setMonthChangedHandler(std::function<void(wxDateTime)> handler) {
        m_onMonthChanged = std::move(handler);
    }

  private:
    void onPaint(wxPaintEvent &) {
        m_cellRects.clear();
        wxAutoBufferedPaintDC dc(this);
        const wxSize size = GetClientSize();
        const wxColour base = m_colors.windowBg;
        const wxColour fg = m_colors.windowFg;
        const wxColour muted = m_colors.mutedFg;
        const wxColour selectedBg = blendOver(base, m_colors.selectedBg, 0.82);
        const wxColour todayBg = blendOver(base, m_colors.todayBg, 0.82);
        const wxColour todayBorder = m_colors.chartHighlight;
        const wxColour hotDot = m_colors.chartHighlight;
        const wxColour calmDot = m_colors.actionFg;
        paintGlassRounded(dc, wxRect(0, 0, size.GetWidth(), size.GetHeight()), m_colors,
                          m_colors.surfaceBg, 0.74, 14);

        wxFont defaultFont = GetFont();

        wxFont weekdayFont = GetFont();
        weekdayFont.SetWeight(wxFONTWEIGHT_BOLD);
        dc.SetFont(weekdayFont);
        dc.SetTextForeground(muted);

        const int margin = 12;
        const int cellPadding = 6;
        const int navH = 34;
        const int headerH = 26;
        const int cellW = std::max(1, (size.GetWidth() - margin * 2) / 7);
        const wxDateTime first = startOfMonth(m_anchor);
        const int firstOffset = weekdayIndex(first, m_weekStartsMonday);
        const int weeks = (firstOffset + daysInMonth(m_anchor) + 6) / 7;
        const int cellH =
            std::max(62, (size.GetHeight() - margin * 2 - navH - headerH) / std::max(1, weeks));
        dc.SetFont(defaultFont);

        wxFont navFont = GetFont();
        navFont.SetPointSize(navFont.GetPointSize() + 2);
        navFont.SetWeight(wxFONTWEIGHT_BOLD);
        dc.SetFont(navFont);
        dc.SetTextForeground(fg);
        m_prevRect = wxRect(margin, margin, 92, 26);
        m_nextRect = wxRect(size.GetWidth() - margin - 92, margin, 92, 26);
        wxDateTime prevMonth = m_anchor;
        prevMonth.Add(wxDateSpan::Months(-1));
        wxDateTime nextMonth = m_anchor;
        nextMonth.Add(wxDateSpan::Months(1));
        const wxString prevTitle = wxString::FromUTF8("◀ ") + prevMonth.Format("%b");
        const wxString monthTitle = m_anchor.Format("%Y %b");
        const wxString nextTitle = nextMonth.Format("%b") + wxString::FromUTF8(" ▶");
        const wxSize titleExtent = dc.GetTextExtent(monthTitle);
        m_titleRect = wxRect((size.GetWidth() - titleExtent.GetWidth()) / 2 - 16, margin,
                             titleExtent.GetWidth() + 32, 26);
        dc.DrawText(prevTitle, m_prevRect.GetX() + 4, m_prevRect.GetY() + 2);
        const wxSize nextExtent = dc.GetTextExtent(nextTitle);
        dc.DrawText(nextTitle, m_nextRect.GetRight() - nextExtent.GetWidth(),
                    m_nextRect.GetY() + 2);
        dc.DrawText(monthTitle, m_titleRect.GetX() + 16, m_titleRect.GetY() + 2);
        dc.SetFont(defaultFont);

        const char *mondayFirst[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
        const char *sundayFirst[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        const char **weekdays = m_weekStartsMonday ? mondayFirst : sundayFirst;
        dc.SetFont(weekdayFont);
        dc.SetTextForeground(muted);
        for (int i = 0; i < 7; ++i) {
            const wxString label = wxString::FromUTF8(weekdays[i]);
            const wxSize extent = dc.GetTextExtent(label);
            dc.DrawText(label, margin + i * cellW + (cellW - extent.GetWidth()) / 2, margin + navH);
        }
        dc.SetFont(defaultFont);

        wxDateTime day = first;
        const wxDateTime today = wxDateTime::Today();

        wxFont dayFont = GetFont();
        dayFont.SetPointSize(dayFont.GetPointSize() + 2);
        dayFont.SetWeight(wxFONTWEIGHT_BOLD);
        wxFont lunarFont = GetFont();
        lunarFont.SetPointSize(std::max(7, lunarFont.GetPointSize() - 1));

        const int gridY = margin + navH + headerH;
        const int gridRows = 6;
        const int daysInCurrentMonth = daysInMonth(m_anchor);
        const wxColour gridColor = m_colors.grid;
        const wxColour selectedBorder = m_colors.weekdayHeader;
        const auto cellHasDay = [&](int r, int c) -> bool {
            const int idx = r * 7 + c;
            if (idx < firstOffset) {
                return false;
            }
            return (idx - firstOffset) < daysInCurrentMonth;
        };

        struct CalendarDayCell {
            wxRect rect;
            wxDateTime day;
            bool isSelected = false;
            bool isToday = false;
            bool holiday = false;
        };
        std::vector<CalendarDayCell> dayCells;

        for (int row = 0; row < gridRows; ++row) {
            for (int col = 0; col < 7; ++col) {
                const int cellIndex = row * 7 + col;
                const int x = margin + col * cellW;
                const int y = gridY + row * cellH;
                const wxRect rect(x, y, cellW, cellH);
                if (cellIndex < firstOffset || day.GetMonth() != m_anchor.GetMonth()) {
                    continue;
                }

                const bool isSelected = day.IsSameDate(m_selected);
                const bool isToday = day.IsSameDate(today);
                dayCells.push_back({rect, day, isSelected, isToday, isHoliday(day)});
                m_cellRects.push_back({rect, day});

                if (isSelected) {
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.SetBrush(wxBrush(selectedBg));
                    dc.DrawRectangle(rect);
                } else if (isToday) {
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.SetBrush(wxBrush(todayBg));
                    dc.DrawRectangle(rect);
                } else {
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.SetBrush(wxBrush(blendOver(base, m_colors.cardBg, 0.28)));
                    dc.DrawRectangle(rect);
                }

                day.Add(wxDateSpan::Days(1));
            }
        }

        dc.SetPen(wxPen(gridColor, 1));
        for (int r = 0; r <= gridRows; ++r) {
            for (int c = 0; c < 7; ++c) {
                const bool dayAbove = r > 0 && cellHasDay(r - 1, c);
                const bool dayBelow = r < gridRows && cellHasDay(r, c);
                if (!((dayAbove && dayBelow) || (dayAbove && !dayBelow) ||
                      (!dayAbove && dayBelow))) {
                    continue;
                }
                const int x1 = margin + c * cellW;
                const int y = gridY + r * cellH;
                dc.DrawLine(x1, y, x1 + cellW, y);
            }
        }
        for (int c = 0; c <= 7; ++c) {
            for (int r = 0; r < gridRows; ++r) {
                const bool dayLeft = c > 0 && cellHasDay(r, c - 1);
                const bool dayRight = c < 7 && cellHasDay(r, c);
                if (!((dayLeft && dayRight) || (dayLeft && !dayRight) || (!dayLeft && dayRight))) {
                    continue;
                }
                const int x = margin + c * cellW;
                const int y1 = gridY + r * cellH;
                dc.DrawLine(x, y1, x, y1 + cellH);
            }
        }

        for (const CalendarDayCell &cell : dayCells) {
            if (!cell.isToday && !cell.isSelected) {
                continue;
            }
            const wxColour highlight = cell.isToday ? todayBorder : selectedBorder;
            const wxRect &rect = cell.rect;
            dc.SetPen(wxPen(highlight, 1));
            dc.DrawLine(rect.GetLeft(), rect.GetTop(), rect.GetRight(), rect.GetTop());
            dc.DrawLine(rect.GetLeft(), rect.GetBottom(), rect.GetRight(), rect.GetBottom());
            dc.DrawLine(rect.GetLeft(), rect.GetTop(), rect.GetLeft(), rect.GetBottom());
            dc.DrawLine(rect.GetRight(), rect.GetTop(), rect.GetRight(), rect.GetBottom());
        }

        for (const CalendarDayCell &cell : dayCells) {
            wxRect inner = cell.rect;
            inner.Deflate(cellPadding);

            dc.SetTextForeground(cell.holiday ? m_colors.chartHighlight : fg);
            dc.SetFont(dayFont);
            wxString dayLabel = wxString::Format("%d", cell.day.GetDay());
            wxSize dayExtent = dc.GetTextExtent(dayLabel);
            dc.DrawText(dayLabel, inner.GetX(), inner.GetY());

            dc.SetFont(lunarFont);
            dc.SetTextForeground(cell.holiday ? blendOver(base, m_colors.chartHighlight, 0.75) : muted);
            wxString lunar = lunarLabel(cell.day);
            dc.DrawText(lunar, inner.GetX(), inner.GetY() + dayExtent.GetHeight() + 2);

            const auto summary = m_summaries.find(cell.day.FormatISODate().ToStdString());
            if (summary != m_summaries.end() && summary->second.records > 0) {
                const DaySummary &value = summary->second;
                const double avg = value.emgRecords > 0
                                       ? (value.energySum + value.moodSum + value.groundingSum) /
                                             (3.0 * value.emgRecords)
                                       : 0.0;
                const bool hot = value.nonEmptyRecords > 3 && avg > 3.0;
                const int dotRadius = 5;
                const int dotGap = 4;
                dc.SetTextForeground(fg);
                dc.SetFont(lunarFont);
                const wxString countLabel = wxString::Format("%d", value.nonEmptyRecords);
                const wxSize countExtent = dc.GetTextExtent(countLabel);
                const int dotDiameter = dotRadius * 2;
                const int rowHeight = std::max(countExtent.GetHeight(), dotDiameter);
                const int rowCenterY = inner.GetBottom() - rowHeight / 2;
                const int dotX = inner.GetRight() - dotRadius;
                const int dotY = rowCenterY;
                dc.DrawText(countLabel, dotX - dotRadius - dotGap - countExtent.GetWidth(),
                            rowCenterY - countExtent.GetHeight() / 2);
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(hot ? hotDot : calmDot));
                dc.DrawCircle(dotX, dotY, dotRadius);
            }
        }
        dc.SetFont(defaultFont);
    }

    void onLeftDown(wxMouseEvent &event) {
        const wxPoint point = event.GetPosition();
        if (m_prevRect.Contains(point) || m_nextRect.Contains(point)) {
            m_anchor.Add(wxDateSpan::Months(m_prevRect.Contains(point) ? -1 : 1));
            m_selected = m_anchor;
            Refresh();
            if (m_onMonthChanged) {
                m_onMonthChanged(m_selected);
            }
            return;
        }
        if (m_titleRect.Contains(point)) {
            wxDateTime chosen = chooseMonth(this, m_anchor, m_colors);
            m_anchor = startOfMonth(chosen);
            m_selected = m_anchor;
            Refresh();
            if (m_onMonthChanged) {
                m_onMonthChanged(m_selected);
            }
            return;
        }
        for (const auto &cell : m_cellRects) {
            if (cell.rect.Contains(point)) {
                m_selected = startOfDay(cell.date);
                m_anchor = startOfMonth(cell.date);
                Refresh();
                if (m_onDaySelected) {
                    m_onDaySelected(m_selected);
                }
                return;
            }
        }
    }

    struct CalendarCell {
        wxRect rect;
        wxDateTime date;
    };

    UiThemeColors m_colors;
    bool m_weekStartsMonday;
    wxDateTime m_anchor = startOfMonth(wxDateTime::Today());
    wxDateTime m_selected = wxDateTime::Today();
    std::map<std::string, DaySummary> m_summaries;
    std::vector<CalendarCell> m_cellRects;
    wxRect m_prevRect;
    wxRect m_titleRect;
    wxRect m_nextRect;
    std::function<void(wxDateTime)> m_onDaySelected;
    std::function<void(wxDateTime)> m_onMonthChanged;
};

class StatisticsChartPanel : public wxPanel {
  public:
    StatisticsChartPanel(wxWindow *parent, const UiThemeColors &colors)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 220)), m_colors(colors) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &StatisticsChartPanel::onPaint, this);
        Bind(wxEVT_MOUSEWHEEL, &StatisticsChartPanel::onMouseWheel, this);
        Bind(wxEVT_LEFT_DOWN, &StatisticsChartPanel::onLeftDown, this);
        Bind(wxEVT_LEFT_UP, &StatisticsChartPanel::onLeftUp, this);
        Bind(wxEVT_MOTION, &StatisticsChartPanel::onMouseMove, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, &StatisticsChartPanel::onMouseCaptureLost, this);
    }

    void setData(std::vector<ChartBucket> buckets, std::vector<ChartSample> samples) {
        m_buckets = std::move(buckets);
        m_samples = std::move(samples);
        m_zoom = 1.0;
        m_pan = 0.0;
        Refresh();
    }

    void setHighlightedSample(std::string key) {
        m_highlightedSampleKey = std::move(key);
        Refresh();
    }

  private:
    static constexpr int ChartLeft = 48;
    static constexpr int ChartRight = 18;
    static constexpr double MinZoom = 1.0;
    static constexpr double MaxZoom = 12.0;

    int plotWidth() const {
        const wxSize size = GetClientSize();
        return std::max(1, size.GetWidth() - ChartLeft - ChartRight);
    }

    void clampPan() {
        const int plotW = plotWidth();
        const double contentW = plotW * m_zoom;
        if (contentW <= plotW) {
            m_pan = 0.0;
            return;
        }
        m_pan = std::clamp(m_pan, plotW - contentW, 0.0);
    }

    void onMouseWheel(wxMouseEvent &event) {
        if (m_buckets.empty()) {
            event.Skip();
            return;
        }

        const int plotW = plotWidth();
        const double oldZoom = m_zoom;
        const double factor = event.GetWheelRotation() > 0 ? 1.2 : 1.0 / 1.2;
        m_zoom = std::clamp(m_zoom * factor, MinZoom, MaxZoom);
        if (std::abs(m_zoom - oldZoom) < 0.000001) {
            return;
        }

        const double mouseX = std::clamp(static_cast<double>(event.GetX() - ChartLeft), 0.0,
                                         static_cast<double>(plotW));
        const double contentX = (mouseX - m_pan) / oldZoom;
        m_pan = mouseX - contentX * m_zoom;
        clampPan();
        Refresh();
    }

    void onLeftDown(wxMouseEvent &event) {
        m_dragging = true;
        m_lastDragX = event.GetX();
        if (!HasCapture()) {
            CaptureMouse();
        }
    }

    void onLeftUp(wxMouseEvent &) {
        m_dragging = false;
        if (HasCapture()) {
            ReleaseMouse();
        }
    }

    void onMouseMove(wxMouseEvent &event) {
        if (!m_dragging || !event.Dragging() || !event.LeftIsDown()) {
            event.Skip();
            return;
        }
        const int x = event.GetX();
        m_pan += x - m_lastDragX;
        m_lastDragX = x;
        clampPan();
        Refresh();
    }

    void onMouseCaptureLost(wxMouseCaptureLostEvent &) { m_dragging = false; }

    void onPaint(wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(this);
        const wxSize size = GetClientSize();
        const wxColour fg = m_colors.windowFg;
        const wxColour grid = m_colors.grid;

        paintGlassRounded(dc, wxRect(0, 0, size.GetWidth(), size.GetHeight()), m_colors,
                          m_colors.surfaceBg, 0.72, 12);

        const int left = ChartLeft;
        const int right = ChartRight;
        const int top = 28;
        const int bottom = 38;
        const int plotW = std::max(1, size.GetWidth() - left - right);
        const int plotH = std::max(1, size.GetHeight() - top - bottom);
        const int baseY = top + plotH;

        dc.SetPen(wxPen(grid));
        dc.DrawLine(left, top, left, baseY);
        dc.DrawLine(left, baseY, left + plotW, baseY);
        for (int i = 1; i < 5; ++i) {
            const int y = baseY - plotH * i / 5;
            dc.DrawLine(left, y, left + plotW, y);
        }

        if (m_buckets.empty()) {
            dc.SetTextForeground(fg);
            dc.DrawText(wxString::FromUTF8(_("No records")), left + 12, top + 20);
            return;
        }

        int maxRecords = 1;
        long maxDuration = 1;
        for (const ChartBucket &bucket : m_buckets) {
            maxRecords = std::max(maxRecords, bucket.records);
            maxDuration = std::max(maxDuration, bucket.durationSeconds);
        }

        clampPan();
        const double bucketW =
            std::max(1.0, plotW * m_zoom / static_cast<double>(m_buckets.size()));
        const int barW = std::max(2, static_cast<int>(std::round(bucketW / 5.0)));
        auto axisX = [&](double value) {
            return left + static_cast<int>(std::round(m_pan + value * bucketW));
        };
        auto bucketCenterX = [&](std::size_t index) {
            return axisX(static_cast<double>(index) + 0.5);
        };

        dc.SetClippingRegion(left, top, plotW, plotH + bottom);
        for (std::size_t i = 0; i < m_buckets.size(); ++i) {
            const ChartBucket &bucket = m_buckets[i];
            const int x = bucketCenterX(i);
            if (x + barW < left || x - barW > left + plotW) {
                continue;
            }
            const int countH = static_cast<int>(
                std::round(plotH * bucket.records / static_cast<double>(maxRecords)));
            const int durationH = static_cast<int>(
                std::round(plotH * bucket.durationSeconds / static_cast<double>(maxDuration)));

            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.SetBrush(wxBrush(wxColour(76, 142, 255)));
            dc.DrawRectangle(x - barW - 1, baseY - countH, barW, countH);
            dc.SetBrush(wxBrush(wxColour(255, 183, 77)));
            dc.DrawRectangle(x + 1, baseY - durationH, barW, durationH);

            if (bucketW >= 28 || i % 2 == 0) {
                dc.SetTextForeground(fg);
                dc.DrawText(bucket.label, x - 10, baseY + 8);
            }
        }

        auto drawLine = [&](auto valueFor, const wxColour &color) {
            wxPoint previous;
            bool hasPrevious = false;
            dc.SetPen(wxPen(color, 2));
            dc.SetBrush(wxBrush(color));
            if (!m_samples.empty()) {
                for (const ChartSample &sample : m_samples) {
                    const double value = std::clamp(valueFor(sample), 0.0, 5.0);
                    const int x = axisX(sample.x);
                    const int y = baseY - static_cast<int>(std::round(plotH * value / 5.0));
                    if (hasPrevious) {
                        dc.DrawLine(previous.x, previous.y, x, y);
                    }
                    const bool highlighted =
                        !m_highlightedSampleKey.empty() && sample.key == m_highlightedSampleKey;
                    if (highlighted) {
                        dc.SetPen(wxPen(m_colors.chartHighlight, 2));
                        dc.SetBrush(*wxTRANSPARENT_BRUSH);
                        dc.DrawCircle(x, y, 7);
                        dc.SetPen(wxPen(color, 2));
                        dc.SetBrush(wxBrush(color));
                    }
                    dc.DrawCircle(x, y, highlighted ? 4 : 3);
                    previous = wxPoint(x, y);
                    hasPrevious = true;
                }
                return;
            }

            for (std::size_t i = 0; i < m_buckets.size(); ++i) {
                const ChartBucket &bucket = m_buckets[i];
                if (bucket.emgRecords <= 0) {
                    continue;
                }
                const double value = std::clamp(valueFor(bucket) / bucket.emgRecords, 0.0, 5.0);
                const int x = bucketCenterX(i);
                const int y = baseY - static_cast<int>(std::round(plotH * value / 5.0));
                if (hasPrevious) {
                    dc.DrawLine(previous.x, previous.y, x, y);
                }
                dc.DrawCircle(x, y, 3);
                previous = wxPoint(x, y);
                hasPrevious = true;
            }
        };

        auto pointFor = [&](double xValue, double value) {
            const int x = axisX(xValue);
            const int y =
                baseY - static_cast<int>(std::round(plotH * std::clamp(value, 0.0, 5.0) / 5.0));
            return wxPoint(x, y);
        };

        std::vector<std::pair<double, double>> recordSamples;
        std::vector<std::pair<double, double>> durationSamples;
        std::vector<std::pair<double, double>> avgSamples;
        std::vector<wxPoint> avgPoints;
        if (!m_samples.empty()) {
            for (const ChartSample &sample : m_samples) {
                const double avg = (sample.energy + sample.mood + sample.grounding) / 3.0;
                avgSamples.push_back({sample.x, avg});
                avgPoints.push_back(pointFor(sample.x, avg));
            }
        } else {
            for (std::size_t i = 0; i < m_buckets.size(); ++i) {
                const ChartBucket &bucket = m_buckets[i];
                if (bucket.emgRecords <= 0) {
                    continue;
                }
                const double x = static_cast<double>(i) + 0.5;
                const double avg = (bucket.energySum + bucket.moodSum + bucket.groundingSum) /
                                   (3.0 * bucket.emgRecords);
                avgSamples.push_back({x, avg});
                avgPoints.push_back(pointFor(x, avg));
            }
        }

        for (std::size_t i = 0; i < m_buckets.size(); ++i) {
            const ChartBucket &bucket = m_buckets[i];
            if (bucket.records <= 0) {
                continue;
            }
            const double x = static_cast<double>(i) + 0.5;
            recordSamples.push_back({x, static_cast<double>(bucket.records) / maxRecords});
            durationSamples.push_back(
                {x, static_cast<double>(bucket.durationSeconds) / maxDuration});
        }

        auto drawPolyline = [&](const std::vector<wxPoint> &points, const wxColour &color,
                                int width) {
            if (points.size() < 2) {
                return;
            }
            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
            if (gc) {
                gc->Clip(left, top, plotW, plotH + bottom);
                gc->SetPen(gc->CreatePen(wxPen(color, width)));
                wxGraphicsPath path = gc->CreatePath();
                path.MoveToPoint(points.front().x, points.front().y);
                for (std::size_t i = 1; i < points.size(); ++i) {
                    path.AddLineToPoint(points[i].x, points[i].y);
                }
                gc->StrokePath(path);
                gc->ResetClip();
                return;
            }

            dc.SetPen(wxPen(color, width));
            for (std::size_t i = 1; i < points.size(); ++i) {
                dc.DrawLine(points[i - 1], points[i]);
            }
        };

        auto fitAt = [](const std::vector<std::pair<double, double>> &samples, double x) {
            if (samples.empty()) {
                return 0.0;
            }
            if (samples.size() == 1) {
                return samples.front().second;
            }
            if (samples.size() == 2) {
                const auto &a = samples.front();
                const auto &b = samples.back();
                const double span = b.first - a.first;
                if (std::abs(span) < 0.000001) {
                    return a.second;
                }
                return a.second + (b.second - a.second) * (x - a.first) / span;
            }

            double sx = 0.0;
            double sx2 = 0.0;
            double sx3 = 0.0;
            double sx4 = 0.0;
            double sy = 0.0;
            double sxy = 0.0;
            double sx2y = 0.0;
            for (const auto &sample : samples) {
                const double x1 = sample.first;
                const double x2 = x1 * x1;
                sx += x1;
                sx2 += x2;
                sx3 += x2 * x1;
                sx4 += x2 * x2;
                sy += sample.second;
                sxy += x1 * sample.second;
                sx2y += x2 * sample.second;
            }

            const double n = static_cast<double>(samples.size());
            double matrix[3][4] = {
                {n, sx, sx2, sy},
                {sx, sx2, sx3, sxy},
                {sx2, sx3, sx4, sx2y},
            };
            for (int col = 0; col < 3; ++col) {
                int pivot = col;
                for (int row = col + 1; row < 3; ++row) {
                    if (std::abs(matrix[row][col]) > std::abs(matrix[pivot][col])) {
                        pivot = row;
                    }
                }
                if (std::abs(matrix[pivot][col]) < 0.000001) {
                    return samples.front().second;
                }
                if (pivot != col) {
                    for (int k = col; k < 4; ++k) {
                        std::swap(matrix[col][k], matrix[pivot][k]);
                    }
                }
                const double divisor = matrix[col][col];
                for (int k = col; k < 4; ++k) {
                    matrix[col][k] /= divisor;
                }
                for (int row = 0; row < 3; ++row) {
                    if (row == col) {
                        continue;
                    }
                    const double factor = matrix[row][col];
                    for (int k = col; k < 4; ++k) {
                        matrix[row][k] -= factor * matrix[col][k];
                    }
                }
            }

            const double a = matrix[0][3];
            const double b = matrix[1][3];
            const double c = matrix[2][3];
            return a + b * x + c * x * x;
        };

        auto drawFitCurve = [&](const std::vector<std::pair<double, double>> &samples, auto yFor,
                                const wxColour &color, int width = 2) {
            if (samples.size() < 2) {
                return;
            }
            const double minX = samples.front().first;
            const double maxX = samples.back().first;
            std::vector<wxPoint> points;
            const int steps = std::max(12, static_cast<int>((maxX - minX + 1.0) * 8.0));
            for (int step = 0; step <= steps; ++step) {
                const double t = static_cast<double>(step) / steps;
                const double x = minX + (maxX - minX) * t;
                const double value = fitAt(samples, x);
                const int px = axisX(x);
                points.push_back(wxPoint(px, yFor(value)));
            }
            drawPolyline(points, color, width);
        };

        drawLine(
            [](const auto &value) {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, ChartSample>) {
                    return value.energy;
                } else {
                    return value.energySum;
                }
            },
            wxColour(255, 105, 180));
        drawLine(
            [](const auto &value) {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, ChartSample>) {
                    return value.mood;
                } else {
                    return value.moodSum;
                }
            },
            wxColour(255, 235, 89));
        drawLine(
            [](const auto &value) {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, ChartSample>) {
                    return value.grounding;
                } else {
                    return value.groundingSum;
                }
            },
            wxColour(92, 214, 130));
        auto normalizedY = [&](double value) {
            return baseY - static_cast<int>(std::round(plotH * std::clamp(value, 0.0, 1.0)));
        };
        auto emgY = [&](double value) {
            return baseY - static_cast<int>(std::round(plotH * std::clamp(value, 0.0, 5.0) / 5.0));
        };

        auto translucent = [](const wxColour &color, double opacity) {
            const unsigned char alphaByte =
                static_cast<unsigned char>(std::round(std::clamp(opacity, 0.0, 1.0) * 255.0));
            return wxColour(color.Red(), color.Green(), color.Blue(), alphaByte);
        };
        const double curveAlpha = 0.10;
        const wxColour avgBaseColor(160, 166, 176);
        const wxColour avgColor = translucent(avgBaseColor, curveAlpha);
        const wxColour recordFitColor = translucent(wxColour(76, 142, 255), curveAlpha);
        const wxColour durationFitColor = translucent(wxColour(255, 143, 36), curveAlpha);
        const wxColour avgFitColor = translucent(wxColour(239, 73, 91), curveAlpha);
        drawPolyline(avgPoints, avgColor, 4);
        drawFitCurve(recordSamples, normalizedY, recordFitColor);
        drawFitCurve(durationSamples, normalizedY, durationFitColor);
        drawFitCurve(avgSamples, emgY, avgFitColor);
        dc.DestroyClippingRegion();

        dc.SetTextForeground(fg);
        int legendX = left;
        auto legend = [&](const wxColour &color, const wxString &text) {
            dc.SetBrush(wxBrush(color));
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(legendX, 10, 12, 8);
            dc.DrawText(text, legendX + 16, 6);
            legendX += dc.GetTextExtent(text).GetWidth() + 46;
        };
        legend(wxColour(76, 142, 255), wxString::FromUTF8(_("Records")));
        legend(wxColour(255, 183, 77), wxString::FromUTF8(_("Duration")));
        legend(wxColour(255, 105, 180), wxString::FromUTF8(_("Energy")));
        legend(wxColour(255, 235, 89), wxString::FromUTF8(_("Mood")));
        legend(wxColour(92, 214, 130), wxString::FromUTF8(_("Grounding")));
        legend(avgBaseColor, wxString::FromUTF8(_("EMG avg")));
    }

    UiThemeColors m_colors;
    std::vector<ChartBucket> m_buckets;
    std::vector<ChartSample> m_samples;
    std::string m_highlightedSampleKey;
    double m_zoom = 1.0;
    double m_pan = 0.0;
    bool m_dragging = false;
    int m_lastDragX = 0;
};

HistoryFrame::HistoryFrame(wxWindow *parent, ObservationStore *store, std::string theme,
                           bool weekStartsMonday, const std::vector<std::string> &quotes)
    : wxFrame(parent, wxID_ANY, wxString::FromUTF8(_("Statistics")), wxDefaultPosition,
              wxSize(1040, 760), wxDEFAULT_FRAME_STYLE | wxRESIZE_BORDER),
      m_store(store), m_quotes(quotes), m_theme(theme), m_weekStartsMonday(weekStartsMonday),
      m_anchor(wxDateTime::Today()) {
    if (m_store != nullptr) {
        m_store->load({});
        if (m_store->allRowCount() > 0) {
            m_anchor = toDateTime(m_store->allRowAt(m_store->allRowCount() - 1).promptedAt);
        }
    }

    const UiThemeColors &colors = uiThemeColors(m_theme);
    const wxFont uiFont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    SetFont(uiFont);
    SetBackgroundColour(colors.windowBg);
    SetForegroundColour(colors.windowFg);

    auto *root = new wxBoxSizer(wxVERTICAL);
    m_toolbar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                              wxTB_HORIZONTAL | wxTB_TEXT | wxTB_FLAT);
    m_toolbar->AddCheckTool(ID_MENU_CALENDAR, wxString::FromUTF8(_("Calendar")),
                            wxArtProvider::GetBitmap(wxART_GO_HOME, wxART_TOOLBAR, wxSize(16, 16)),
                            wxNullBitmap, wxString::FromUTF8("F1"));
    m_toolbar->AddSeparator();
    m_toolbar->AddCheckTool(
        ID_MENU_DAY, wxString::FromUTF8(_("Day")),
        wxArtProvider::GetBitmap(wxART_NORMAL_FILE, wxART_TOOLBAR, wxSize(16, 16)), wxNullBitmap,
        wxString::FromUTF8("F5"));
    m_toolbar->AddCheckTool(
        ID_MENU_WEEK, wxString::FromUTF8(_("Week")),
        wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxART_TOOLBAR, wxSize(16, 16)), wxNullBitmap,
        wxString::FromUTF8("F6"));
    m_toolbar->AddCheckTool(
        ID_MENU_MONTH, wxString::FromUTF8(_("Month")),
        wxArtProvider::GetBitmap(wxART_REPORT_VIEW, wxART_TOOLBAR, wxSize(16, 16)), wxNullBitmap,
        wxString::FromUTF8("F7"));
    m_toolbar->AddCheckTool(ID_MENU_YEAR, wxString::FromUTF8(_("Year")),
                            wxArtProvider::GetBitmap(wxART_HARDDISK, wxART_TOOLBAR, wxSize(16, 16)),
                            wxNullBitmap, wxString::FromUTF8("F8"));
    m_toolbar->AddSeparator();
    m_toolbar->AddTool(ID_MENU_TODAY, wxString::FromUTF8(_("Today")),
                       wxArtProvider::GetBitmap(wxART_TICK_MARK, wxART_TOOLBAR, wxSize(16, 16)),
                       wxString::FromUTF8("Ctrl+T"));
    m_toolbar->AddTool(ID_MENU_PREVIOUS, wxString::FromUTF8(_("Previous")),
                       wxArtProvider::GetBitmap(wxART_GO_BACK, wxART_TOOLBAR, wxSize(16, 16)),
                       wxString::FromUTF8("PageUp"));
    m_toolbar->AddTool(ID_MENU_NEXT, wxString::FromUTF8(_("Next")),
                       wxArtProvider::GetBitmap(wxART_GO_FORWARD, wxART_TOOLBAR, wxSize(16, 16)),
                       wxString::FromUTF8("PageDown"));
    m_toolbar->AddSeparator();
    m_toolbar->AddTool(wxID_CLOSE, wxString::FromUTF8(_("Close")),
                       wxArtProvider::GetBitmap(wxART_CLOSE, wxART_TOOLBAR, wxSize(16, 16)),
                       wxString::FromUTF8("Esc"));
    m_toolbar->Realize();
    m_toolbar->SetBackgroundColour(blendOver(colors.windowBg, colors.surfaceBg, 0.62));
    m_toolbar->SetForegroundColour(colors.windowFg);

    auto *header = new wxBoxSizer(wxHORIZONTAL);
    m_title = new wxStaticText(this, wxID_ANY, "");
    wxFont titleFont = GetFont();
    titleFont.SetPointSize(titleFont.GetPointSize() + 5);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_title->SetFont(titleFont);
    m_title->SetForegroundColour(colors.windowFg);

    m_eachYear = new wxCheckBox(this, wxID_ANY, wxString::FromUTF8(_("Each Year")));
    m_eachYear->SetValue(true);
    m_eachYear->SetForegroundColour(colors.listFg);

    header->Add(m_title, 1, wxALIGN_CENTER_VERTICAL);
    header->Add(m_eachYear, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);

    m_calendar = new ObservationCalendarPanel(this, colors, m_weekStartsMonday);
    m_calendar->setSelected(m_anchor);
    m_calendar->setDaySelectedHandler([this](wxDateTime date) {
        m_anchor = date;
        reloadTableRecords();
        updateSelectedDaySummary();
        updateTitle();
    });
    m_calendar->setMonthChangedHandler([this](wxDateTime date) {
        m_anchor = date;
        reloadTableRecords();
        updateSelectedDaySummary();
        updateTitle();
    });

    m_daySummaryPanel = new ThemeGlassPanel(this, colors, colors.cardBg, 12, 0.72);
    auto *daySummarySizer = new wxBoxSizer(wxVERTICAL);
    m_daySummaryDate = new wxStaticText(m_daySummaryPanel, wxID_ANY, "");
    wxFont daySummaryTitleFont = GetFont();
    daySummaryTitleFont.SetPointSize(daySummaryTitleFont.GetPointSize() + 3);
    daySummaryTitleFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_daySummaryDate->SetFont(daySummaryTitleFont);
    m_daySummaryDate->SetForegroundColour(colors.windowFg);
    auto addSummaryLine = [&](wxStaticText *&target) {
        target = new wxStaticText(m_daySummaryPanel, wxID_ANY, "");
        target->SetForegroundColour(colors.listFg);
        daySummarySizer->Add(target, 0, wxBOTTOM, 8);
    };
    daySummarySizer->Add(m_daySummaryDate, 0, wxBOTTOM, 16);
    addSummaryLine(m_daySummaryRecords);
    addSummaryLine(m_daySummaryEmpty);
    addSummaryLine(m_daySummaryDuration);
    addSummaryLine(m_daySummaryEmg);
    m_daySummaryFooter = new wxStaticText(m_daySummaryPanel, wxID_ANY, "");
    wxFont daySummaryFooterFont = GetFont();
    daySummaryFooterFont.SetPointSize(std::max(7, daySummaryFooterFont.GetPointSize() - 2));
    m_daySummaryFooter->SetFont(daySummaryFooterFont);
    m_daySummaryFooter->SetForegroundColour(colors.mutedFg);
    m_daySummaryFooter->SetLabel(
        wxString::Format(wxString::FromUTF8(_("Version %s · Copyright (C) %d %s")),
                         wxString::FromUTF8(PROJECT_VERSION), PROJECT_YEAR,
                         wxString::FromUTF8(PROJECT_AUTHOR)));
    auto *daySummaryOuter = new wxBoxSizer(wxVERTICAL);
    daySummaryOuter->Add(daySummarySizer, 0, wxEXPAND | wxALL, 14);
    daySummaryOuter->AddStretchSpacer(1);
    daySummaryOuter->Add(m_daySummaryFooter, 0, wxLEFT | wxRIGHT | wxBOTTOM, 14);
    m_daySummaryPanel->SetSizer(daySummaryOuter);

    m_metricsPanel = new wxPanel(this);
    m_metricsPanel->SetBackgroundColour(colors.windowBg);
    m_metricsSizer = new wxBoxSizer(wxHORIZONTAL);
    m_metricsPanel->SetSizer(m_metricsSizer);
    m_chart = new StatisticsChartPanel(this, colors);
    m_tableTitle = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(_("Top activities")));
    wxFont tableTitleFont = GetFont();
    tableTitleFont.SetPointSize(tableTitleFont.GetPointSize() + 2);
    tableTitleFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_tableTitle->SetFont(tableTitleFont);
    m_tableTitle->SetForegroundColour(colors.listFg);
    auto *tableFrame = new ThemeGlassPanel(this, colors, colors.surfaceBg, 10, 0.74);
    auto *tableFrameSizer = new wxBoxSizer(wxVERTICAL);
    tableFrame->SetSizer(tableFrameSizer);
    m_table = new ObservationRecordTable(tableFrame, colors);
    m_table->SetFont(uiFont);
    m_table->setSelectionHandler([this](const Observation *observation) {
        if (observation != nullptr && (m_mode == ViewMode::Day || m_mode == ViewMode::Week)) {
            m_chart->setHighlightedSample(observationKey(*observation));
            return;
        }
        m_chart->setHighlightedSample("");
    });
    m_table->setRecordHandlers(
        [this](const Observation &observation) { editObservation(observation); },
        [this](const Observation &observation) { deleteObservation(observation); });
    m_table->setBulkDeleteHandler([this]() { deleteSelectedObservations(); });
    m_table->setPeriodNavigationHandler([this](int keyCode) { navigatePeriodFromTable(keyCode); });
    tableFrameSizer->Add(m_table, 1, wxEXPAND | wxALL, 10);

    root->Add(m_toolbar, 0, wxEXPAND);
    root->Add(header, 0, wxALL | wxEXPAND, 16);
    auto *calendarRow = new wxBoxSizer(wxHORIZONTAL);
    calendarRow->Add(m_calendar, 0, wxRIGHT, 18);
    calendarRow->Add(m_daySummaryPanel, 1, wxEXPAND);
    root->Add(calendarRow, 0, wxLEFT | wxRIGHT | wxEXPAND, 16);
    root->Add(m_metricsPanel, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
    root->Add(m_chart, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
    root->Add(m_tableTitle, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
    root->Add(tableFrame, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 16);
    SetSizer(root);

    m_eachYear->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { render(); });
    Bind(wxEVT_TOOL, [this](wxCommandEvent &) { setMode(ViewMode::Calendar); }, ID_MENU_CALENDAR);
    Bind(wxEVT_TOOL, [this](wxCommandEvent &) { setMode(ViewMode::Day); }, ID_MENU_DAY);
    Bind(wxEVT_TOOL, [this](wxCommandEvent &) { setMode(ViewMode::Week); }, ID_MENU_WEEK);
    Bind(wxEVT_TOOL, [this](wxCommandEvent &) { setMode(ViewMode::Month); }, ID_MENU_MONTH);
    Bind(wxEVT_TOOL, [this](wxCommandEvent &) { setMode(ViewMode::Year); }, ID_MENU_YEAR);
    Bind(wxEVT_TOOL, [this](wxCommandEvent &) { goToday(); }, ID_MENU_TODAY);
    Bind(wxEVT_TOOL, [this](wxCommandEvent &) { movePeriod(-1); }, ID_MENU_PREVIOUS);
    Bind(wxEVT_TOOL, [this](wxCommandEvent &) { movePeriod(1); }, ID_MENU_NEXT);
    Bind(wxEVT_TOOL, [this](wxCommandEvent &) { Close(); }, wxID_CLOSE);
    Bind(wxEVT_CHAR_HOOK, &HistoryFrame::onCharHook, this);
    Bind(wxEVT_CLOSE_WINDOW, &HistoryFrame::onClose, this);

    hookDisplaySurface(m_daySummaryPanel);
    hookDisplaySurface(m_metricsPanel);
    hookDisplaySurface(m_calendar);
    hookDisplaySurface(m_chart);
    hookDisplaySurface(m_title);
    hookDisplaySurface(tableFrame);
    for (wxWindow *summaryLine :
         {static_cast<wxWindow *>(m_daySummaryDate), static_cast<wxWindow *>(m_daySummaryRecords),
          static_cast<wxWindow *>(m_daySummaryEmpty), static_cast<wxWindow *>(m_daySummaryDuration),
          static_cast<wxWindow *>(m_daySummaryEmg), static_cast<wxWindow *>(m_daySummaryFooter)}) {
        hookDisplaySurface(summaryLine);
    }

    CentreOnParent();
    SetFocus();
    render();
}

void HistoryFrame::hookDisplaySurface(wxWindow *surface) {
    if (surface == nullptr) {
        return;
    }
    surface->SetCanFocus(false);
    surface->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
        SetFocus();
        event.Skip();
    });
    surface->Bind(wxEVT_CHAR_HOOK, &HistoryFrame::onCharHook, this);
}

void HistoryFrame::onCharHook(wxKeyEvent &event) {
    const int keyCode = event.GetKeyCode();
    if (keyCode == WXK_ESCAPE) {
        Close();
        return;
    }
    if (keyCode == WXK_DELETE) {
        deleteSelectedObservations();
        return;
    }
    if (event.ControlDown() && (keyCode == 'T' || keyCode == 't')) {
        goToday();
        return;
    }
    if (keyCode == WXK_HOME) {
        goToday();
        return;
    }
    if (keyCode == WXK_LEFT || keyCode == WXK_RIGHT) {
        if (m_mode == ViewMode::Calendar) {
            m_anchor.Add(wxDateSpan::Days(keyCode == WXK_LEFT ? -1 : 1));
            renderCalendar();
        } else {
            const int direction = keyCode == WXK_LEFT ? -1 : 1;
            int modeIndex = static_cast<int>(m_mode) - static_cast<int>(ViewMode::Day);
            modeIndex = (modeIndex + direction + 4) % 4;
            setMode(static_cast<ViewMode>(static_cast<int>(ViewMode::Day) + modeIndex));
        }
        return;
    }
    if (keyCode == WXK_UP || keyCode == WXK_DOWN) {
        const bool tableActive = m_table != nullptr && m_table->IsShown() && m_table->HasFocus();
        if (tableActive && m_table->hasSelection()) {
            event.Skip();
            return;
        }
        if (m_mode == ViewMode::Calendar) {
            m_anchor.Add(wxDateSpan::Days(keyCode == WXK_UP ? -7 : 7));
            renderCalendar();
        } else {
            movePeriod(keyCode == WXK_UP ? -1 : 1);
        }
        return;
    }
    if (keyCode == WXK_F1) {
        setMode(ViewMode::Calendar);
        return;
    }
    if (keyCode == WXK_F5) {
        setMode(ViewMode::Day);
        return;
    }
    if (keyCode == WXK_F6) {
        setMode(ViewMode::Week);
        return;
    }
    if (keyCode == WXK_F7) {
        setMode(ViewMode::Month);
        return;
    }
    if (keyCode == WXK_F8) {
        setMode(ViewMode::Year);
        return;
    }
    if (keyCode == WXK_PAGEUP) {
        movePeriod(-1);
        return;
    }
    if (keyCode == WXK_PAGEDOWN) {
        movePeriod(1);
        return;
    }
    event.Skip();
}

void HistoryFrame::setMode(ViewMode mode) {
    m_mode = mode;
    render();
}

void HistoryFrame::movePeriod(int delta) {
    switch (m_mode) {
    case ViewMode::Day:
        m_anchor.Add(wxDateSpan::Days(delta));
        break;
    case ViewMode::Week:
        m_anchor.Add(wxDateSpan::Weeks(delta));
        break;
    case ViewMode::Month:
    case ViewMode::Calendar:
        m_anchor.Add(wxDateSpan::Months(delta));
        break;
    case ViewMode::Year:
        m_anchor.Add(wxDateSpan::Years(delta));
        break;
    }
    render();
}

void HistoryFrame::render() {
    if (m_mode == ViewMode::Calendar) {
        renderCalendar();
    } else {
        renderStatistics();
    }
}

void HistoryFrame::updateTitle() {
    const bool eachYear = m_eachYear->GetValue();
    m_title->SetLabel(periodTitle(m_mode, m_anchor, eachYear));
    m_eachYear->Show(m_mode == ViewMode::Day || m_mode == ViewMode::Week ||
                     m_mode == ViewMode::Month);
}

void HistoryFrame::updateToolbar() {
    if (m_toolbar == nullptr) {
        return;
    }
    const int viewIds[] = {
        ID_MENU_CALENDAR, ID_MENU_DAY, ID_MENU_WEEK, ID_MENU_MONTH, ID_MENU_YEAR,
    };
    for (int viewId : viewIds) {
        m_toolbar->ToggleTool(viewId, false);
    }

    int id = ID_MENU_CALENDAR;
    switch (m_mode) {
    case ViewMode::Calendar:
        id = ID_MENU_CALENDAR;
        break;
    case ViewMode::Day:
        id = ID_MENU_DAY;
        break;
    case ViewMode::Week:
        id = ID_MENU_WEEK;
        break;
    case ViewMode::Month:
        id = ID_MENU_MONTH;
        break;
    case ViewMode::Year:
        id = ID_MENU_YEAR;
        break;
    }
    m_toolbar->ToggleTool(id, true);
}

void HistoryFrame::renderCalendar() {
    reloadCalendarRecords();
    m_calendar->Show();
    m_daySummaryPanel->Show();
    m_metricsPanel->Hide();
    m_chart->Hide();
    m_tableTitle->Hide();
    m_table->Show();
    m_calendar->setMonth(m_anchor);
    m_calendar->setSelected(m_anchor);
    updateTitle();
    updateToolbar();
    reloadTableRecords();
    updateSelectedDaySummary();
    Layout();
}

void HistoryFrame::rebuildMetrics(const std::vector<Observation> &selected) {
    m_metricsSizer->Clear(true);

    double energy = 0.0;
    double mood = 0.0;
    double grounding = 0.0;
    long totalDuration = 0;
    int emptyCount = 0;
    int emgCount = 0;
    for (const Observation &observation : selected) {
        totalDuration += durationSeconds(observation);
        if (trim(observation.activity).empty()) {
            ++emptyCount;
        }
        if (observationEmgMissing(observation)) {
            continue;
        }
        ++emgCount;
        energy += observation.energy;
        mood += observation.mood;
        grounding += observation.grounding;
    }

    const double emgAverageCount = static_cast<double>(emgCount);
    const UiThemeColors &colors = uiThemeColors(m_theme);

    auto addMetric = [&](const wxArtID &artId, const wxString &label, const wxString &value) {
        auto *panel = new ThemeGlassPanel(m_metricsPanel, colors, colors.cardBg, 12, 0.72);
        auto *row = new wxBoxSizer(wxHORIZONTAL);
        auto *icon = new ThemeIconBadge(
            panel, colors, wxArtProvider::GetBitmap(artId, wxART_OTHER, wxSize(24, 24)));
        auto *texts = new wxBoxSizer(wxVERTICAL);
        auto *valueText = new wxStaticText(panel, wxID_ANY, value);
        auto *labelText = new wxStaticText(panel, wxID_ANY, label);
        wxFont valueFont = GetFont();
        valueFont.SetPointSize(valueFont.GetPointSize() + 3);
        valueFont.SetWeight(wxFONTWEIGHT_BOLD);
        valueText->SetFont(valueFont);
        valueText->SetForegroundColour(colors.listFg);
        labelText->SetForegroundColour(colors.mutedFg);
        texts->Add(valueText, 0, wxBOTTOM, 2);
        texts->Add(labelText, 0);
        row->Add(icon, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 10);
        row->Add(texts, 1, wxALIGN_CENTER_VERTICAL);
        auto *cardInner = new wxBoxSizer(wxVERTICAL);
        cardInner->Add(row, 1, wxEXPAND | wxALL, 12);
        panel->SetSizer(cardInner);
        hookDisplaySurface(panel);
        hookDisplaySurface(icon);
        hookDisplaySurface(valueText);
        hookDisplaySurface(labelText);
        m_metricsSizer->Add(panel, 1, wxRIGHT | wxEXPAND, 8);
    };

    addMetric(wxART_REPORT_VIEW, wxString::FromUTF8(_("Records")),
              wxString::Format("%zu", selected.size()));
    addMetric(wxART_WARNING, wxString::FromUTF8(_("Empty notes")),
              wxString::Format("%d", emptyCount));
    addMetric(wxART_TIP, wxString::FromUTF8(_("Total prompt duration")),
              wxString::FromUTF8(formatDuration(totalDuration).c_str()));
    addMetric(wxART_INFORMATION, wxString::FromUTF8(_("Average energy")),
              emgCount == 0 ? "-"
                            : wxString::FromUTF8(formatDouble(energy / emgAverageCount).c_str()));
    addMetric(wxART_INFORMATION, wxString::FromUTF8(_("Average mood")),
              emgCount == 0 ? "-"
                            : wxString::FromUTF8(formatDouble(mood / emgAverageCount).c_str()));
    addMetric(wxART_INFORMATION, wxString::FromUTF8(_("Average grounding")),
              emgCount == 0
                  ? "-"
                  : wxString::FromUTF8(formatDouble(grounding / emgAverageCount).c_str()));
}

std::vector<Observation> HistoryFrame::currentViewRows() const {
    std::vector<Observation> rows;
    if (m_store == nullptr) {
        return rows;
    }
    rows.reserve(m_store->rowCount());
    for (std::size_t index = 0; index < m_store->rowCount(); ++index) {
        rows.push_back(m_store->rowAt(index));
    }
    return rows;
}

ObservationCriteria HistoryFrame::tableCriteria() const {
    ObservationCriteria criteria;
    criteria.filter = ObservationFilter::Day;
    criteria.anchorDate = m_anchor.FormatISODate().ToStdString();
    criteria.weekStartsMonday = m_weekStartsMonday;
    return criteria;
}

ObservationCriteria HistoryFrame::statisticsCriteria() const {
    ObservationCriteria criteria;
    criteria.anchorDate = m_anchor.FormatISODate().ToStdString();
    criteria.eachYear = m_eachYear->GetValue();
    criteria.weekStartsMonday = m_weekStartsMonday;
    switch (m_mode) {
    case ViewMode::Day:
        criteria.filter = ObservationFilter::Day;
        break;
    case ViewMode::Week:
        criteria.filter = ObservationFilter::Week;
        break;
    case ViewMode::Month:
        criteria.filter = ObservationFilter::Month;
        break;
    case ViewMode::Year:
        criteria.filter = ObservationFilter::Year;
        break;
    case ViewMode::Calendar:
        criteria.filter = ObservationFilter::All;
        break;
    }
    return criteria;
}

void HistoryFrame::reloadTableRecords(long selectIndex) {
    if (m_store == nullptr || m_table == nullptr) {
        return;
    }
    if (m_mode == ViewMode::Calendar) {
        m_store->load(tableCriteria());
    } else {
        m_store->load(statisticsCriteria());
    }
    m_table->setRecords(currentViewRows(), selectIndex);
}

void HistoryFrame::reloadCalendarRecords() {
    if (m_store == nullptr || m_calendar == nullptr) {
        return;
    }
    std::vector<Observation> allRows;
    allRows.reserve(m_store->allRowCount());
    for (std::size_t index = 0; index < m_store->allRowCount(); ++index) {
        allRows.push_back(m_store->allRowAt(index));
    }
    m_calendar->setObservations(allRows);
}

void HistoryFrame::updateSelectedDaySummary() {
    if (m_store != nullptr && m_mode == ViewMode::Calendar) {
        m_store->load(tableCriteria());
    }
    const std::vector<Observation> selected = currentViewRows();
    double energy = 0.0;
    double mood = 0.0;
    double grounding = 0.0;
    long totalDuration = 0;
    int emptyCount = 0;
    int emgCount = 0;
    for (const Observation &observation : selected) {
        totalDuration += durationSeconds(observation);
        if (trim(observation.activity).empty()) {
            ++emptyCount;
        }
        if (observationEmgMissing(observation)) {
            continue;
        }
        ++emgCount;
        energy += observation.energy;
        mood += observation.mood;
        grounding += observation.grounding;
    }

    const double emgAverageCount = static_cast<double>(emgCount);
    m_daySummaryDate->SetLabel(m_anchor.FormatISODate());
    m_daySummaryRecords->SetLabel(wxString::FromUTF8(_("Records")) + ": " +
                                  wxString::Format("%zu", selected.size()));
    m_daySummaryEmpty->SetLabel(wxString::FromUTF8(_("Empty notes")) + ": " +
                                wxString::Format("%d", emptyCount));
    m_daySummaryDuration->SetLabel(wxString::FromUTF8(_("Total prompt duration")) + ": " +
                                   wxString::FromUTF8(formatDuration(totalDuration).c_str()));
    if (emgCount == 0) {
        m_daySummaryEmg->SetLabel(wxString::FromUTF8(_("Average E/M/G")) + ": -");
    } else {
        const double avgEnergy = energy / emgAverageCount;
        const double avgMood = mood / emgAverageCount;
        const double avgGrounding = grounding / emgAverageCount;
        m_daySummaryEmg->SetLabel(
            wxString::FromUTF8(_("Average E/M/G")) + ": " +
            scoreWithEmoji(energyEmoji(avgEnergy), avgEnergy) + " / " +
            scoreWithEmoji(moodEmoji(avgMood), avgMood) + " / " +
            scoreWithEmoji(groundingEmoji(avgGrounding), avgGrounding));
    }
    m_daySummaryPanel->Layout();
}

void HistoryFrame::renderStatistics(long selectIndex) {
    m_calendar->Hide();
    m_daySummaryPanel->Hide();
    m_metricsPanel->Show();
    m_chart->Show();
    m_tableTitle->Show();
    m_table->Show();
    m_tableTitle->SetLabel(wxString::FromUTF8(_("Top activities")));
    updateTitle();
    updateToolbar();

    if (m_store != nullptr) {
        m_store->load(statisticsCriteria());
    }
    const std::vector<Observation> selected = currentViewRows();
    const bool eachYear = m_eachYear->GetValue();
    std::vector<ChartBucket> buckets = makeBuckets(m_mode, m_anchor, eachYear, m_weekStartsMonday);
    std::vector<ChartSample> samples;
    for (const Observation &observation : selected) {
        const int index = bucketIndex(observation, m_mode, m_weekStartsMonday);
        if (index < 0 || index >= static_cast<int>(buckets.size())) {
            continue;
        }
        ChartBucket &bucket = buckets[index];
        ++bucket.records;
        bucket.durationSeconds += durationSeconds(observation);
        if (!observationEmgMissing(observation)) {
            ++bucket.emgRecords;
            bucket.energySum += observation.energy;
            bucket.moodSum += observation.mood;
            bucket.groundingSum += observation.grounding;
            if (m_mode == ViewMode::Day || m_mode == ViewMode::Week) {
                samples.push_back({sampleAxisX(observation, m_mode, m_weekStartsMonday),
                                   observation.energy, observation.mood, observation.grounding,
                                   observationKey(observation)});
            }
        }
    }
    std::sort(samples.begin(), samples.end(),
              [](const ChartSample &a, const ChartSample &b) { return a.x < b.x; });

    rebuildMetrics(selected);
    m_chart->setData(std::move(buckets), std::move(samples));
    m_table->setRecords(selected, selectIndex);
    m_metricsPanel->Layout();
    Layout();
}

void HistoryFrame::goToday() {
    m_anchor = wxDateTime::Today();
    render();
}

void HistoryFrame::activateWindow() {
    Show(true);
    Raise();
    SetFocus();
}

void HistoryFrame::onClose(wxCloseEvent &event) {
    try {
        if (m_store != nullptr) {
            m_store->commit();
        }
    } catch (const std::exception &ex) {
        wxMessageBox(wxString::FromUTF8(ex.what()), wxString::FromUTF8(_("Save error")),
                     wxOK | wxICON_ERROR, this);
        event.Veto();
        return;
    }
    event.Skip();
}

void HistoryFrame::refreshAfterMutation(long selectIndex) {
    reloadCalendarRecords();
    if (m_mode == ViewMode::Calendar) {
        reloadTableRecords(selectIndex);
        updateSelectedDaySummary();
        updateTitle();
        return;
    }
    renderStatistics(selectIndex);
}

void HistoryFrame::editObservation(const Observation &original) {
    CallAfter([this, original]() {
        ObservePromptDefaults defaults;
        defaults.theme = m_theme;
        defaults.weekStartsMonday = m_weekStartsMonday;
        defaults.energy = original.energy;
        defaults.mood = original.mood;
        defaults.grounding = original.grounding;
        defaults.quote = original.quote;
        defaults.quotes = m_quotes;
        if (defaults.quotes.empty() && !original.quote.empty()) {
            defaults.quotes.push_back(original.quote);
        }
        defaults.editing = original;
        defaults.store = m_store;

        ObservationDialog dialog(this, defaults);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }

        if (m_store != nullptr) {
            m_store->update(original.id, dialog.observation());
        }
        refreshAfterMutation();
    });
}

void HistoryFrame::navigatePeriodFromTable(int keyCode) {
    if (keyCode != WXK_UP && keyCode != WXK_DOWN) {
        return;
    }
    if (m_mode == ViewMode::Calendar) {
        m_anchor.Add(wxDateSpan::Days(keyCode == WXK_UP ? -7 : 7));
        renderCalendar();
        return;
    }
    movePeriod(keyCode == WXK_UP ? -1 : 1);
}

void HistoryFrame::deleteSelectedObservations() {
    if (m_table == nullptr || m_store == nullptr) {
        return;
    }

    const std::vector<Observation> records = m_table->selectedRecords();
    if (records.empty()) {
        return;
    }

    long selectAfter = m_table->selectedIndex();
    for (const Observation &observation : records) {
        m_store->remove(observation.id);
    }
    refreshAfterMutation(selectAfter);
}

void HistoryFrame::deleteObservation(const Observation &original) {
    long selectAfter = -1;
    if (m_table != nullptr) {
        const long deletedIndex = m_table->selectedIndex();
        if (deletedIndex >= 0) {
            selectAfter = deletedIndex;
        }
    }

    if (m_store != nullptr) {
        m_store->remove(original.id);
    }
    refreshAfterMutation(selectAfter);
}
