#include "Suggestions.h"

#include "Text.h"
#include "UrlFixup.h"

#include <algorithm>
#include <cwctype>
#include <map>

namespace meridian::services {

namespace {
constexpr double kBookmarkBonus = 150; // a little more than one visit this week (100)

struct Candidate {
    std::wstring url, title, stripped;
    int64_t historyId { 0 };
    int visitCount { 0 };
    bool bookmarked { false };
    double base { 0 };
    int quality { 1 }; // 3 = URL starts with the input, 2 = words start at word boundaries, 1 = substring
    double score { 0 };
};

std::wstring trim(const std::wstring& s)
{
    size_t b = 0, e = s.size();
    while (b < e && iswspace(s[b])) ++b;
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

bool startsAtWordBoundary(const std::wstring& text, const std::wstring& word)
{
    for (size_t pos = text.find(word); pos != std::wstring::npos; pos = text.find(word, pos + 1))
        if (pos == 0 || !iswalnum(text[pos - 1]))
            return true;
    return false;
}

std::wstring hostPart(const std::wstring& stripped)
{
    return stripped.substr(0, stripped.find_first_of(L"/?#"));
}

std::wstring withoutTrailingSlash(std::wstring s)
{
    if (!s.empty() && s.back() == L'/')
        s.pop_back();
    return s;
}

Suggestion toSuggestion(const Candidate& c, SuggestionKind kind)
{
    return { kind, c.title, c.url, c.historyId, c.bookmarked };
}
}

SuggestionList buildSuggestions(History& history, Bookmarks& bookmarks, const std::wstring& rawInput, const SuggestionOptions& options)
{
    SuggestionList list;
    std::wstring input = trim(rawInput);
    if (input.empty())
        return list;
    auto words = splitWords(input);
    // A trailing space means the word is finished: nothing to complete.
    bool singleWord = words.size() == 1 && !iswspace(rawInput.back());
    std::wstring typed = singleWord ? strippedURL(input) : std::wstring();
    // Match "https://wiki" and "www.wiki" against stored URLs the way the
    // user means them: without the scheme or www. they typed.
    for (auto& w : words)
        w = strippedURL(w);
    words.erase(std::remove_if(words.begin(), words.end(), [](auto& w) { return w.empty(); }), words.end());

    // Gather: history first, then bookmarks merged in by URL.
    std::vector<Candidate> candidates;
    std::map<std::wstring, size_t> byURL;
    std::vector<HistoryEntry> historyMatches;
    std::vector<Bookmark> bookmarkMatches;
    if (!words.empty()) { // no words would match everything
        historyMatches = history.find(words, History::Order::MostVisited, 200);
        bookmarkMatches = bookmarks.find(words, 50);
    }
    for (auto& e : historyMatches) {
        Candidate c;
        c.url = e.url;
        c.title = e.title;
        c.historyId = e.id;
        c.visitCount = e.visitCount;
        c.base = frecencyScore(e.visitCount, e.typedCount, options.now - e.lastVisit);
        byURL[c.url] = candidates.size();
        candidates.push_back(std::move(c));
    }
    for (auto& b : bookmarkMatches) {
        auto it = byURL.find(b.url);
        if (it == byURL.end()) {
            Candidate c;
            c.url = b.url;
            c.title = b.title;
            byURL[c.url] = candidates.size();
            candidates.push_back(std::move(c));
            it = byURL.find(b.url);
        }
        Candidate& c = candidates[it->second];
        if (!c.bookmarked)
            c.base += kBookmarkBonus;
        c.bookmarked = true;
        if (c.title.empty())
            c.title = b.title;
    }

    for (auto& c : candidates) {
        c.stripped = strippedURL(c.url);
        std::wstring host = hostPart(c.stripped), title = toLower(c.title);
        if (!typed.empty() && c.stripped.compare(0, typed.size(), typed) == 0)
            c.quality = 3;
        else if (std::all_of(words.begin(), words.end(), [&](auto& w) { return startsAtWordBoundary(host, w) || startsAtWordBoundary(title, w); }))
            c.quality = 2;
        c.score = c.base * (c.quality == 3 ? 4 : c.quality == 2 ? 2 : 1);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](auto& a, auto& b) { return a.score > b.score; });

    // Top Hit.
    const Candidate* topCandidate = nullptr; // row to leave out of the list below
    std::wstring topURL;
    bool typedHasPath = typed.find_first_of(L"/?#") != std::wstring::npos;
    if (!typed.empty() && !typedHasPath) {
        // Still inside the host name: complete to the host whose pages score best.
        std::map<std::wstring, double> hostScore;
        for (auto& c : candidates)
            if (c.quality == 3)
                hostScore[hostPart(c.stripped)] += c.score;
        const std::wstring* bestHost = nullptr;
        double best = -1;
        for (auto& [host, score] : hostScore)
            if (score > best) {
                best = score;
                bestHost = &host;
            }
        if (bestHost) {
            const Candidate* inHost = nullptr;
            for (auto& c : candidates) {
                if (c.quality != 3 || hostPart(c.stripped) != *bestHost)
                    continue;
                if (!inHost)
                    inHost = &c;
                if (withoutTrailingSlash(c.stripped) == *bestHost) {
                    topCandidate = &c; // the site's front page is itself in history
                    break;
                }
            }
            Suggestion top;
            if (topCandidate)
                top = toSuggestion(*topCandidate, SuggestionKind::TopHit);
            else {
                top.kind = SuggestionKind::TopHit;
                top.url = originOf(inHost->url) + L"/";
                top.title = *bestHost;
            }
            topURL = top.url;
            list.items.push_back(std::move(top));
            if (options.allowCompletion) {
                list.completion = bestHost->substr(typed.size());
                list.defaultSelection = 0;
            }
        }
    } else if (!typed.empty()) {
        for (auto& c : candidates)
            if (c.quality == 3) {
                topCandidate = &c;
                break;
            }
        if (topCandidate) {
            topURL = topCandidate->url;
            list.items.push_back(toSuggestion(*topCandidate, SuggestionKind::TopHit));
            if (options.allowCompletion) {
                list.completion = topCandidate->stripped.substr(typed.size());
                list.defaultSelection = 0;
            }
        }
    }
    if (list.items.empty() && !candidates.empty()) {
        // No address match: a well-used page whose title matches is still worth leading with.
        auto& c = candidates.front();
        if (c.quality == 2 && (c.bookmarked || c.visitCount >= 3)) {
            topCandidate = &c;
            topURL = c.url;
            list.items.push_back(toSuggestion(c, SuggestionKind::TopHit));
        }
    }

    // The typed text itself: an address to go to, or a search.
    auto fixed = fixupInput(input, options.searchPrefix);
    if (fixed.isSearch)
        list.items.push_back({ SuggestionKind::Search, input, fixed.url });
    else if (!fixed.url.empty() && withoutTrailingSlash(strippedURL(fixed.url)) != withoutTrailingSlash(strippedURL(topURL)))
        list.items.push_back({ SuggestionKind::Navigate, input, fixed.url });

    size_t matches = 0;
    for (auto& c : candidates) {
        if (matches >= options.maxMatches)
            break;
        if (&c == topCandidate || c.url == topURL)
            continue;
        list.items.push_back(toSuggestion(c, c.bookmarked ? SuggestionKind::Bookmark : SuggestionKind::History));
        ++matches;
    }
    return list;
}

} // namespace meridian::services
