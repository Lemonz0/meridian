#pragma once
// services/: Reader settings and the parts of Reader that don't need the
// engine (option encoding, result parsing). The script itself is
// src/engine/reader.js; engine/ReaderScript.h builds calls to it.
#include "Settings.h"

#include <string>

namespace meridian::services {

enum class ReaderTheme { Automatic, Light, Sepia, Dark };

struct ReaderStyle {
    ReaderTheme theme { ReaderTheme::Automatic };
    bool serif { false };
    int sizePercent { 100 }; // 60..250

    static constexpr int kMinSize = 60, kMaxSize = 250, kStep = 10;
    ReaderStyle larger() const;
    ReaderStyle smaller() const;
    std::wstring toJson() const; // options object for reader.js
};

ReaderStyle loadReaderStyle(Settings&);
void saveReaderStyle(Settings&, const ReaderStyle&);

// What reader.js reports (fields separated by U+001F).
struct ReaderResult {
    bool available { false };
    bool shown { false };
    std::wstring title, excerpt, byline, siteName;
};
ReaderResult parseReaderResult(const std::wstring&);

// A JavaScript string literal (double-quoted) that is safe to splice into a script.
std::wstring jsStringLiteral(const std::wstring&);

} // namespace meridian::services
