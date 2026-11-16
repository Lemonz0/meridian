#pragma once
// services/: search-engine suggestions for the Smart Search field.
//
// This sends what the user types to the search engine, so it only runs when
// the user has turned it on (settings::kSearchSuggestions, off by default)
// and never in private windows. The UI decides; this class just fetches.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace meridian::services {

// Accepts the OpenSearch form ["query",["a","b"]] and the object form
// [{"phrase":"a"},{"phrase":"b"}]. Anything else gives an empty list.
std::vector<std::wstring> parseSearchSuggestions(const std::string& json, size_t limit);

class SearchSuggestions {
public:
    using Callback = std::function<void(const std::wstring& query, std::vector<std::wstring>)>;
    // Async; the callback runs on the UI thread. Only the newest request
    // reports back: answers to older queries are dropped.
    void fetch(const std::wstring& urlPrefix, const std::wstring& query, Callback);
    void cancel() { ++*m_generation; }
private:
    std::shared_ptr<uint64_t> m_generation { std::make_shared<uint64_t>(0) }; // UI thread only
};

} // namespace meridian::services
