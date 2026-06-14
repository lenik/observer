#ifndef DATA_DIR_STORE_H
#define DATA_DIR_STORE_H

#include "ObservationStore.h"

#include <string>

class DataDirStore : public ObservationStore {
public:
    explicit DataDirStore(std::string path);

    void save(const Observation& observation) override;
    std::vector<Observation> loadAll() override;
    const std::string& path() const override;

    static bool shouldUseDataDir(const std::string& path);

private:
    std::string path_;
};

#endif
