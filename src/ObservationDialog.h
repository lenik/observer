#ifndef OBSERVATION_DIALOG_H
#define OBSERVATION_DIALOG_H

#include "Observation.h"
#include "RenderDriver.h"

#include <wx/stc/stc.h>
#include <wx/textctrl.h>
#include <wx/wx.h>

#include <optional>
#include <random>
#include <string>
#include <vector>

class QuoteCanvas;
class RatingControl;

struct ObservationLayoutSnapshot {
    std::string quote;
    int windowWidth = 0;
    int windowHeight = 0;
    int quoteX = 0;
    int quoteY = 0;
    int quoteWidth = 0;
    int quoteHeight = 0;
};

class ObservationDialog : public wxDialog {
  public:
    explicit ObservationDialog(wxWindow *parent, const ObservePromptDefaults &defaults);

    Observation observation() const;
    double intervalSeconds() const;
    void animateIn();
    void showStatistics();
    bool isStatisticsOpen() const { return m_statisticsOpen; }
    ObservationLayoutSnapshot captureLayoutSnapshot() const;
    static ObservationLayoutSnapshot captureLayoutSnapshot(const ObservePromptDefaults &defaults);

  private:
    void onCharHook(wxKeyEvent &event);
    void submit();
    void skip();
    void snooze();
    void quit();
    void finishWithResult(int result);
    void animateOut();
    std::string currentTimestamp() const;
    void showRandomQuote();
    void toggleIntervalUnit();
    void refitDialogLayout();
    void updateAnimationAnchors();
    void paintWindowChrome(wxPaintEvent &event);
    void onFirstShow(wxShowEvent &event);
    wxSize goldenRatioWindowSize(const wxSize &contentMin) const;
    std::string activityText() const;

    std::string m_quote;
    std::string m_promptedAt;
    std::string m_theme;
    std::optional<Observation> m_editing;
    ObservationStore* m_store = nullptr;
    std::vector<std::string> m_quotes;
    std::size_t m_quoteIndex = 0;
    std::mt19937 m_quoteRng;
    bool m_weekStartsMonday = true;
    bool m_editMode = false;
    QuoteCanvas *m_quoteCanvas = nullptr;
    RatingControl *m_energyRating = nullptr;
    RatingControl *m_moodRating = nullptr;
    RatingControl *m_groundingRating = nullptr;
    wxTextCtrl *m_intervalCtrl = nullptr;
    wxStaticText *m_intervalUnitLabel = nullptr;
    wxStaticText *m_quitLabel = nullptr;
    wxStaticText *m_submitLabel = nullptr;
    wxStaticText *m_skipLabel = nullptr;
    wxStaticText *m_nextPromptLabel = nullptr;
    wxStyledTextCtrl *m_activityCtrl = nullptr;
    wxPanel *m_contentPanel = nullptr;
    wxPoint m_animationStartPosition;
    wxPoint m_animationFinalPosition;
    bool m_closing = false;
    bool m_intervalInSeconds = false;
    bool m_statisticsOpen = false;
    bool m_animationStarted = false;
    wxColour m_chromeBg;
    wxColour m_borderColour;
    wxColour m_shadowColour;
};

#endif
