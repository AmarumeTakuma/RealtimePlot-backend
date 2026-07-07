cmake_minimum_required (VERSION 3.16.3)

# 【追加】古いCMake設定を読み込んでもエラーで止めないようにする設定
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

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

# コンパイラごとの警告・文字コード設定
if (MSVC)
    set(CMAKE_CXX_FLAGS "/W4 /EHsc /utf-8")
else()
    set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unknown-pragmas -pthread")
endif()

# 実行ファイルの出力先をbinフォルダに指定
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

# --- 外部ライブラリの自動取得 (FetchContent) ---
include(FetchContent)

# jsonライブラリ側で不要なテストをビルドしない設定
set(JSON_BuildTests OFF CACHE INTERNAL "")

# 1. nlohmann/json ライブラリの取得
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP true
)

# 2. Standalone Asio (UDP通信用) の取得
FetchContent_Declare(
    asio
    URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-28-0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP true
)

FetchContent_MakeAvailable(json asio)
# ----------------------------------------------------

# ソースファイルの指定
set(SOURCE_FILES
    "src/app/AppSetting.cpp"
    "src/app/CommandListener.cpp"
    "src/telemetry/Logger.cpp"
    "src/telemetry/HardwareReceiver.cpp"
    "src/simulator/CsvSimulator.cpp"
    "src/main.cpp"
)

# 実行ファイルの作成
add_executable(TelemetryBackend ${SOURCE_FILES})

target_include_directories(TelemetryBackend PRIVATE "src/")
target_include_directories(TelemetryBackend PRIVATE ${json_SOURCE_DIR}/single_include/nlohmann) 
target_include_directories(TelemetryBackend PRIVATE ${json_SOURCE_DIR}/single_include)
target_include_directories(TelemetryBackend PRIVATE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(TelemetryBackend PRIVATE ASIO_STANDALONE)