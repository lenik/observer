#ifndef DATA_DIR_STORE_H
#define DATA_DIR_STORE_H

#include "ObservationStore.h"

#include <string>
#include <vector>

class DataDirStore : public ObservationStore {
public:
    explicit DataDirStore(std::string path);

    const std::string& path() const override;

    static bool shouldUseDataDir(const std::string& path);

protected:
    void readRows(std::vector<Observation>& rows) override;
    void writeRows(const std::vector<Observation>& rows) override;
    void appendRow(Observation& observation) override;

private:
    std::string m_path;
};

#endif
