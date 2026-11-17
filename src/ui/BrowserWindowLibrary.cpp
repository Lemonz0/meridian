// ui/: BrowserWindow, M5 part: sidebar, Tab Groups, Reading List and Reader.
#include "BrowserWindow.h"

#include "WindowText.h"
#include "app/App.h"
#include "config/Brand.h"
#include "engine/ReaderScript.h"
#include "platform/Log.h"
#include "services/UrlFixup.h"

#include <algorithm>
#include <commctrl.h>
#include <functional>
#include <optional>

namespace meridian::ui {

namespace {
constexpr size_t kMaxSidebarRows = 3000;
constexpr int kMaxFolderDepth = 32;

enum LibraryMenuId : UINT {
    kLibOpenNewTab = 300, kLibRename, kLibDelete, kLibNewGroup, kLibNewGroupWithTabs, kLibNewFolder, kLibMarkRead,
    kLibMarkUnread, kLibClearRead, kLibToggleSidebar,
    kReaderAuto, kReaderLight, kReaderSepia, kReaderDark, kReaderSans, kReaderSerif, kReaderLarger, kReaderSmaller,
    kReaderActualSize, kReaderToggle,
    kGroupUngrouped = 900, kGroupFirst = 1000, // + index into the group list
};

std::wstring tabsLabel(int n)
{
    return std::to_wstring(n) + (n == 1 ? L" Tab" : L" Tabs");
}

template<typename F>
void guarded(const char* what, F&& f)
{
    try {
        f();
    } catch (const std::exception& e) {
        platform::log(L"%hs FAILED: %hs", what, e.what());
    }
}
}

// ---- sidebar ---------------------------------------------------------------------

void BrowserWindow::setSidebar(bool open)
{
    if (open == m_sidebarOpen)
        return;
    if (!open)
        endRename(true);
    m_sidebarOpen = open;
    m_sidebarHover = {};
    if (open)
        refreshSidebar();
    layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void BrowserWindow::refreshSidebar()
{
    using Kind = Sidebar::Kind;
    std::vector<Sidebar::Item> items;
    auto add = [&](Kind kind, int64_t id, std::wstring title) -> Sidebar::Item& {
        Sidebar::Item item;
        item.kind = kind;
        item.id = id;
        item.title = std::move(title);
        items.push_back(std::move(item));
        return items.back();
    };

    // Tab Groups: this window's own tabs first, then every named group.
    add(Kind::Header, 0, L"Tab Groups").addButton = m_app.tabGroups() != nullptr;
    add(Kind::Group, 0, tabsLabel(ungroupedCount())).current = m_groupId == 0;
    guarded("tab group list", [&] {
        if (auto* groups = m_app.tabGroups())
            for (auto& g : groups->list()) {
                size_t count;
                if (g.id == m_groupId)
                    count = size_t(std::count_if(m_tabs.begin(), m_tabs.end(), [](auto& t) { return !t->pinned; }));
                else if (auto* other = m_app.windowShowingGroup(g.id, this)) {
                    count = 0;
                    for (int i = 0; i < other->tabCount(); ++i)
                        count += !other->tab(i).pinned;
                } else
                    count = groups->tabCount(g.id);
                auto& item = add(Kind::Group, g.id, g.name);
                item.detail = std::to_wstring(count);
                item.current = g.id == m_groupId;
            }
    });

    add(Kind::Panes, 0, L"");
    if (m_sidebar.pane() == Sidebar::Pane::Bookmarks) {
        guarded("bookmark tree", [&] {
            auto* bookmarks = m_app.bookmarks();
            if (!bookmarks)
                return;
            size_t leaves = 0;
            std::function<void(int64_t, int)> walk = [&](int64_t folder, int depth) {
                if (depth > kMaxFolderDepth)
                    return;
                for (auto& b : bookmarks->children(folder)) {
                    if (items.size() >= kMaxSidebarRows)
                        return;
                    auto& item = add(b.folder ? Kind::Folder : Kind::Bookmark, b.id, b.title.empty() ? b.url : b.title);
                    item.depth = depth;
                    item.url = b.url;
                    item.host = services::displayHost(b.url);
                    if (b.folder) {
                        item.expanded = m_expandedFolders.count(b.id) != 0;
                        if (item.expanded)
                            walk(b.id, depth + 1);
                    } else
                        ++leaves;
                }
            };
            for (int64_t root : { services::Bookmarks::kFavoritesId, services::Bookmarks::kBookmarksId }) {
                auto r = bookmarks->get(root);
                auto& item = add(Kind::Folder, root, r ? r->title : L"");
                item.expanded = m_expandedFolders.count(root) != 0;
                if (item.expanded)
                    walk(root, 1);
            }
            if (!leaves && bookmarks->children(services::Bookmarks::kFavoritesId).empty()
                && bookmarks->children(services::Bookmarks::kBookmarksId).empty())
                add(Kind::Placeholder, 0, L"No bookmarks yet. Press Ctrl+D to add the current page to Favorites, "
                                          L"or right-click the start page to import bookmarks.");
        });
    } else {
        guarded("reading list", [&] {
            auto* list = m_app.readingList();
            if (!list)
                return;
            auto entries = list->list();
            for (auto& r : entries) {
                if (items.size() >= kMaxSidebarRows)
                    break;
                auto& item = add(Kind::Reading, r.id, r.title.empty() ? r.url : r.title);
                item.url = r.url;
                item.host = services::displayHost(r.url);
                item.detail = r.excerpt;
                item.unread = r.unread();
            }
            if (entries.empty())
                add(Kind::Placeholder, 0, L"Your Reading List is empty. Press Ctrl+Shift+D to save the current page for later.");
        });
    }
    m_sidebar.setItems(std::move(items));
    m_sidebarHover = {};
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void BrowserWindow::sidebarClick(const Sidebar::Hit& hit, bool ctrl)
{
    const Sidebar::Item* item = m_sidebar.item(hit.index);
    if (!item)
        return;
    Sidebar::Item it = *item; // the actions below rebuild the rows
    switch (it.kind) {
    case Sidebar::Kind::Header:
        if (hit.part == Sidebar::Part::Add) {
            if (int64_t id = newGroup(false))
                beginRename(Sidebar::Kind::Group, id);
        }
        break;
    case Sidebar::Kind::Group:
        switchToGroup(it.id);
        break;
    case Sidebar::Kind::Panes:
        m_sidebar.setPane(hit.part == Sidebar::Part::ReadingPane ? Sidebar::Pane::ReadingList : Sidebar::Pane::Bookmarks);
        refreshSidebar();
        break;
    case Sidebar::Kind::Folder:
        if (!m_expandedFolders.erase(it.id))
            m_expandedFolders.insert(it.id);
        refreshSidebar();
        break;
    case Sidebar::Kind::Bookmark:
        if (ctrl)
            openTab(it.url, false, m_active + 1);
        else
            navigate(it.url, false);
        break;
    case Sidebar::Kind::Reading:
        openReadingItem(it.id, ctrl);
        break;
    case Sidebar::Kind::Placeholder:
        break;
    }
}

void BrowserWindow::showSidebarMenu(const Sidebar::Hit& hit, POINT screen)
{
    const Sidebar::Item* found = m_sidebar.item(hit.index);
    Sidebar::Item it = found ? *found : Sidebar::Item {};
    bool isRoot = it.kind == Sidebar::Kind::Folder
        && (it.id == services::Bookmarks::kFavoritesId || it.id == services::Bookmarks::kBookmarksId);
    bool readingPane = m_sidebar.pane() == Sidebar::Pane::ReadingList;
    int ungrouped = ungroupedCount();

    HMENU menu = CreatePopupMenu();
    switch (found ? it.kind : Sidebar::Kind::Placeholder) {
    case Sidebar::Kind::Header:
    case Sidebar::Kind::Group:
        if (found && it.kind == Sidebar::Kind::Group && it.id) {
            AppendMenuW(menu, MF_STRING, kLibRename, L"Rename");
            AppendMenuW(menu, MF_STRING, kLibDelete, L"Delete Tab Group…");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
        AppendMenuW(menu, MF_STRING | (m_app.tabGroups() ? 0 : MF_GRAYED), kLibNewGroup, L"New Empty Tab Group");
        if (m_groupId == 0 && ungrouped > 0)
            AppendMenuW(menu, MF_STRING, kLibNewGroupWithTabs, (L"New Tab Group with " + tabsLabel(ungrouped)).c_str());
        break;
    case Sidebar::Kind::Folder:
        AppendMenuW(menu, MF_STRING, kLibNewFolder, L"New Folder");
        if (!isRoot) {
            AppendMenuW(menu, MF_STRING, kLibRename, L"Rename");
            AppendMenuW(menu, MF_STRING, kLibDelete, L"Delete Folder…");
        }
        break;
    case Sidebar::Kind::Bookmark:
        AppendMenuW(menu, MF_STRING, kLibOpenNewTab, L"Open in New Tab");
        AppendMenuW(menu, MF_STRING, kLibRename, L"Rename");
        AppendMenuW(menu, MF_STRING, kLibDelete, L"Delete");
        break;
    case Sidebar::Kind::Reading:
        AppendMenuW(menu, MF_STRING, kLibOpenNewTab, L"Open in New Tab");
        AppendMenuW(menu, MF_STRING, it.unread ? kLibMarkRead : kLibMarkUnread, it.unread ? L"Mark as Read" : L"Mark as Unread");
        AppendMenuW(menu, MF_STRING, kLibDelete, L"Delete");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kLibClearRead, L"Clear Read Items");
        break;
    default:
        if (readingPane)
            AppendMenuW(menu, MF_STRING | (m_app.readingList() ? 0 : MF_GRAYED), kLibClearRead, L"Clear Read Items");
        else
            AppendMenuW(menu, MF_STRING | (m_app.bookmarks() ? 0 : MF_GRAYED), kLibNewFolder, L"New Folder");
        break;
    }
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (!cmd)
        return;

    auto* bookmarks = m_app.bookmarks();
    auto* reading = m_app.readingList();
    guarded("sidebar action", [&] {
        switch (cmd) {
        case kLibOpenNewTab:
            openTab(it.url, false, m_active + 1);
            if (it.kind == Sidebar::Kind::Reading && reading) {
                reading->setRead(it.id, true, services::unixNow());
                m_app.libraryChanged();
            }
            break;
        case kLibRename:
            beginRename(it.kind, it.id);
            break;
        case kLibDelete:
            if (it.kind == Sidebar::Kind::Group)
                deleteGroup(it.id, true);
            else if (it.kind == Sidebar::Kind::Reading && reading)
                reading->remove(it.id);
            else if (bookmarks && (it.kind == Sidebar::Kind::Bookmark || it.kind == Sidebar::Kind::Folder)) {
                if (it.kind == Sidebar::Kind::Folder && !bookmarks->children(it.id).empty()
                    && MessageBoxW(m_hwnd, (L"Delete the folder “" + it.title + L"” and everything in it?").c_str(),
                           brand::kProductName, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
                    break;
                bookmarks->remove(it.id);
            }
            m_app.libraryChanged();
            break;
        case kLibNewGroup:
            if (int64_t id = newGroup(false))
                beginRename(Sidebar::Kind::Group, id);
            break;
        case kLibNewGroupWithTabs:
            if (int64_t id = newGroup(true))
                beginRename(Sidebar::Kind::Group, id);
            break;
        case kLibNewFolder: {
            if (!bookmarks)
                break;
            int64_t parent = it.kind == Sidebar::Kind::Folder ? it.id : services::Bookmarks::kBookmarksId;
            int64_t id = bookmarks->addFolder(parent, L"New Folder");
            m_expandedFolders.insert(parent);
            m_app.libraryChanged();
            if (id)
                beginRename(Sidebar::Kind::Folder, id);
            break;
        }
        case kLibMarkRead:
        case kLibMarkUnread:
            if (reading)
                reading->setRead(it.id, cmd == kLibMarkRead, services::unixNow());
            m_app.libraryChanged();
            break;
        case kLibClearRead:
            if (reading)
                reading->clearRead();
            m_app.libraryChanged();
            break;
        }
    });
}

// Inline rename: an EDIT placed over the row. Enter or leaving the field saves; Esc cancels.
void BrowserWindow::beginRename(Sidebar::Kind kind, int64_t id)
{
    if (!m_sidebarOpen)
        setSidebar(true);
    int index = -1;
    for (int i = 0; i < m_sidebar.count(); ++i)
        if (m_sidebar.item(i)->kind == kind && m_sidebar.item(i)->id == id) {
            index = i;
            break;
        }
    if (index < 0) {
        endRename(false);
        return;
    }
    bool starting = m_renameId != id || m_renameKind != kind || !IsWindowVisible(m_renameEdit);
    m_renameKind = kind;
    m_renameId = id;
    const Sidebar::Item& it = *m_sidebar.item(index);
    auto r = m_sidebar.rowRect(index);
    float left = kind == Sidebar::Kind::Group ? r.left + 30 : r.left + 8 + it.depth * 16 + 38;
    int height = px(tokens::kBodyFontSize * 1.7f);
    SetWindowPos(m_renameEdit, HWND_TOP, px(left), px((r.top + r.bottom) / 2) - height / 2, std::max(0, px(r.right - 4 - left)),
        height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (starting) {
        SetWindowTextW(m_renameEdit, it.title.c_str());
        SetFocus(m_renameEdit);
        SendMessageW(m_renameEdit, EM_SETSEL, 0, -1);
    }
}

void BrowserWindow::endRename(bool commit)
{
    if (m_renameId < 0)
        return;
    Sidebar::Kind kind = m_renameKind;
    int64_t id = m_renameId;
    m_renameId = -1; // first: hiding the field sends WM_KILLFOCUS, which ends up here again
    std::wstring text = trimmed(windowText(m_renameEdit));
    bool hadFocus = GetFocus() == m_renameEdit;
    ShowWindow(m_renameEdit, SW_HIDE);
    if (commit && !text.empty()) {
        guarded("rename", [&] {
            if (kind == Sidebar::Kind::Group && m_app.tabGroups())
                m_app.tabGroups()->rename(id, text);
            else if (auto* bookmarks = m_app.bookmarks())
                if (auto b = bookmarks->get(id))
                    bookmarks->update(id, text, b->url);
        });
    }
    m_app.libraryChanged();
    if (hadFocus)
        focusContent();
}

LRESULT CALLBACK BrowserWindow::renameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
{
    auto* self = reinterpret_cast<BrowserWindow*>(ref);
    switch (msg) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DefSubclassProc(h, msg, wp, lp);
    case WM_KEYDOWN:
        if (wp == VK_RETURN || wp == VK_ESCAPE) {
            self->endRename(wp == VK_RETURN);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == VK_RETURN || wp == VK_ESCAPE)
            return 0;
        break;
    case WM_KILLFOCUS:
        self->endRename(true);
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ---- Tab Groups -----------------------------------------------------------------

std::unique_ptr<Tab> BrowserWindow::makeTab(const services::SessionTab& st)
{
    auto tab = std::make_unique<Tab>();
    tab->id = m_app.nextTabId();
    tab->url = st.url;
    tab->title = st.title;
    tab->pinned = st.pinned;
    tab->pendingState = st.state;
    tab->startPage = st.url.empty() || st.url == L"about:blank";
    return tab;
}

int BrowserWindow::ungroupedCount() const
{
    if (m_groupId)
        return int(m_stash.size());
    return int(std::count_if(m_tabs.begin(), m_tabs.end(), [](auto& t) { return !t->pinned; }));
}

void BrowserWindow::persistGroup()
{
    auto* groups = m_app.tabGroups();
    if (!m_groupId || !groups)
        return;
    std::vector<services::SessionTab> tabs;
    int selected = 0;
    for (int i = 0; i < int(m_tabs.size()); ++i) {
        auto& t = *m_tabs[i];
        if (t.pinned)
            continue;
        if (i == m_active)
            selected = int(tabs.size());
        tabs.push_back({ t.url, t.title, false, t.view ? t.view->sessionState() : t.pendingState });
    }
    guarded("tab group save", [&] { groups->saveTabs(m_groupId, tabs, selected); });
}

void BrowserWindow::switchToGroup(int64_t target)
{
    if (target == m_groupId)
        return;
    auto* groups = m_app.tabGroups();
    bool exists = false;
    guarded("tab group lookup", [&] { exists = target == 0 || (groups && groups->get(target)); });
    if (!exists)
        return;
    // A group is shown by one window at a time: go to the window that has it.
    if (auto* other = m_app.windowShowingGroup(target, this)) {
        if (IsIconic(other->hwnd()))
            ShowWindow(other->hwnd(), SW_RESTORE);
        SetForegroundWindow(other->hwnd());
        return;
    }
    endEditingAddress(true);
    endRename(true);
    if (m_overviewOpen)
        setOverview(false);
    if (m_historyOpen)
        setHistory(false);

    // 1. Pinned tabs stay; the rest leave with the current group. Hide the
    //    shown page first: if it is a pinned tab, nothing else would.
    persistGroup();
    if (Tab* shown = active(); shown && shown->view) {
        captureThumbnail(*shown, false);
        shown->view->setVisible(false);
    }
    std::vector<std::unique_ptr<Tab>> pinned, leaving;
    int leavingSelected = 0;
    for (int i = 0; i < int(m_tabs.size()); ++i) {
        if (m_tabs[i]->pinned) {
            pinned.push_back(std::move(m_tabs[i]));
            continue;
        }
        if (i == m_active)
            leavingSelected = int(leaving.size());
        leaving.push_back(std::move(m_tabs[i]));
    }
    m_tabs.clear();
    m_active = -1;
    m_drag = {};
    m_overviewFocus = -1;
    for (auto& t : leaving) {
        // Keep the back/forward list, drop the web view (it reloads when shown again).
        if (t->view) {
            t->pendingState = t->view->sessionState();
            t->view.reset();
        }
        t->loading = false;
        t->progress = 0;
        t->readerAvailable = t->readerActive = false;
    }
    if (m_groupId == 0) {
        m_stash = std::move(leaving);
        m_stashSelected = leavingSelected;
    }
    leaving.clear(); // a named group's tabs were saved by persistGroup above

    // 2. The incoming group's tabs (lazy: nothing loads until a tab is shown).
    std::vector<std::unique_ptr<Tab>> incoming;
    int selected = 0;
    if (target == 0) {
        incoming = std::move(m_stash);
        m_stash.clear();
        selected = m_stashSelected;
    } else
        guarded("tab group load", [&] {
            for (auto& st : groups->tabs(target, &selected))
                incoming.push_back(makeTab(st));
        });
    if (incoming.empty()) {
        services::SessionTab blank { L"about:blank", L"", false, {} };
        incoming.push_back(makeTab(blank));
        selected = 0;
    }
    m_groupId = target;
    m_tabs = std::move(pinned);
    int base = int(m_tabs.size());
    for (auto& t : incoming)
        m_tabs.push_back(std::move(t));
    activate(base + std::clamp(selected, 0, int(m_tabs.size()) - base - 1));
    updateGroupLabel();
    tabsChanged();
    m_app.libraryChanged(); // counts in other windows' sidebars
}

int64_t BrowserWindow::newGroup(bool withCurrentTabs, const std::wstring& name)
{
    auto* groups = m_app.tabGroups();
    if (!groups)
        return 0;
    int64_t id = 0;
    guarded("tab group create", [&] { id = groups->create(groups->uniqueName(name.empty() ? L"Untitled" : name), services::unixNow()); });
    if (!id)
        return 0;
    if (withCurrentTabs && m_groupId == 0) {
        // The window's tabs become the group; its ungrouped set is now empty.
        m_groupId = id;
        persistGroup();
        updateGroupLabel();
        tabsChanged();
        m_app.libraryChanged();
    } else
        switchToGroup(id);
    return id;
}

void BrowserWindow::deleteGroup(int64_t id, bool confirm)
{
    auto* groups = m_app.tabGroups();
    if (!groups || !id)
        return;
    std::optional<services::TabGroup> group;
    guarded("tab group lookup", [&] { group = groups->get(id); });
    if (!group)
        return;
    if (confirm) {
        std::wstring text = L"Delete the tab group “" + group->name + L"”? Its tabs will be closed.";
        if (MessageBoxW(m_hwnd, text.c_str(), brand::kProductName, MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
            return;
    }
    if (m_groupId == id)
        switchToGroup(0);
    else if (auto* other = m_app.windowShowingGroup(id, this))
        other->switchToGroup(0);
    guarded("tab group delete", [&] { groups->remove(id); });
    m_app.libraryChanged();
}

void BrowserWindow::showGroupMenu(POINT screen)
{
    std::vector<services::TabGroup> list;
    guarded("tab group list", [&] {
        if (m_app.tabGroups())
            list = m_app.tabGroups()->list();
    });
    int ungrouped = ungroupedCount();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (m_groupId == 0 ? MF_CHECKED : 0), kGroupUngrouped, tabsLabel(ungrouped).c_str());
    for (size_t i = 0; i < list.size() && i < 500; ++i)
        AppendMenuW(menu, MF_STRING | (list[i].id == m_groupId ? MF_CHECKED : 0), kGroupFirst + UINT(i), list[i].name.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (m_app.tabGroups() ? 0 : MF_GRAYED), kLibNewGroup, L"New Empty Tab Group");
    if (m_groupId == 0 && ungrouped > 0)
        AppendMenuW(menu, MF_STRING, kLibNewGroupWithTabs, (L"New Tab Group with " + tabsLabel(ungrouped)).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kLibToggleSidebar, m_sidebarOpen ? L"Hide Sidebar\tCtrl+Shift+L" : L"Show Sidebar\tCtrl+Shift+L");
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == kGroupUngrouped)
        switchToGroup(0);
    else if (cmd >= kGroupFirst && cmd < kGroupFirst + list.size())
        switchToGroup(list[cmd - kGroupFirst].id);
    else if (cmd == kLibNewGroup || cmd == kLibNewGroupWithTabs) {
        if (int64_t id = newGroup(cmd == kLibNewGroupWithTabs))
            beginRename(Sidebar::Kind::Group, id);
    } else if (cmd == kLibToggleSidebar)
        setSidebar(!m_sidebarOpen);
}

void BrowserWindow::updateGroupLabel()
{
    m_groupLabel.clear();
    m_groupLabelWidth = 0;
    if (m_groupId && m_app.tabGroups())
        guarded("tab group lookup", [&] {
            if (auto g = m_app.tabGroups()->get(m_groupId))
                m_groupLabel = g->name;
        });
    if (!m_groupLabel.empty() && m_dwrite && m_cardTitleFormat) {
        ComPtr<IDWriteTextLayout> layout;
        m_dwrite->CreateTextLayout(m_groupLabel.c_str(), UINT32(m_groupLabel.size()), m_cardTitleFormat.Get(), 1000, 30,
            layout.GetAddressOf());
        DWRITE_TEXT_METRICS tm {};
        if (layout && SUCCEEDED(layout->GetMetrics(&tm)))
            m_groupLabelWidth = std::min(tokens::kGroupLabelMaxWidth, tm.widthIncludingTrailingWhitespace + 2);
    }
    layout();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void BrowserWindow::libraryChanged()
{
    updateGroupLabel(); // the shown group may have been renamed
    if (m_sidebarOpen)
        refreshSidebar();
    if (showingStartPage())
        refreshStartPage();
}

// ---- Reading List ---------------------------------------------------------------

void BrowserWindow::addToReadingList()
{
    Tab* t = active();
    auto* list = m_app.readingList();
    if (!t || t->startPage || m_historyOpen || m_overviewOpen)
        return;
    if (!list || !services::isRecordableURL(t->url)) {
        showBanner(*t, L"This page can’t be added to the Reading List.");
        return;
    }
    uint64_t id = t->id;
    std::wstring url = t->url, title = t->displayTitle();
    HWND hwnd = m_hwnd;
    auto save = [this, hwnd, id, url, title](const services::ReaderResult& r) {
        if (!IsWindow(hwnd))
            return;
        Tab* tab = findTab(id);
        bool ok = false;
        guarded("reading list add", [&] {
            ok = m_app.readingList()->add(url, r.title.empty() ? title : r.title, r.excerpt, services::unixNow()) != 0;
        });
        if (tab)
            showBanner(*tab, ok ? L"Added “" + title + L"” to the Reading List." : L"Couldn’t save to the Reading List.");
        m_app.libraryChanged();
    };
    if (!t->view) {
        save({});
        return;
    }
    // The excerpt comes from the same article detection Reader uses.
    t->view->evaluateScript(engine::readerCall(L"extract", m_app.readerStyle()),
        [save](const std::wstring& raw) { save(services::parseReaderResult(raw)); });
}

void BrowserWindow::openReadingItem(int64_t id, bool newTab)
{
    auto* list = m_app.readingList();
    if (!list)
        return;
    std::optional<services::ReadingItem> item;
    guarded("reading list open", [&] {
        item = list->get(id);
        if (item)
            list->setRead(id, true, services::unixNow());
    });
    if (!item)
        return;
    if (newTab)
        openTab(item->url, false, m_active + 1);
    else
        navigate(item->url, false);
    m_app.libraryChanged();
}

// ---- Reader -------------------------------------------------------------------------

void BrowserWindow::checkReader(uint64_t tabId)
{
    Tab* t = findTab(tabId);
    if (!t || !t->view)
        return;
    HWND hwnd = m_hwnd;
    std::wstring url = t->url;
    t->view->evaluateScript(engine::readerCall(L"check", m_app.readerStyle()), [this, hwnd, tabId, url](const std::wstring& raw) {
        if (!IsWindow(hwnd))
            return;
        Tab* tab = findTab(tabId);
        if (!tab || tab->url != url)
            return; // answer for a page that's gone
        auto r = services::parseReaderResult(raw);
        tab->readerAvailable = r.available || tab->readerActive;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    });
}

void BrowserWindow::setReader(bool on)
{
    Tab* t = active();
    if (!t || !t->view || on == t->readerActive || (on && !t->readerAvailable))
        return;
    uint64_t id = t->id;
    HWND hwnd = m_hwnd;
    t->view->evaluateScript(engine::readerCall(on ? L"show" : L"hide", m_app.readerStyle()),
        [this, hwnd, id, on](const std::wstring& raw) {
            if (!IsWindow(hwnd))
                return;
            Tab* tab = findTab(id);
            if (!tab)
                return;
            auto r = services::parseReaderResult(raw);
            tab->readerActive = on && r.shown;
            if (on && !r.shown)
                tab->readerAvailable = false; // the article went away (page changed since the check)
            if (tab->readerActive && tab->view && active() == tab)
                SetFocus(tab->view->hwnd()); // so arrow keys and Space scroll the article
            InvalidateRect(m_hwnd, nullptr, FALSE);
        });
}

void BrowserWindow::applyReaderStyle(const services::ReaderStyle& style)
{
    m_app.setReaderStyle(style);
    for (auto& t : m_tabs)
        if (t->readerActive && t->view)
            t->view->evaluateScript(engine::readerCall(L"style", style), [](const std::wstring&) {});
}

void BrowserWindow::showReaderMenu(POINT screen)
{
    Tab* t = active();
    auto style = m_app.readerStyle();
    using services::ReaderTheme;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kReaderToggle, t && t->readerActive ? L"Hide Reader\tF9" : L"Show Reader\tF9");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (style.theme == ReaderTheme::Automatic ? MF_CHECKED : 0), kReaderAuto, L"Match System Theme");
    AppendMenuW(menu, MF_STRING | (style.theme == ReaderTheme::Light ? MF_CHECKED : 0), kReaderLight, L"Light");
    AppendMenuW(menu, MF_STRING | (style.theme == ReaderTheme::Sepia ? MF_CHECKED : 0), kReaderSepia, L"Sepia");
    AppendMenuW(menu, MF_STRING | (style.theme == ReaderTheme::Dark ? MF_CHECKED : 0), kReaderDark, L"Dark");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (!style.serif ? MF_CHECKED : 0), kReaderSans, L"Sans-Serif");
    AppendMenuW(menu, MF_STRING | (style.serif ? MF_CHECKED : 0), kReaderSerif, L"Serif");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kReaderLarger, L"Larger Text\tCtrl+=");
    AppendMenuW(menu, MF_STRING, kReaderSmaller, L"Smaller Text\tCtrl+-");
    AppendMenuW(menu, MF_STRING, kReaderActualSize, L"Actual Size\tCtrl+0");
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
    case kReaderToggle: setReader(!(t && t->readerActive)); return;
    case kReaderAuto: style.theme = ReaderTheme::Automatic; break;
    case kReaderLight: style.theme = ReaderTheme::Light; break;
    case kReaderSepia: style.theme = ReaderTheme::Sepia; break;
    case kReaderDark: style.theme = ReaderTheme::Dark; break;
    case kReaderSans: style.serif = false; break;
    case kReaderSerif: style.serif = true; break;
    case kReaderLarger: style = style.larger(); break;
    case kReaderSmaller: style = style.smaller(); break;
    case kReaderActualSize: style.sizePercent = 100; break;
    default: return;
    }
    applyReaderStyle(style);
}

} // namespace meridian::ui
