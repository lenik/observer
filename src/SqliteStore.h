#ifndef SQLITE_STORE_H
#define SQLITE_STORE_H

#include "ObservationStore.h"

#include <sqlite3.h>

#include <filesystem>
#include <string>

class SqliteStore : public ObservationStore {
public:
    explicit SqliteStore(std::string path);
    ~SqliteStore() override;

    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;

    void save(const Observation& observation) override;
    const std::string& path() const override;

    static std::string defaultDatabasePath();

private:
    void open();
    void initializeSchema();
    bool schemaRequiresScores();
    void migrateNullableScores();
    static void ensureParentDirectory(const std::filesystem::path& path);

    sqlite3* db_ = nullptr;
    std::string path_;
};

#endif
