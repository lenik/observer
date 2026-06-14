#ifndef STATISTICS_DIALOG_H
#define STATISTICS_DIALOG_H

#include "Observation.h"

#include <string>
#include <vector>
#include <wx/checkbox.h>
#include <wx/wx.h>

class ObservationCalendarPanel;
class ObservationRecordTable;
class StatisticsChartPanel;

class StatisticsDialog : public wxDialog {
public:
    enum class ViewMode {
        Calendar,
        Day,
        Week,
        Month,
        Year,
    };

    StatisticsDialog(wxWindow* parent, std::vector<Observation> observations, std::string theme, bool weekStartsMonday);

private:
    void onCharHook(wxKeyEvent& event);
    void setMode(ViewMode mode);
    void movePeriod(int delta);
    void render();
    void renderCalendar();
    void renderStatistics();
    void updateTitle();
    void updateToolbar();
    void rebuildMetrics(const std::vector<Observation>& selected);
    void updateSelectedDaySummary();
    std::vector<Observation> selectedDayRecords() const;
    void goToday();

    std::vector<Observation> observations_;
    std::string theme_;
    bool weekStartsMonday_ = true;
    ViewMode mode_ = ViewMode::Calendar;
    wxDateTime anchor_;
    wxToolBar* toolbar_ = nullptr;
    wxStaticText* title_ = nullptr;
    wxCheckBox* eachYear_ = nullptr;
    ObservationCalendarPanel* calendar_ = nullptr;
    wxPanel* daySummaryPanel_ = nullptr;
    wxStaticText* daySummaryDate_ = nullptr;
    wxStaticText* daySummaryRecords_ = nullptr;
    wxStaticText* daySummaryEmpty_ = nullptr;
    wxStaticText* daySummaryDuration_ = nullptr;
    wxStaticText* daySummaryEnergy_ = nullptr;
    wxStaticText* daySummaryMood_ = nullptr;
    wxStaticText* daySummaryGrounding_ = nullptr;
    wxPanel* metricsPanel_ = nullptr;
    wxBoxSizer* metricsSizer_ = nullptr;
    StatisticsChartPanel* chart_ = nullptr;
    wxStaticText* tableTitle_ = nullptr;
    ObservationRecordTable* table_ = nullptr;
};

#endif
