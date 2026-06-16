#include "ObservationStore.h"

#include "AppConfig.h"
#include "DataDirStore.h"
#include "SqliteStore.h"
#include "calendar.h"

#include <algorithm>
#include <stdexcept>

namespace {

wxDateTime criteriaAnchor(const ObservationCriteria& criteria)
{
    if (!criteria.anchorDate.empty()) {
        return toDateTime(criteria.anchorDate + "T12:00:00");
    }
    return wxDateTime::Today();
}

wxDateTime filterPeriodStart(wxDateTime value, ObservationFilter filter, bool weekStartsMonday)
{
    switch (filter) {
    case ObservationFilter::Day:
        return startOfDay(value);
    case ObservationFilter::Week:
        return startOfWeek(value, weekStartsMonday);
    case ObservationFilter::Month:
        return startOfMonth(value);
    case ObservationFilter::Year:
        return startOfYear(value);
    case ObservationFilter::All:
        return startOfDay(value);
    }
    return startOfDay(value);
}

wxDateTime filterPeriodEnd(const wxDateTime& start, ObservationFilter filter)
{
    wxDateTime end = start;
    switch (filter) {
    case ObservationFilter::Day:
        end.Add(wxDateSpan::Days(1));
        break;
    case ObservationFilter::Week:
        end.Add(wxDateSpan::Weeks(1));
        break;
    case ObservationFilter::Month:
        end.Add(wxDateSpan::Months(1));
        break;
    case ObservationFilter::Year:
        end.Add(wxDateSpan::Years(1));
        break;
    case ObservationFilter::All:
        end.Add(wxDateSpan::Days(1));
        break;
    }
    return end;
}

bool observationMatches(const Observation& observation, const ObservationCriteria& criteria)
{
    if (criteria.filter == ObservationFilter::All) {
        return true;
    }

    const wxDateTime when = toDateTime(observation.promptedAt);
    const wxDateTime anchor = criteriaAnchor(criteria);

    if (criteria.filter == ObservationFilter::Year) {
        return when.GetYear() == anchor.GetYear();
    }

    if (!criteria.eachYear) {
        switch (criteria.filter) {
        case ObservationFilter::Day:
            return when.GetMonth() == anchor.GetMonth() && when.GetDay() == anchor.GetDay();
        case ObservationFilter::Week:
            return isoWeek(when) == isoWeek(anchor);
        case ObservationFilter::Month:
            return when.GetMonth() == anchor.GetMonth();
        default:
            return false;
        }
    }

    const wxDateTime start = filterPeriodStart(anchor, criteria.filter, criteria.weekStartsMonday);
    const wxDateTime end = filterPeriodEnd(start, criteria.filter);
    return when.IsEqualTo(start) || (when.IsLaterThan(start) && when.IsEarlierThan(end));
}

std::vector<Observation>::iterator findRowByKey(std::vector<Observation>& rows, int64_t key)
{
    return std::find_if(rows.begin(), rows.end(),
                        [key](const Observation& row) { return row.id == key; });
}

} // namespace

void ObservationStore::ensureLoaded()
{
    if (m_loaded) {
        return;
    }
    readRows(m_rows);
    m_loaded = true;
}

void ObservationStore::rebuildView()
{
    m_view.clear();
    for (std::size_t index = 0; index < m_rows.size(); ++index) {
        if (observationMatches(m_rows[index], m_criteria)) {
            m_view.push_back(index);
        }
    }
}

void ObservationStore::load(const ObservationCriteria& criteria)
{
    ensureLoaded();
    m_criteria = criteria;
    rebuildView();
}

std::size_t ObservationStore::rowCount() const
{
    return m_view.size();
}

Observation ObservationStore::rowAt(std::size_t index) const
{
    if (index >= m_view.size()) {
        throw std::out_of_range("observation row index out of range");
    }
    return m_rows[m_view[index]];
}

int64_t ObservationStore::rowKeyAt(std::size_t index) const
{
    return rowAt(index).id;
}

std::size_t ObservationStore::allRowCount()
{
    ensureLoaded();
    return m_rows.size();
}

Observation ObservationStore::allRowAt(std::size_t index)
{
    ensureLoaded();
    if (index >= m_rows.size()) {
        throw std::out_of_range("observation row index out of range");
    }
    return m_rows[index];
}

void ObservationStore::save(const Observation& observation)
{
    ensureLoaded();
    Observation row = observation;
    appendRow(row);
    m_rows.push_back(row);
    rebuildView();
}

void ObservationStore::update(int64_t key, const Observation& observation)
{
    ensureLoaded();
    const auto iterator = findRowByKey(m_rows, key);
    if (iterator == m_rows.end()) {
        throw std::runtime_error("observation not found");
    }
    *iterator = observation;
    iterator->id = key;
    m_dirty = true;
    rebuildView();
}

void ObservationStore::remove(int64_t key)
{
    ensureLoaded();
    const auto iterator = findRowByKey(m_rows, key);
    if (iterator == m_rows.end()) {
        throw std::runtime_error("observation not found");
    }
    m_rows.erase(iterator);
    m_dirty = true;
    rebuildView();
}

void ObservationStore::commit()
{
    if (!m_dirty) {
        return;
    }
    writeRows(m_rows);
    m_dirty = false;
}

bool ObservationStore::dirty() const
{
    return m_dirty;
}

const ObservationCriteria& ObservationStore::criteria() const
{
    return m_criteria;
}

std::unique_ptr<ObservationStore> createObservationStore()
{
    std::string path = appConfig().storePath;
    if (path.empty()) {
        path = SqliteStore::defaultDatabasePath();
    }

    if (DataDirStore::shouldUseDataDir(path)) {
        return std::make_unique<DataDirStore>(path);
    }

    return std::make_unique<SqliteStore>(path);
}
