#include "Database.h"

#include <sqlite3.h>
#include <stdexcept>

namespace meridian::services {

static void check(sqlite3* db, int rc, const char* what)
{
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
        throw std::runtime_error(std::string(what) + ": " + (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc)));
}

Statement::Statement(sqlite3* db, const char* sql)
    : m_db(db)
{
    check(m_db, sqlite3_prepare_v2(m_db, sql, -1, &m_stmt, nullptr), "prepare");
}

Statement::~Statement() { sqlite3_finalize(m_stmt); }

Statement& Statement::bind(int i, int64_t v)
{
    check(m_db, sqlite3_bind_int64(m_stmt, i, v), "bind");
    return *this;
}

Statement& Statement::bind(int i, const std::wstring& v)
{
    check(m_db, sqlite3_bind_text16(m_stmt, i, v.c_str(), static_cast<int>(v.size() * sizeof(wchar_t)), SQLITE_TRANSIENT), "bind");
    return *this;
}

Statement& Statement::bind(int i, const std::vector<uint8_t>& v)
{
    check(m_db, sqlite3_bind_blob(m_stmt, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT), "bind");
    return *this;
}

bool Statement::step()
{
    int rc = sqlite3_step(m_stmt);
    check(m_db, rc, "step");
    return rc == SQLITE_ROW;
}

void Statement::run()
{
    while (step()) { }
}

void Statement::reset()
{
    sqlite3_reset(m_stmt);
    sqlite3_clear_bindings(m_stmt);
}

int64_t Statement::integer(int c) const { return sqlite3_column_int64(m_stmt, c); }

std::wstring Statement::text(int c) const
{
    auto* p = static_cast<const wchar_t*>(sqlite3_column_text16(m_stmt, c));
    int bytes = sqlite3_column_bytes16(m_stmt, c);
    return p ? std::wstring(p, bytes / sizeof(wchar_t)) : std::wstring();
}

std::vector<uint8_t> Statement::blob(int c) const
{
    auto* p = static_cast<const uint8_t*>(sqlite3_column_blob(m_stmt, c));
    int n = sqlite3_column_bytes(m_stmt, c);
    return p ? std::vector<uint8_t>(p, p + n) : std::vector<uint8_t>();
}

Database::Database(const std::filesystem::path& path)
{
    int rc = sqlite3_open16(path.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        std::string msg = m_db ? sqlite3_errmsg(m_db) : "open failed";
        sqlite3_close(m_db);
        m_db = nullptr;
        throw std::runtime_error("open: " + msg);
    }
    sqlite3_busy_timeout(m_db, 2000);
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
    exec("PRAGMA foreign_keys=ON");
}

Database::~Database() { sqlite3_close(m_db); }

void Database::exec(const char* sql)
{
    char* err = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : sqlite3_errstr(rc);
        sqlite3_free(err);
        throw std::runtime_error(std::string("exec: ") + msg);
    }
}

int Database::userVersion()
{
    auto s = prepare("PRAGMA user_version");
    return s.step() ? static_cast<int>(s.integer(0)) : 0;
}

void Database::setUserVersion(int v)
{
    exec(("PRAGMA user_version=" + std::to_string(v)).c_str());
}

} // namespace meridian::services
