#pragma once
// services/: named Tab Groups. A window shows either its own ungrouped tabs
// or one group; a group's tabs are kept here while no window shows it (and
// are refreshed from the window that shows it, so a crash loses little).
// Pinned tabs never belong to a group: they stay in the window.
#include "Database.h"
#include "SessionStore.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace meridian::services {

struct TabGroup {
    int64_t id { 0 };
    std::wstring name;
    int64_t created { 0 };
};

class TabGroups {
public:
    explicit TabGroups(Database&);

    std::vector<TabGroup> list();            // in sidebar order
    std::optional<TabGroup> get(int64_t id);
    int64_t create(const std::wstring& name, int64_t now); // empty or whitespace name -> "Untitled"
    bool rename(int64_t id, const std::wstring& name);
    bool remove(int64_t id);                 // tabs go with it
    bool move(int64_t id, int position);

    // Tabs stored for a group (pinned flags are ignored: groups hold no pinned tabs).
    std::vector<SessionTab> tabs(int64_t id, int* selected = nullptr);
    void saveTabs(int64_t id, const std::vector<SessionTab>&, int selected); // atomic replace
    size_t tabCount(int64_t id);

    // "Untitled", "Untitled 2", ... : first name not used by another group.
    std::wstring uniqueName(const std::wstring& base);

private:
    Database& m_db;
};

} // namespace meridian::services
