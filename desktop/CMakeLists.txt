cmake_minimum_required(VERSION 3.20)
project(orderbook_desktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# webview/webview: a native window that hosts the system web engine (WebView2 on Windows).
# Only its header-only core is used; skip everything else it can build.
set(WEBVIEW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_ENABLE_CHECKS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_ENABLE_PACKAGING OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_STATIC_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(webview
  URL https://github.com/webview/webview/archive/refs/tags/0.12.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# nlohmann/json: converts the structs in exchange_client.hpp to and from the UI's JSON.
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(webview json)

add_executable(orderbook-app
  host/main.cpp
  host/bridge.cpp
  host/demo_client.cpp
  host/app.rc
  # Your code. Add more files here as your client grows.
  client/my_client.cpp)

target_include_directories(orderbook-app PRIVATE include host)
target_link_libraries(orderbook-app PRIVATE webview::core nlohmann_json::nlohmann_json)
target_compile_definitions(orderbook-app PRIVATE OB_APP_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(orderbook-app PRIVATE /utf-8)
endif()

# When your client uses gRPC (built with vcpkg or from source):
#   find_package(gRPC CONFIG REQUIRED)
#   target_link_libraries(orderbook-app PRIVATE gRPC::grpc++)
# plus the generated exchange.pb.cc / exchange.grpc.pb.cc from exchange.proto.

# The console window is handy while you develop (std::cout from your client shows up there).
option(OB_HIDE_CONSOLE "Build without the console window" OFF)
if(OB_HIDE_CONSOLE AND MSVC)
  set_target_properties(orderbook-app PROPERTIES WIN32_EXECUTABLE ON)
  target_link_options(orderbook-app PRIVATE /ENTRY:mainCRTStartup)
endif()

# The UI is the static build of web/ (npm run build there). It's copied next to the exe as ui/.
set(OB_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../web/build" CACHE PATH "Built web UI")
if(NOT EXISTS "${OB_UI_DIR}/index.html")
  message(WARNING "No built UI at ${OB_UI_DIR}. Run `npm install && npm run build` in web/ first.")
endif()
add_custom_command(TARGET orderbook-app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${OB_UI_DIR}" "$<TARGET_FILE_DIR:orderbook-app>/ui"
  COMMENT "Copying the web UI next to orderbook-app")
