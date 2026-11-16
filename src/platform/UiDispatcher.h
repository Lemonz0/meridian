#pragma once
// platform/: run a function on the UI thread from any thread.
#include <functional>

namespace meridian::platform {

void initUiDispatcher();                       // call once, on the UI thread
void postToUi(std::function<void()>);          // safe from any thread

} // namespace meridian::platform
