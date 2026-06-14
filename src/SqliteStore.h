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
    std::vector<Observation> loadAll() override;
    const std::string& path() const override;

    static std::string defaultDatabasePath();

private:
    void open();
    void initializeSchema();
    bool schemaRequiresMigration();
    void migrateSchema();
    static void ensureParentDirectory(const std::filesystem::path& path);

    sqlite3* m_db = nullptr;
    std::string m_path;
};

#endif
