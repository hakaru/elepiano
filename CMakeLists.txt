cmake_minimum_required(VERSION 3.16)
project(elepiano C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(ELEPIANO_ENABLE_LV2 "Enable LV2 host in FxChain (requires lilv-0)" OFF)

# ============================================================
# 依存ライブラリ
# ============================================================
find_package(ALSA REQUIRED)

# FLAC デコード: dr_flac (ヘッダーオンリー, src/dr_flac.h)

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
# setBfree (Hammond B3 tonewheel organ) — 静的ライブラリ
# ============================================================
set(SETBFREE_DIR ${CMAKE_SOURCE_DIR}/vendor/setBfree)
add_library(setbfree STATIC
    ${SETBFREE_DIR}/src/tonegen.c
    ${SETBFREE_DIR}/src/vibrato.c
    ${SETBFREE_DIR}/src/midi.c
    ${SETBFREE_DIR}/src/state.c
    ${SETBFREE_DIR}/src/cfgParser.c
    ${SETBFREE_DIR}/src/pgmParser.c
    ${SETBFREE_DIR}/src/program.c
    ${SETBFREE_DIR}/src/midnam.c
    src/setbfree_stubs.c
    ${SETBFREE_DIR}/b_overdrive/overdrive.c
    ${SETBFREE_DIR}/b_whirl/whirl.c
    ${SETBFREE_DIR}/b_whirl/eqcomp.c
    ${SETBFREE_DIR}/b_reverb/reverb.c
)
target_include_directories(setbfree PUBLIC
    ${SETBFREE_DIR}/src
    ${SETBFREE_DIR}/b_overdrive
    ${SETBFREE_DIR}/b_whirl
    ${SETBFREE_DIR}/b_reverb
)
target_compile_options(setbfree PRIVATE -O3 -ffast-math -fomit-frame-pointer -w)
target_compile_definitions(setbfree PRIVATE _GNU_SOURCE)
set_target_properties(setbfree PROPERTIES C_STANDARD 99)

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
    src/setbfree_engine.cpp
    src/fx_chain.cpp
    src/ir_convolver.cpp
    src/status_reporter.cpp
    src/midi_input.cpp
    src/audio_output.cpp
)

if(ELEPIANO_ENABLE_LV2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LILV REQUIRED IMPORTED_TARGET lilv-0)
    target_sources(elepiano PRIVATE
        src/lv2_host.cpp
    )
    target_compile_definitions(elepiano PRIVATE ELEPIANO_ENABLE_LV2=1)
    target_link_libraries(elepiano PRIVATE
        PkgConfig::LILV
    )
endif()

target_include_directories(elepiano PRIVATE
    src
)

target_link_libraries(elepiano PRIVATE
    ALSA::ALSA
    nlohmann_json::nlohmann_json
    setbfree
    pthread
    m
)

# 最適化フラグ
target_compile_options(elepiano PRIVATE -O2 -Wall -Wextra)

# ============================================================
# Pi クロスコンパイル用ツールチェーン設定（オプション）
# cmake -DCMAKE_TOOLCHAIN_FILE=cmake/pi-toolchain.cmake ..
# ============================================================
