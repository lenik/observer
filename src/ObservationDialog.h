#ifndef OBSERVATION_DIALOG_H
#define OBSERVATION_DIALOG_H

#include "Observation.h"
#include "RenderDriver.h"

#include <wx/stc/stc.h>
#include <wx/textctrl.h>
#include <wx/wx.h>

#include <random>

class QuoteCanvas;
class RatingControl;

class ObservationDialog : public wxDialog {
  public:
    explicit ObservationDialog(wxWindow *parent, const ObservePromptDefaults &defaults);

    Observation observation() const;
    double intervalSeconds() const;
    void animateIn();

  private:
    void onCharHook(wxKeyEvent &event);
    void submit();
    void skip();
    void snooze();
    void quit();
    void finishWithResult(int result);
    void animateOut();
    void showStatistics();
    std::string currentTimestamp() const;
    void showRandomQuote();
    void toggleIntervalUnit();
    std::string activityText() const;

    std::string m_quote;
    std::string m_promptedAt;
    std::string m_theme;
    std::vector<std::string> m_quotes;
    std::vector<Observation> m_history;
    std::size_t m_quoteIndex = 0;
    std::mt19937 m_quoteRng;
    bool m_weekStartsMonday = true;
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
    wxPoint m_animationStartPosition;
    wxPoint m_animationFinalPosition;
    int m_finalOpacity = 178;
    bool m_hasTransparency = false;
    bool m_closing = false;
    bool m_intervalInSeconds = false;
};

#endif
