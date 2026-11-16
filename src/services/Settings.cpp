#include "Settings.h"

namespace meridian::services {

Settings::Settings(Database& db)
    : m_db(db)
{
    m_db.exec("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
}

std::wstring Settings::get(const std::wstring& key, const std::wstring& fallback)
{
    auto s = m_db.prepare("SELECT value FROM settings WHERE key = ?");
    s.bind(1, key);
    return s.step() ? s.text(0) : fallback;
}

bool Settings::getBool(const std::wstring& key, bool fallback)
{
    return get(key, fallback ? L"1" : L"0") == L"1";
}

void Settings::set(const std::wstring& key, const std::wstring& value)
{
    auto s = m_db.prepare("INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    s.bind(1, key).bind(2, value);
    s.run();
}

} // namespace meridian::services
