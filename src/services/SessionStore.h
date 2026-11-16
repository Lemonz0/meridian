#pragma once
// services/: persists open windows and tabs so the next launch restores them.
#include "Database.h"

#include <cstdint>
#include <string>
#include <vector>

namespace meridian::services {

struct SessionTab {
    std::wstring url;
    std::wstring title;
    bool pinned { false };
    std::vector<uint8_t> state; // WebKit back/forward session data (may be empty)
};

struct SessionWindow {
    std::vector<SessionTab> tabs;
    int selected { 0 };
    int left { 0 }, top { 0 }, right { 0 }, bottom { 0 }; // restored (non-maximized) bounds
    bool maximized { false };
};

class SessionStore {
public:
    explicit SessionStore(Database&);
    void save(const std::vector<SessionWindow>&); // atomic replace
    std::vector<SessionWindow> load();
private:
    Database& m_db;
};

} // namespace meridian::services
