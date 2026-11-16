#pragma once
// services/: what the Smart Search dropdown shows for the text typed so far.
// Everything here is local (history + bookmarks); nothing touches the network.
#include "Bookmarks.h"
#include "History.h"

#include <string>
#include <vector>

namespace meridian::services {

enum class SuggestionKind {
    TopHit,           // best local match; selected by default while it is being autocompleted
    Navigate,         // the typed text read as an address
    Search,           // the typed text sent to the search engine
    History,
    Bookmark,
    SearchSuggestion, // from the search engine (opt-in; added by the UI, never by buildSuggestions)
};

struct Suggestion {
    SuggestionKind kind { SuggestionKind::History };
    std::wstring title;
    std::wstring url;
    int64_t historyId { 0 }; // non-zero when the row can be deleted from history
    bool bookmarked { false };
};

struct SuggestionList {
    std::vector<Suggestion> items;
    // Inline autocomplete: text to append after what the user typed ("web" ->
    // "kit.org"). Empty when nothing completes.
    std::wstring completion;
    int defaultSelection { -1 }; // row Enter acts on before any arrow key; -1 = the typed text
};

struct SuggestionOptions {
    std::wstring searchPrefix;
    size_t maxMatches { 6 };       // history/bookmark rows after the top rows
    bool allowCompletion { true }; // false while deleting, so Backspace is not fought
    int64_t now { unixNow() };
};

SuggestionList buildSuggestions(History&, Bookmarks&, const std::wstring& input, const SuggestionOptions&);

} // namespace meridian::services
