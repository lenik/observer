#ifndef SQLITE_STORE_H
#define SQLITE_STORE_H

#include "ObservationStore.h"

#include <sqlite3.h>

#include <filesystem>
#include <string>
#include <vector>

class SqliteStore : public ObservationStore {
public:
    explicit SqliteStore(std::string path);
    ~SqliteStore() override;

    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;

    const std::string& path() const override;

    static std::string defaultDatabasePath();

protected:
    void readRows(std::vector<Observation>& rows) override;
    void writeRows(const std::vector<Observation>& rows) override;
    void appendRow(Observation& observation) override;

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
