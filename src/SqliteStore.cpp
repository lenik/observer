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

std::string columnText(sqlite3_stmt* stmt, int index)
{
    const unsigned char* value = sqlite3_column_text(stmt, index);
    return value != nullptr ? reinterpret_cast<const char*>(value) : "";
}

double columnScore(sqlite3_stmt* stmt, int index)
{
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return DefaultObservationScore;
    }
    return sqlite3_column_double(stmt, index);
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

bool tableHasColumn(sqlite3* db, const char* table, const char* column)
{
    std::string sql = std::string("PRAGMA table_info(") + table + ")";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* rawName = sqlite3_column_text(stmt, 1);
        if (rawName != nullptr && column == std::string(reinterpret_cast<const char*>(rawName))) {
            found = true;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return found;
}

constexpr const char* CreateSchemaSql =
    "CREATE TABLE IF NOT EXISTS observations ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "prompted_at TEXT NOT NULL,"
    "submitted_at TEXT NOT NULL,"
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
        "(prompted_at, submitted_at, energy, mood, grounding, activity, quote) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }

    try {
        bindText(stmt, 1, observation.promptedAt);
        bindText(stmt, 2, observation.submittedAt);
        bindScore(stmt, 3, observation.energy);
        bindScore(stmt, 4, observation.mood);
        bindScore(stmt, 5, observation.grounding);
        bindText(stmt, 6, observation.activity);
        bindText(stmt, 7, observation.quote);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            throw std::runtime_error(sqlite3_errmsg(db_));
        }
    } catch (...) {
        sqlite3_finalize(stmt);
        throw;
    }

    sqlite3_finalize(stmt);
}

std::vector<Observation> SqliteStore::loadAll()
{
    initializeSchema();
    const char* sql =
        "SELECT prompted_at, submitted_at, energy, mood, grounding, activity, quote "
        "FROM observations ORDER BY prompted_at ASC, id ASC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }

    std::vector<Observation> observations;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        observations.push_back(Observation{
            columnText(stmt, 0),
            columnText(stmt, 1),
            columnScore(stmt, 2),
            columnScore(stmt, 3),
            columnScore(stmt, 4),
            columnText(stmt, 5),
            columnText(stmt, 6),
        });
    }

    sqlite3_finalize(stmt);
    return observations;
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
    if (schemaRequiresMigration()) {
        migrateSchema();
    }
}

bool SqliteStore::schemaRequiresMigration()
{
    const char* sql = "PRAGMA table_info(observations)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db_));
    }

    bool hasPromptedAt = false;
    bool hasSubmittedAt = false;
    bool scoresRequireValues = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* rawName = sqlite3_column_text(stmt, 1);
        const int notNull = sqlite3_column_int(stmt, 3);
        if (rawName == nullptr) {
            continue;
        }

        const std::string name(reinterpret_cast<const char*>(rawName));
        if (name == "prompted_at") {
            hasPromptedAt = true;
        }
        if (name == "submitted_at") {
            hasSubmittedAt = true;
        }
        if ((name == "energy" || name == "mood" || name == "grounding") && notNull != 0) {
            scoresRequireValues = true;
        }
    }

    sqlite3_finalize(stmt);
    return !hasPromptedAt || !hasSubmittedAt || scoresRequireValues;
}

void SqliteStore::migrateSchema()
{
    try {
        execSql(db_, "BEGIN IMMEDIATE");
        execSql(db_, "ALTER TABLE observations RENAME TO observations_old");
        const bool oldHasCreatedAt = tableHasColumn(db_, "observations_old", "created_at");
        const bool oldHasPromptedAt = tableHasColumn(db_, "observations_old", "prompted_at");
        const bool oldHasSubmittedAt = tableHasColumn(db_, "observations_old", "submitted_at");
        const std::string promptedExpr = oldHasPromptedAt ? "prompted_at"
            : (oldHasCreatedAt ? "created_at" : "datetime('now')");
        const std::string submittedExpr = oldHasSubmittedAt ? "submitted_at"
            : (oldHasCreatedAt ? "created_at" : promptedExpr);
        execSql(db_, CreateSchemaSql);
        const std::string insertSql = std::string(
            "INSERT INTO observations (id, prompted_at, submitted_at, energy, mood, grounding, activity, quote) "
            "SELECT id, ") +
            promptedExpr + ", " +
            submittedExpr + ", "
            "NULLIF(energy, 3.0), NULLIF(mood, 3.0), NULLIF(grounding, 3.0), "
            "activity, quote FROM observations_old";
        execSql(db_, insertSql.c_str());
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
