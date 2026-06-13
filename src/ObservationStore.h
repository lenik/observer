#ifndef OBSERVATION_STORE_H
#define OBSERVATION_STORE_H

#include "Observation.h"

#include <memory>
#include <string>

class ObservationStore {
public:
    virtual ~ObservationStore() = default;

    ObservationStore(const ObservationStore&) = delete;
    ObservationStore& operator=(const ObservationStore&) = delete;

    virtual void save(const Observation& observation) = 0;
    virtual const std::string& path() const = 0;

protected:
    ObservationStore() = default;
};

std::unique_ptr<ObservationStore> createObservationStore();

#endif
