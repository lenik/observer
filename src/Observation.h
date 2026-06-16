#ifndef OBSERVATION_H
#define OBSERVATION_H

#include <cmath>
#include <string>

inline constexpr double DefaultObservationScore = 3.0;

struct Observation {
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

#endif
