#ifndef RENDER_DRIVER_H
#define RENDER_DRIVER_H

#include "Observation.h"

#include <optional>
#include <string>
#include <vector>

enum class ObserveResultKind {
    Submitted,
    Skipped,
    Snoozed,
    Empty
};

struct ObservePromptDefaults {
    double energy = DefaultObservationScore;
    double mood = DefaultObservationScore;
    double grounding = DefaultObservationScore;
    double intervalSeconds = 120.0;
    std::string theme = "dark";
    std::string quote;
    std::vector<std::string> quotes;
    std::size_t quoteIndex = 0;
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
