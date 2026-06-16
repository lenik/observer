#ifndef OBSERVATION_STORE_H
#define OBSERVATION_STORE_H

#include "Observation.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class ObservationFilter {
    All,
    Day,
    Week,
    Month,
    Year,
};

struct ObservationCriteria {
    ObservationFilter filter = ObservationFilter::All;
    std::string anchorDate;
    bool eachYear = false;
    bool weekStartsMonday = true;
};

class ObservationStore {
public:
    virtual ~ObservationStore() = default;

    ObservationStore(const ObservationStore&) = delete;
    ObservationStore& operator=(const ObservationStore&) = delete;

    void load(const ObservationCriteria& criteria);
    std::size_t rowCount() const;
    Observation rowAt(std::size_t index) const;
    int64_t rowKeyAt(std::size_t index) const;

    std::size_t allRowCount();
    Observation allRowAt(std::size_t index);

    void save(const Observation& observation);
    void update(int64_t key, const Observation& observation);
    void remove(int64_t key);
    void commit();
    bool dirty() const;
    const ObservationCriteria& criteria() const;

    virtual const std::string& path() const = 0;

protected:
    ObservationStore() = default;

    virtual void readRows(std::vector<Observation>& rows) = 0;
    virtual void writeRows(const std::vector<Observation>& rows) = 0;
    virtual void appendRow(Observation& observation) = 0;

private:
    void ensureLoaded();
    void rebuildView();

    std::vector<Observation> m_rows;
    std::vector<std::size_t> m_view;
    ObservationCriteria m_criteria{};
    bool m_loaded = false;
    bool m_dirty = false;
};

std::unique_ptr<ObservationStore> createObservationStore();

#endif
