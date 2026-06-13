#include "SqliteStore.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace {

void bindText(sqlite3_stmt* stmt, int index, const std::string& value)
{
    if (sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        throw std::runtime_error("failed to bind SQLite text parameter");
    }
}

void bindDouble(sqlite3_stmt* stmt, int index, double value)
{
    if (sqlite3_bind_double(stmt, index, value) != SQLITE_OK) {
        throw std::runtime_error("failed to bind SQLite real parameter");
    }
}

void bindNull(sqlite3_stmt* stmt, int index)
{
    if (sqlite3_bind_null(stmt, index) != SQLITE_OK) {
        throw std::runtime_error("failed to bind SQLite null parameter");
    }
}

bool isDefaultScore(double value)
{
    return std::abs(value - DefaultObservationScore) < 0.000001;
}

void bindScore(sqlite3_stmt* stmt, int index, double value)
{
    if (isDefaultScore(value)) {
        bindNull(stmt, index);
    } else {
        bindDouble(stmt, index, value);
    }
}

void execSql(sqlite3* db, const char* sql)
{
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error != nullptr ? error : "SQLite statement failed";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

constexpr const char* CreateSchemaSql =
    "CREATE TABLE IF NOT EXISTS observations ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "created_at TEXT NOT NULL,"
    "energy REAL,"
    "mood REAL,"
    "grounding REAL,"
    "activity TEXT NOT NULL,"
    "quote TEXT"
    ")";

}

SqliteStore::SqliteStore(std::string path)
    : path_(std::move(path))
{
    ensureParentDirectory(path_);
    open();
    initializeSchema();
}

SqliteStore::~SqliteStore()
{
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

void SqliteStore::save(const Observation& observation)
{
    const char* sql =
        "INSERT INTO observations "
        "(created_at, energy, mood, grounding, activity, quote) "
        "VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }

    try {
        bindText(stmt, 1, observation.createdAt);
        bindScore(stmt, 2, observation.energy);
        bindScore(stmt, 3, observation.mood);
        bindScore(stmt, 4, observation.grounding);
        bindText(stmt, 5, observation.activity);
        bindText(stmt, 6, observation.quote);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            throw std::runtime_error(sqlite3_errmsg(db_));
        }
    } catch (...) {
        sqlite3_finalize(stmt);
        throw;
    }

    sqlite3_finalize(stmt);
}

const std::string& SqliteStore::path() const
{
    return path_;
}

void SqliteStore::open()
{
    if (sqlite3_open(path_.c_str(), &db_) != SQLITE_OK) {
        std::string message = db_ != nullptr ? sqlite3_errmsg(db_) : "failed to open SQLite database";
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error(message);
    }
}

void SqliteStore::initializeSchema()
{
    execSql(db_, CreateSchemaSql);
    if (schemaRequiresScores()) {
        migrateNullableScores();
    }
}

bool SqliteStore::schemaRequiresScores()
{
    const char* sql = "PRAGMA table_info(observations)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }

    bool requiresScores = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* rawName = sqlite3_column_text(stmt, 1);
        const int notNull = sqlite3_column_int(stmt, 3);
        if (rawName == nullptr) {
            continue;
        }

        const std::string name(reinterpret_cast<const char*>(rawName));
        if ((name == "energy" || name == "mood" || name == "grounding") && notNull != 0) {
            requiresScores = true;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return requiresScores;
}

void SqliteStore::migrateNullableScores()
{
    try {
        execSql(db_, "BEGIN IMMEDIATE");
        execSql(db_, "ALTER TABLE observations RENAME TO observations_old");
        execSql(db_, CreateSchemaSql);
        execSql(db_,
            "INSERT INTO observations (id, created_at, energy, mood, grounding, activity, quote) "
            "SELECT id, created_at, "
            "NULLIF(energy, 3.0), NULLIF(mood, 3.0), NULLIF(grounding, 3.0), "
            "activity, quote FROM observations_old");
        execSql(db_, "DROP TABLE observations_old");
        execSql(db_, "COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

std::string SqliteStore::defaultDatabasePath()
{
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        throw std::runtime_error("HOME is not set; cannot choose database path");
    }

    return (std::filesystem::path(home) / ".observer" / "observer.sqlite3").string();
}

void SqliteStore::ensureParentDirectory(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
}
