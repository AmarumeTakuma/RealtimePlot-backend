cmake_minimum_required (VERSION 3.16.3)

# Apple M1/Intel Mac対応
if (APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

project(TelemetryBackend)

enable_language(CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# コンパイラごとの警告レベル設定
if (MSVC)
    set(CMAKE_CXX_FLAGS "/W4 /EHsc")
else()
    set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unknown-pragmas -pthread")
endif()

# 実行ファイルの出力先をbinフォルダに指定
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

# --- 外部ライブラリの自動取得 ---
include(FetchContent)

set(CMAKE_POLICY_DEFAULT CMP0135 NEW)

# 1. nlohmann/json ライブラリの取得
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
FetchContent_GetProperties(json)
if(NOT json_POPULATED)
    FetchContent_Populate(json)
endif()

# 2. Standalone Asio (UDP通信用) の取得
FetchContent_Declare(asio URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-28-0.tar.gz)
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
endif()
# ----------------------------------------------------

# includeパスの設定
include_directories("src/")

# ソースファイルの指定
set(SOURCE_FILES
    "src/main.cpp"
)

# 実行ファイルの作成
add_executable(TelemetryBackend ${SOURCE_FILES})

# ライブラリの場所を直接指定してリンクする
target_include_directories(TelemetryBackend PRIVATE ${json_SOURCE_DIR}/single_include)
target_include_directories(TelemetryBackend PRIVATE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(TelemetryBackend PRIVATE ASIO_STANDALONE)