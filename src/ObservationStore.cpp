#include "ObservationStore.h"

#include "AppConfig.h"
#include "DataDirStore.h"
#include "SqliteStore.h"

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
