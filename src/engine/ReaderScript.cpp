#include "ReaderScript.h"

#include "services/Text.h"

#include <ReaderScriptData.h> // generated from reader.js at build time (cmake/EmbedText.cmake)

namespace meridian::engine {

std::wstring readerCall(const wchar_t* mode, const services::ReaderStyle& style)
{
    static const std::wstring script = services::fromUtf8(reinterpret_cast<const char*>(kReaderScript));
    return L"(" + script + L")(" + services::jsStringLiteral(mode) + L"," + style.toJson() + L")";
}

} // namespace meridian::engine
