#ifndef OBSERVATION_H
#define OBSERVATION_H

#include <cmath>
#include <cstdint>
#include <string>

inline constexpr double DefaultObservationScore = 2.5;

struct Observation {
    int64_t id = 0;
    std::string promptedAt;
    std::string submittedAt;
    double energy;
    double mood;
    double grounding;
    std::string activity;
    std::string quote;
};

inline bool isDefaultObservationScore(double value)
{
    return std::abs(value - DefaultObservationScore) < 0.000001;
}

inline bool observationEmgMissing(const Observation& observation)
{
    return isDefaultObservationScore(observation.energy)
        && isDefaultObservationScore(observation.mood)
        && isDefaultObservationScore(observation.grounding);
}

inline bool sameObservationIdentity(const Observation& a, const Observation& b)
{
    if (a.id != 0 && b.id != 0) {
        return a.id == b.id;
    }
    return a.promptedAt == b.promptedAt && a.submittedAt == b.submittedAt && a.activity == b.activity;
}

#endif
