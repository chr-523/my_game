#pragma once

// win64_app.hpp
// 这个头文件对应 src/win64_app.cc。
//
// 注释规则：
// - 函数/类“做什么”的功能描述放在这里。
// - 函数内部“为什么这样实现”的细节注释放在 src/win64_app.cc 的函数体里。
// - 构造函数、析构函数、直接 return 的小函数在这里直接实现。

#ifdef _WIN64

// WIN32_LEAN_AND_MEAN / NOMINMAX 必须在 windows.h 之前定义。
// 它们放在这个 Windows 专用头里，比放在 main.hpp 更贴近真正需要 windows.h 的地方。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// MinGW 的 GDI+ 头文件会用到 PROPID。
// PROPID 定义在 wtypes.h 中，所以在 gdiplus.h 前显式包含。
#include <wtypes.h>

#include <gdiplus.h>

#include <string>
#include <vector>

#include "puzzle_game.hpp"

namespace puzzle {

// Windows 窗口类名，用于 RegisterClassExW 和 CreateWindowExW 匹配窗口类型。
extern const wchar_t kWindowClassName[];

// 窗口标题栏显示的文本。
extern const wchar_t kWindowTitle[];

// 拼图使用的 PNG 图片资源路径。
extern const wchar_t kPuzzleImageRelativePath[];

// 把 red/green/blue 三个 0-255 数值打包成 Windows GDI 使用的 COLORREF。
inline COLORREF rgb(int red, int green, int blue) {
    return RGB(red, green, blue);
}

// 把项目自己的 Rect 转成 Windows 的 RECT。
inline RECT to_win_rect(Rect rect) {
    return RECT{
        rect.x,
        rect.y,
        rect.x + rect.width,
        rect.y + rect.height,
    };
}

// RAII 画刷包装类：创建 HBRUSH，并在析构时释放。
class ScopedBrush {
public:
    // 创建一支纯色画刷。
    explicit ScopedBrush(COLORREF color)
        : handle_(CreateSolidBrush(color)) {}

    // 禁止复制，避免两个对象释放同一个 GDI 句柄。
    ScopedBrush(const ScopedBrush&) = delete;
    ScopedBrush& operator=(const ScopedBrush&) = delete;

    // 释放画刷句柄。
    ~ScopedBrush() {
        if (handle_ != nullptr) {
            DeleteObject(handle_);
        }
    }

    // 返回原始 HBRUSH，供 GDI 绘图函数使用。
    HBRUSH get() const {
        return handle_;
    }

private:
    HBRUSH handle_ = nullptr;
};

// RAII 画笔包装类：创建 HPEN，并在析构时释放。
class ScopedPen {
public:
    // 创建一支指定线型、线宽、颜色的画笔。
    ScopedPen(int style, int width, COLORREF color)
        : handle_(CreatePen(style, width, color)) {}

    // 禁止复制，避免两个对象释放同一个 GDI 句柄。
    ScopedPen(const ScopedPen&) = delete;
    ScopedPen& operator=(const ScopedPen&) = delete;

    // 释放画笔句柄。
    ~ScopedPen() {
        if (handle_ != nullptr) {
            DeleteObject(handle_);
        }
    }

    // 返回原始 HPEN，供 GDI 绘图函数使用。
    HPEN get() const {
        return handle_;
    }

private:
    HPEN handle_ = nullptr;
};

// 用纯色填充一个矩形。
void fill_rect(HDC dc, Rect rect, COLORREF color);

// 用纯色填充一个多边形。
void draw_filled_polygon(HDC dc, std::vector<POINT> points, COLORREF color);

// 用纯色填充一个圆。
void draw_filled_circle(HDC dc, int center_x, int center_y, int radius, COLORREF color);

// 画一条指定宽度和颜色的直线。
void draw_line(HDC dc, int x1, int y1, int x2, int y2, int width, COLORREF color);

// 返回当前 exe 所在目录。
std::wstring executable_directory();

// 返回可能存在拼图 PNG 的路径列表。
std::vector<std::wstring> puzzle_image_path_candidates();

// GDI+ 会话包装类：启动 GDI+，并在析构时关闭。
class GdiPlusSession {
public:
    // 启动 GDI+ 会话。
    GdiPlusSession() {
        Gdiplus::GdiplusStartupInput startup_input;
        status_ = Gdiplus::GdiplusStartup(&token_, &startup_input, nullptr);
    }

    // 禁止复制，避免重复关闭同一个 GDI+ token。
    GdiPlusSession(const GdiPlusSession&) = delete;
    GdiPlusSession& operator=(const GdiPlusSession&) = delete;

    // 关闭 GDI+ 会话。
    ~GdiPlusSession() {
        if (token_ != 0) {
            Gdiplus::GdiplusShutdown(token_);
        }
    }

    // 判断 GDI+ 是否启动成功。
    bool ok() const {
        return status_ == Gdiplus::Ok;
    }

private:
    ULONG_PTR token_ = 0;
    Gdiplus::Status status_ = Gdiplus::GenericError;
};

// 保存完整拼图源图的内存位图。
// 拼图块不存像素，渲染时从这张完整源图中裁剪对应区域。
class SourceImage {
public:
    // 创建一个尚未加载图片资源的 SourceImage。
    SourceImage() = default;

    // 释放内存 DC 和位图资源。
    ~SourceImage() {
        reset();
    }

    // 禁止复制，避免重复释放同一批 GDI 资源。
    SourceImage(const SourceImage&) = delete;
    SourceImage& operator=(const SourceImage&) = delete;

    // 初始化完整源图位图，并尝试从 picture/xuk.png 加载图片。
    bool initialize(HWND window);

    // 返回保存完整源图的内存 DC。
    HDC dc() const {
        return dc_;
    }

private:
    // 释放 SourceImage 持有的 GDI 资源。
    void reset();

    // 尝试把 PNG 图片绘制到完整源图位图中。
    bool draw_source_picture_from_png();

    // PNG 加载失败时绘制一张内置示例图。
    void draw_generated_source_picture();

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_bitmap_ = nullptr;
};

// 保存一个窗口运行所需的全部状态。
struct AppState {
    // 释放字体句柄。
    ~AppState() {
        if (title_font != nullptr) {
            DeleteObject(title_font);
        }

        if (message_font != nullptr) {
            DeleteObject(message_font);
        }
    }

    PuzzleGame game;
    GdiPlusSession gdiplus;
    SourceImage source_image;
    HFONT title_font = nullptr;
    HFONT message_font = nullptr;
};

// 创建游戏使用的 GDI 字体。
HFONT create_font(int height, int weight);

// 在指定矩形内居中绘制文本。
void draw_text_centered(HDC dc, Rect rect, const std::wstring& text, HFONT font, COLORREF color);

// 把一块拼图从完整源图中裁剪出来，并绘制到目标矩形。
void draw_tile(HDC dc, const SourceImage& source_image, const Tile& tile, Rect destination);

// 绘制一整帧游戏画面。
void draw_frame(HDC dc, AppState& state);

// 从窗口的用户数据槽取回 AppState。
AppState* state_from_window(HWND window);

// Windows 窗口过程，负责接收并分发窗口消息。
LRESULT window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

// Windows x64 应用主体。
//
// HINSTANCE 是 instance handle，程序实例句柄。
// 可以把它理解成“Windows 分配给当前 exe 运行实例的 ID/句柄”。
// 注册窗口类、创建窗口时都需要把它传给系统。
//
// show_command 来自 WinMain，表示系统希望窗口用什么方式显示。
// 常见值包括正常显示、最大化、最小化等。
int run_win64_app(HINSTANCE instance, int show_command);

}  // namespace puzzle

#endif
