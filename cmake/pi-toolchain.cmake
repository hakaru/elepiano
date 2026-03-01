# Raspberry Pi 4 (aarch64) クロスコンパイル用ツールチェーン
# Mac から使う場合: brew install aarch64-unknown-linux-gnu
# または Pi 上で直接ビルド（ツールチェーン不要）

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# クロスコンパイラのプレフィックス（環境に合わせて変更）
set(CROSS_PREFIX "aarch64-unknown-linux-gnu")

find_program(CMAKE_C_COMPILER   ${CROSS_PREFIX}-gcc)
find_program(CMAKE_CXX_COMPILER ${CROSS_PREFIX}-g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
