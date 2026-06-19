#ifndef HISTORY_FRAME_H
#define HISTORY_FRAME_H

#include "ObservationStore.h"

#include <wx/checkbox.h>
#include <wx/wx.h>

#include <string>
#include <vector>

class ObservationCalendarPanel;
class ObservationRecordTable;
class StatisticsChartPanel;

class HistoryFrame : public wxDialog {
  public:
    enum class ViewMode {
        Calendar,
        Day,
        Week,
        Month,
        Year,
    };

    HistoryFrame(wxWindow *parent, ObservationStore *store, std::string theme, bool weekStartsMonday,
                 const std::vector<std::string> &quotes = {});

  private:
    void onCharHook(wxKeyEvent &event);
    void onClose(wxCloseEvent &event);
    void setMode(ViewMode mode);
    void movePeriod(int delta);
    void render();
    void renderCalendar();
    void renderStatistics(long selectIndex = -1);
    void updateTitle();
    void updateToolbar();
    void hookDisplaySurface(wxWindow *surface);
    void rebuildMetrics(const std::vector<Observation> &selected);
    void updateSelectedDaySummary();
    void reloadTableRecords(long selectIndex = -1);
    void reloadCalendarRecords();
    std::vector<Observation> currentViewRows() const;
    ObservationCriteria tableCriteria() const;
    ObservationCriteria statisticsCriteria() const;
    void goToday();
    void editObservation(const Observation &original);
    void deleteObservation(const Observation &original);
    void deleteSelectedObservations();
    void navigatePeriodFromTable(int keyCode);
    void refreshAfterMutation(long selectIndex = -1);

    ObservationStore *m_store = nullptr;
    std::vector<std::string> m_quotes;
    std::string m_theme;
    bool m_weekStartsMonday = true;
    ViewMode m_mode = ViewMode::Calendar;
    wxDateTime m_anchor;
    wxToolBar *m_toolbar = nullptr;
    wxStaticText *m_title = nullptr;
    wxCheckBox *m_eachYear = nullptr;
    ObservationCalendarPanel *m_calendar = nullptr;
    wxPanel *m_daySummaryPanel = nullptr;
    wxStaticText *m_daySummaryDate = nullptr;
    wxStaticText *m_daySummaryRecords = nullptr;
    wxStaticText *m_daySummaryEmpty = nullptr;
    wxStaticText *m_daySummaryDuration = nullptr;
    wxStaticText *m_daySummaryEmg = nullptr;
    wxStaticText *m_daySummaryFooter = nullptr;
    wxPanel *m_metricsPanel = nullptr;
    wxBoxSizer *m_metricsSizer = nullptr;
    StatisticsChartPanel *m_chart = nullptr;
    wxStaticText *m_tableTitle = nullptr;
    ObservationRecordTable *m_table = nullptr;
};

#endif // HISTORY_FRAME_H