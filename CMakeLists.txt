cmake_minimum_required(VERSION 3.16)

project(SudokuGame VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动处理 AUTOMOC（Qt 元对象编译器）
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 兼容 Qt5 与 Qt6，优先 Qt6
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(SudokuGame
    src/main.cpp
    src/sudokugame.h
    src/sudokugame.cpp
    src/sudokuwidget.h
    src/sudokuwidget.cpp
)

target_link_libraries(SudokuGame PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# Windows 下生成控制台/窗口应用（MinGW 用 WIN32 标记为窗口程序）
if(WIN32)
    set_target_properties(SudokuGame PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ============================================================================
# MinGW + 静态 Qt 的独立运行处理
# ============================================================================
# 1) 静态链接 GCC 运行时。注意：32 位 MinGW-w64(GCC 8.1) 使用 DW2 异常模型时，
#    即使写了 -static-libgcc，C++ 异常的展开器(__gcc_personality_v0)仍会来自
#    libgcc_s_dw2-1.dll，因此这 3 个运行时 DLL 中 libgcc_s_dw2-1.dll 往往甩不掉。
# 2) 因此在链接后，把编译器 bin 目录里存在的运行时 DLL 一并拷贝到 exe 旁边，
#    保证 exe 能单独运行（这也是 Qt+MinGW 静态编译的通用做法）。
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")

    # 编译器(g++/gcc)所在目录，通常与这些运行时 DLL 位于同一 bin 目录
    get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)

    # 逐个拷贝可能被动态依赖的 GCC 运行时库；存在才拷贝，避免破坏其它工具链
    foreach(_gcc_runtime_dll
            "libgcc_s_dw2-1.dll"
            "libgcc_s_seh-1.dll"
            "libstdc++-6.dll"
            "libwinpthread-1.dll")
        if(EXISTS "${MINGW_BIN}/${_gcc_runtime_dll}")
            add_custom_command(TARGET SudokuGame POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MINGW_BIN}/${_gcc_runtime_dll}"
                    "$<TARGET_FILE_DIR:SudokuGame>")
        endif()
    endforeach()
endif()
