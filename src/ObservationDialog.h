#ifndef OBSERVATION_DIALOG_H
#define OBSERVATION_DIALOG_H

#include "Observation.h"
#include "RenderDriver.h"

#include <random>
#include <wx/stc/stc.h>
#include <wx/textctrl.h>
#include <wx/wx.h>

class QuoteCanvas;
class RatingControl;

class ObservationDialog : public wxDialog {
public:
    explicit ObservationDialog(wxWindow* parent, const ObservePromptDefaults& defaults);

    Observation observation() const;
    double intervalSeconds() const;
    void animateIn();

private:
    void onCharHook(wxKeyEvent& event);
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

    std::string quote_;
    std::string promptedAt_;
    std::string theme_;
    std::vector<std::string> quotes_;
    std::vector<Observation> history_;
    std::size_t quoteIndex_ = 0;
    std::mt19937 quoteRng_;
    bool weekStartsMonday_ = true;
    QuoteCanvas* quoteCanvas_ = nullptr;
    RatingControl* energyRating_ = nullptr;
    RatingControl* moodRating_ = nullptr;
    RatingControl* groundingRating_ = nullptr;
    wxTextCtrl* intervalCtrl_ = nullptr;
    wxStaticText* intervalUnitLabel_ = nullptr;
    wxStaticText* quitLabel_ = nullptr;
    wxStaticText* submitLabel_ = nullptr;
    wxStaticText* skipLabel_ = nullptr;
    wxStaticText* nextPromptLabel_ = nullptr;
    wxStyledTextCtrl* activityCtrl_ = nullptr;
    wxPoint animationStartPosition_;
    wxPoint animationFinalPosition_;
    int finalOpacity_ = 178;
    bool hasTransparency_ = false;
    bool closing_ = false;
    bool intervalInSeconds_ = false;
};

#endif
