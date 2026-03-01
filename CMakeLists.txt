cmake_minimum_required(VERSION 3.16)
project(elepiano CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# 依存ライブラリ
# ============================================================
find_package(ALSA REQUIRED)

# libFLAC
find_library(FLAC_LIB NAMES FLAC)
find_path(FLAC_INCLUDE NAMES FLAC/stream_decoder.h)
if(NOT FLAC_LIB OR NOT FLAC_INCLUDE)
    message(FATAL_ERROR "libFLAC が見つかりません。apt install libflac-dev")
endif()

# nlohmann/json (ヘッダーオンリー)
# システムにある場合: apt install nlohmann-json3-dev
# ない場合は FetchContent でダウンロード
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ============================================================
# ターゲット
# ============================================================
add_executable(elepiano
    src/main.cpp
    src/flac_decoder.cpp
    src/sample_db.cpp
    src/voice.cpp
    src/synth_engine.cpp
    src/organ_engine.cpp
    src/midi_input.cpp
    src/audio_output.cpp
)

target_include_directories(elepiano PRIVATE
    ${FLAC_INCLUDE}
    src
)

target_link_libraries(elepiano PRIVATE
    ALSA::ALSA
    ${FLAC_LIB}
    nlohmann_json::nlohmann_json
    pthread
)

# 最適化フラグ
target_compile_options(elepiano PRIVATE -O2 -Wall -Wextra)

# ============================================================
# Pi クロスコンパイル用ツールチェーン設定（オプション）
# cmake -DCMAKE_TOOLCHAIN_FILE=cmake/pi-toolchain.cmake ..
# ============================================================
