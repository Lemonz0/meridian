# Copies the WebKit runtime needed by Meridian from SRC (WebKitBuild/Release/bin) to DST.
file(GLOB dlls "${SRC}/*.dll")
list(FILTER dlls EXCLUDE REGEX "(TestRunner|MiniBrowser|ImageDiff|TestWebKit).*\\.dll$")
set(processes
    "${SRC}/WebKitWebProcess.exe"
    "${SRC}/WebKitNetworkProcess.exe"
    "${SRC}/WebKitGPUProcess.exe")
file(COPY ${dlls} ${processes} DESTINATION "${DST}")
file(COPY "${SRC}/WebKit.resources" DESTINATION "${DST}")
