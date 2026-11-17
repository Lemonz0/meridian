#pragma once
// engine/: builds calls to the embedded Reader script (src/engine/reader.js).
#include "services/Reader.h"

#include <string>

namespace meridian::engine {

// mode: "check", "extract", "show", "hide" or "style". The result string is
// parsed with services::parseReaderResult.
std::wstring readerCall(const wchar_t* mode, const services::ReaderStyle&);

} // namespace meridian::engine
