#ifndef RENDER_DRIVER_H
#define RENDER_DRIVER_H

#include "Observation.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class ObservationStore;

enum class ObserveResultKind {
    Submitted,
    Skipped,
    Snoozed,
    Quit
};

struct ObservePromptDefaults {
    double energy = DefaultObservationScore;
    double mood = DefaultObservationScore;
    double grounding = DefaultObservationScore;
    double intervalSeconds = 120.0;
    int opacityPercent = 75;
    bool weekStartsMonday = true;
    std::string theme = "dark";
    std::string quote;
    std::vector<std::string> quotes;
    std::size_t quoteIndex = 0;
    ObservationStore* store = nullptr;
    std::optional<Observation> editing;
};

struct ObserveResult {
    ObserveResultKind kind;
    std::optional<Observation> observation;
    double intervalSeconds = 120.0;
};

class RenderDriver {
public:
    virtual ~RenderDriver() = default;
    virtual ObserveResult prompt(const ObservePromptDefaults& defaults) = 0;
};

#endif
