# CMakeLists.txt
# CMake 是这个 C++ 项目的构建入口。
# VS Code 的调试配置目前直接调用 g++，但保留 CMakeLists.txt 可以方便以后用
# CMake Tools、命令行 cmake，或者迁移到更正式的构建流程。

# 指定项目需要的最低 CMake 版本。
cmake_minimum_required(VERSION 3.16)

# 定义项目名，并声明这是一个 C++ 项目。
project(my_game LANGUAGES CXX)

# 使用 C++17。
set(CMAKE_CXX_STANDARD 17)

# 如果编译器不支持 C++17，就直接报错，而不是默默降级。
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 禁用编译器私有扩展，尽量使用标准 C++。
set(CMAKE_CXX_EXTENSIONS OFF)

# 当前实现使用 Windows 桌面 API，并明确要求 64 位 Windows。
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    # CMake 里的 WIN32 表示生成 Windows GUI 子系统程序。
    # 它不是“32 位”的意思；真正的位数由编译器工具链决定。
    # 源文件分布：
    # - main.cc: Windows 程序入口
    # - src/puzzle_game.cc: 游戏核心状态逻辑
    # - src/win64_app.cc: 窗口、消息循环、GDI 绘制
    add_executable(my_game WIN32
        main.cc
        src/puzzle_game.cc
        src/win64_app.cc
    )

    # 让源文件可以直接 #include "puzzle_game.hpp" 这类头文件。
    target_include_directories(my_game PRIVATE
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include
    )

    # 链接 Windows 绘图和窗口相关系统库。
    # - gdi32: GDI 绘图函数，例如 CreatePen、BitBlt、Rectangle。
    # - gdiplus: GDI+ 图像库，用来读取 picture/xuk.png 这种 PNG 文件。
    # - user32: 窗口、消息循环、输入事件，例如 CreateWindowEx、GetMessage。
    target_link_libraries(my_game PRIVATE
        gdi32
        gdiplus
        user32
    )
else()
    # 非 Windows x64 平台目前不支持，因为渲染层直接使用 Windows/GDI。
    message(FATAL_ERROR "This version currently builds on 64-bit Windows only.")
endif()
