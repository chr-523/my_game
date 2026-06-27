#include "main.hpp"

#ifdef _WIN64

// WinMain 是 Windows GUI 程序的入口函数。
// 如果这是普通控制台程序，入口通常叫 main；但 Windows GUI 子系统程序通常从 WinMain 开始。
//
// 函数名里的 Win 指 Windows，Main 表示入口。
//
// 这里虽然函数名叫 WinMain，但当前工具链是 64 位 MinGW UCRT，
// 所以编译出来的是 Windows x64 程序。
//
// 参数说明：
// - instance: 当前程序实例句柄。它不是 C++ 对象，而是 Windows 给当前程序实例的系统句柄。
// - 第二个 HINSTANCE: 旧版 Windows 保留参数。现代 Windows 总是传空值，当前程序不需要命名它。
// - LPSTR: long pointer to string，窄字符串命令行参数。当前游戏不用，所以不命名。
// - show_command: Windows 希望窗口以什么方式显示，例如正常显示、最小化等。
//
// Windows 文档里的 “long pointer” 是历史叫法。
// 在 64 位程序里，指针本身就是 64 位；LPSTR 本质上就是 char*。
//
// 入口函数只负责转交控制权，不写游戏逻辑。
int WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show_command) {
    return puzzle::run_win64_app(instance, show_command);
}

#endif
