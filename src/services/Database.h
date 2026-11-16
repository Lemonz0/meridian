#pragma once
// services/: minimal RAII wrapper over SQLite (UTF-16 APIs).
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace meridian::services {

class Statement {
public:
    Statement(sqlite3*, const char* sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int index, int64_t);
    Statement& bind(int index, const std::wstring&);
    Statement& bind(int index, const std::vector<uint8_t>&);
    bool step();          // true while a row is available
    void run();           // step to completion; throws on error
    void reset();

    int64_t integer(int column) const;
    std::wstring text(int column) const;
    std::vector<uint8_t> blob(int column) const;

private:
    sqlite3* m_db;
    sqlite3_stmt* m_stmt { nullptr };
};

class Database {
public:
    // Opens (creating if needed) with WAL journaling. Use ":memory:" for tests.
    explicit Database(const std::filesystem::path&);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void exec(const char* sql);
    Statement prepare(const char* sql) { return Statement(m_db, sql); }
    int userVersion();
    void setUserVersion(int);

    // RAII transaction: commits on commit(), rolls back otherwise.
    class Transaction {
    public:
        explicit Transaction(Database& db) : m_db(db) { m_db.exec("BEGIN IMMEDIATE"); }
        ~Transaction() { if (!m_done) try { m_db.exec("ROLLBACK"); } catch (...) {} }
        void commit() { m_db.exec("COMMIT"); m_done = true; }
    private:
        Database& m_db;
        bool m_done { false };
    };

private:
    sqlite3* m_db { nullptr };
};

} // namespace meridian::services
