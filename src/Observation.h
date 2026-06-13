#ifndef OBSERVATION_H
#define OBSERVATION_H

#include <string>

inline constexpr double DefaultObservationScore = 3.0;

struct Observation {
    std::string createdAt;
    double energy;
    double mood;
    double grounding;
    std::string activity;
    std::string quote;
};

#endif
