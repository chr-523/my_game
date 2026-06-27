#include "win64_app.hpp"

// 这份文件是 Windows x64 渲染层。
// 它依赖 windows.h 里的窗口、消息循环、GDI 绘图接口，所以只在 _WIN64 下编译。
#ifdef _WIN64

// windowsx.h 提供 GET_X_LPARAM / GET_Y_LPARAM。
// Windows 鼠标消息会把 x/y 坐标塞进一个 LPARAM 里，这两个宏负责把坐标拆出来。
#include <windowsx.h>
#include <algorithm>

namespace puzzle {

// wchar_t 是宽字符类型，Windows 的 W 版本 API 使用 UTF-16 宽字符串。
// 前缀 L 表示这是一个宽字符串字面量，例如 L"abc" 的类型接近 const wchar_t*。
const wchar_t kWindowClassName[] = L"MyGamePuzzleWindow";

const wchar_t kWindowTitle[] = L"2x2 Puzzle";

// 这里仅定义相关路径，不直接加载图片。
// 加载图片的逻辑在 SourceImage::draw_source_picture_from_png()
// 引号内的 // 是 转义 + "/"
const wchar_t kPuzzleImageRelativePath[] = L"picture\\csk.png";

void fill_rect(HDC dc, Rect rect, COLORREF color) {
    ScopedBrush brush(color);
    RECT win_rect = to_win_rect(rect);
    FillRect(dc, &win_rect, brush.get()); // 用 brush 填满 win_rect。
}

void draw_filled_polygon(HDC dc, std::vector<POINT> points, COLORREF color) {
    ScopedBrush brush(color);
    ScopedPen pen(PS_NULL, 0, color); // PS_NULL 表示不画轮廓，只填充内部。

    // GDI 的绘图对象需要先 SelectObject 到 DC 里才会生效。
    // SelectObject 返回之前的对象；画完后必须选回去，避免 DC 持有即将释放的对象。
    //
    // HGDIOBJ 是“任意 GDI 对象”的通用句柄类型。
    // 画刷、画笔、字体、位图都可以临时用 HGDIOBJ 保存旧对象。
    const HGDIOBJ old_brush = SelectObject(dc, brush.get());
    const HGDIOBJ old_pen = SelectObject(dc, pen.get());

    // POINT 是 Windows 自己的点结构，字段名是 x/y。
    Polygon(dc, points.data(), static_cast<int>(points.size()));

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
}

void draw_filled_circle(HDC dc, int center_x, int center_y, int radius, COLORREF color) {
    ScopedBrush brush(color);
    ScopedPen pen(PS_NULL, 0, color);
    const HGDIOBJ old_brush = SelectObject(dc, brush.get());
    const HGDIOBJ old_pen = SelectObject(dc, pen.get());

    // GDI 没有 Circle 函数。Ellipse 给一个正方形边界时，画出来就是圆。
    Ellipse(dc, center_x - radius, center_y - radius, center_x + radius, center_y + radius);

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
}

void draw_line(HDC dc, int x1, int y1, int x2, int y2, int width, COLORREF color) {
    ScopedPen pen(PS_SOLID, width, color);
    const HGDIOBJ old_pen = SelectObject(dc, pen.get());

    MoveToEx(dc, x1, y1, nullptr); // 设置当前画笔起点。最后一个参数可接收旧位置，这里不需要所以传 nullptr。
    LineTo(dc, x2, y2);            // 从当前点画到终点，并把当前点更新成终点。

    SelectObject(dc, old_pen);
}

std::wstring executable_directory() {
    wchar_t path[MAX_PATH]{};

    // GetModuleFileNameW(nullptr, ...) 会返回当前 exe 的完整路径。
    // 例如：D:\my_game\build\my_game.exe。
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }

    std::wstring full_path(path, length);
    const std::wstring::size_type slash = full_path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L"";
    }

    return full_path.substr(0, slash);
}

std::vector<std::wstring> puzzle_image_path_candidates() {
    std::vector<std::wstring> paths;

    // 相对当前工作目录。VS Code 调试时 cwd 通常是项目根目录，所以这个最常用。
    paths.push_back(kPuzzleImageRelativePath);

    const std::wstring exe_dir = executable_directory();
    if (!exe_dir.empty()) {
        // 如果 exe 就在项目根目录，这个路径会命中。
        paths.push_back(exe_dir + L"\\" + kPuzzleImageRelativePath);

        // 如果 exe 在 build/ 里，这个路径会回到项目根目录再找 picture/。
        paths.push_back(exe_dir + L"\\..\\" + kPuzzleImageRelativePath);
    }

    return paths;
}

bool SourceImage::initialize(HWND window) {
    reset();

    // HWND 是窗口句柄。GetDC(window) 获取这个窗口的绘图上下文。
    // 这里拿 window_dc 只是为了创建与窗口兼容的内存 DC 和位图。
    HDC window_dc = GetDC(window);
    if (window_dc == nullptr) {
        return false;
    }

    // CreateCompatibleDC 创建一个内存 DC。它不直接显示到屏幕。
    // 后面我们会把完整图片画到这个内存 DC 中，作为源图缓存。
    dc_ = CreateCompatibleDC(window_dc);

    // CreateCompatibleBitmap 创建一张与窗口像素格式兼容的位图。
    // 宽高就是完整拼图图片的大小。
    bitmap_ = CreateCompatibleBitmap(
        window_dc,
        PuzzleGame::kBoardSize,
        PuzzleGame::kBoardSize
    );

    // GetDC 得到的是窗口 DC 的临时使用权，必须用 ReleaseDC 还给系统。
    // 注意不是 DeleteDC：DeleteDC 只用于自己 CreateCompatibleDC 创建出来的内存 DC。
    ReleaseDC(window, window_dc);

    if (dc_ == nullptr || bitmap_ == nullptr) {
        reset();
        return false;
    }

    // 一个 DC 需要“选中”某张 bitmap，后续绘制才会真正写入那张 bitmap。
    // old_bitmap_ 保存原先选中的对象，析构时要选回去。
    old_bitmap_ = SelectObject(dc_, bitmap_);

    if (!draw_source_picture_from_png()) {
        // 如果图片路径不对或 GDI+ 读取失败，保留原来的代码绘制示例图作为兜底。
        // 正常情况下，项目根目录的 picture\xuk.png 会被加载。
        draw_generated_source_picture();
    }

    return true;
}

void SourceImage::reset() {
    if (dc_ != nullptr && old_bitmap_ != nullptr) {
        // 删除位图前，必须先把内存 DC 恢复到原来的对象。
        // 否则 DC 仍然选中 bitmap_，DeleteObject(bitmap_) 可能失败或留下资源状态问题。
        SelectObject(dc_, old_bitmap_);
        old_bitmap_ = nullptr;
    }

    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }

    if (dc_ != nullptr) {
        DeleteDC(dc_);
        dc_ = nullptr;
    }
}

bool SourceImage::draw_source_picture_from_png() {
    for (const std::wstring& image_path : puzzle_image_path_candidates()) {
        Gdiplus::Bitmap image(image_path.c_str());
        if (image.GetLastStatus() != Gdiplus::Ok) {
            continue;
        }

        const int image_width = static_cast<int>(image.GetWidth());
        const int image_height = static_cast<int>(image.GetHeight());
        if (image_width <= 0 || image_height <= 0) {
            continue;
        }

        // 图片不是正方形时，取中间最大的正方形区域。
        // xuk.png 当前是 522x422，所以会裁掉左右两边各 50 像素左右。
        const int crop_size = std::min(image_width, image_height);
        const int crop_x = (image_width - crop_size) / 2;
        const int crop_y = (image_height - crop_size) / 2;

        // 先把目标位图填成深色。
        // 如果 PNG 带透明区域，透明处也不会留下未初始化像素。
        fill_rect(dc_, {0, 0, PuzzleGame::kBoardSize, PuzzleGame::kBoardSize}, rgb(20, 24, 30));

        // Gdiplus::Graphics 可以包住已有 HDC，把 PNG 绘制到我们的内存位图里。
        Gdiplus::Graphics graphics(dc_);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        const Gdiplus::Rect destination(
            0,
            0,
            PuzzleGame::kBoardSize,
            PuzzleGame::kBoardSize
        );

        const Gdiplus::Status draw_status = graphics.DrawImage(
            &image,
            destination,
            crop_x,
            crop_y,
            crop_size,
            crop_size,
            Gdiplus::UnitPixel
        );

        if (draw_status == Gdiplus::Ok) {
            return true;
        }
    }

    return false;
}

void SourceImage::draw_generated_source_picture() {
    for (int y = 0; y < PuzzleGame::kBoardSize; ++y) {
        const double blend = static_cast<double>(y) / PuzzleGame::kBoardSize;
        const int red = static_cast<int>(92 + 70 * blend);
        const int green = static_cast<int>(178 + 20 * blend);
        const int blue = static_cast<int>(230 - 80 * blend);
        draw_line(dc_, 0, y, PuzzleGame::kBoardSize, y, 1, rgb(red, green, blue));
    }

    draw_filled_circle(dc_, 118, 104, 62, rgb(255, 226, 82));
    draw_filled_circle(dc_, 118, 104, 38, rgb(255, 244, 148));

    draw_filled_polygon(dc_, {{0, 312}, {150, 165}, {270, 312}}, rgb(95, 110, 112));
    draw_filled_polygon(dc_, {{210, 322}, {383, 126}, {548, 322}}, rgb(70, 88, 96));
    draw_filled_polygon(dc_, {{118, 196}, {150, 165}, {176, 198}, {146, 188}}, rgb(240, 244, 232));
    draw_filled_polygon(dc_, {{342, 172}, {383, 126}, {427, 178}, {386, 163}}, rgb(236, 241, 230));

    fill_rect(dc_, {0, 300, PuzzleGame::kBoardSize, 220}, rgb(70, 139, 84));
    draw_filled_polygon(
        dc_,
        {{0, 332}, {150, 286}, {316, 335}, {520, 282}, {520, 520}, {0, 520}},
        rgb(49, 94, 64)
    );

    draw_filled_polygon(
        dc_,
        {{180, 520}, {230, 395}, {285, 330}, {346, 396}, {406, 520}},
        rgb(68, 150, 194)
    );
    draw_filled_polygon(
        dc_,
        {{222, 520}, {258, 406}, {286, 360}, {320, 410}, {356, 520}},
        rgb(117, 197, 224)
    );

    fill_rect(dc_, {415, 265, 34, 165}, rgb(102, 71, 48));
    draw_filled_circle(dc_, 431, 236, 74, rgb(35, 113, 57));
    draw_filled_circle(dc_, 386, 265, 48, rgb(45, 137, 68));
    draw_filled_circle(dc_, 474, 268, 52, rgb(54, 154, 74));

    draw_line(dc_, 0, 260, PuzzleGame::kBoardSize, 260, 4, rgb(255, 255, 255));
    draw_line(dc_, 260, 0, 260, PuzzleGame::kBoardSize, 4, rgb(255, 255, 255));
}

HFONT create_font(int height, int weight) {
    // HFONT 是字体句柄。
    // CreateFontW 参数很多，因为它来自传统 GDI；当前游戏只关心高度、粗细和字体名。
    //
    // 函数名末尾的 W 表示 wide，也就是使用宽字符串版本。
    // 与之对应的 A 版本使用 char 窄字符串；现代 Windows 程序一般优先用 W。
    return CreateFontW(
        height,                 // 字体高度，单位是逻辑像素。
        0,                      // 字体宽度。0 表示让系统按高度自动选择合适宽度。
        0,                      // 文本倾斜角度。0 表示不旋转。
        0,                      // 字符基线角度。0 表示正常水平文本。
        weight,                 // 字重，例如 FW_NORMAL / FW_BOLD。
        FALSE,                  // 是否斜体。
        FALSE,                  // 是否下划线。
        FALSE,                  // 是否删除线。
        DEFAULT_CHARSET,        // 字符集。默认即可。
        OUT_DEFAULT_PRECIS,     // 输出精度。默认即可。
        CLIP_DEFAULT_PRECIS,    // 裁剪精度。默认即可。
        CLEARTYPE_QUALITY,      // 字体抗锯齿质量。
        DEFAULT_PITCH | FF_SWISS, // 字体族提示。FF_SWISS 通常对应无衬线字体。
        L"Segoe UI"             // 字体名。Windows 默认 UI 字体。
    );
}

void draw_text_centered(HDC dc, Rect rect, const std::wstring& text, HFONT font, COLORREF color) {
    RECT win_rect = to_win_rect(rect);

    HGDIOBJ old_font = nullptr;
    if (font != nullptr) {
        old_font = SelectObject(dc, font); // 选中字体，并保存旧字体。
    }

    SetTextColor(dc, color);       // 设置文字颜色。
    SetBkMode(dc, TRANSPARENT);    // 文字背景透明，不画白底。
    DrawTextW(
        dc,
        text.c_str(),
        -1,                        // -1 表示字符串以 '\0' 结尾，让系统自己算长度。
        &win_rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE // 水平居中、垂直居中、单行。
    );

    if (old_font != nullptr) {
        SelectObject(dc, old_font);
    }
}

void draw_tile(HDC dc, const SourceImage& source_image, const Tile& tile, Rect destination) {
    // correct_cell 表示“这块图在完整图片里的原始位置”。
    // 所以渲染时用 correct_cell 算源图裁剪区域，而不是 current_cell。
    const int source_col = tile.correct_cell % PuzzleGame::kGridSize;
    const int source_row = tile.correct_cell / PuzzleGame::kGridSize;
    const int source_x = source_col * PuzzleGame::kTileSize;
    const int source_y = source_row * PuzzleGame::kTileSize;

    // BitBlt 是 block transfer，块传输。
    // 它把 source_image.dc() 中的一块像素复制到目标 dc 的 destination 区域。
    BitBlt(
        dc,                 // 目标 DC。
        destination.x,      // 目标左上角 x。
        destination.y,      // 目标左上角 y。
        destination.width,  // 复制宽度。
        destination.height, // 复制高度。
        source_image.dc(),  // 源 DC。
        source_x,           // 源区域左上角 x。
        source_y,           // 源区域左上角 y。
        SRCCOPY             // 直接复制源像素。
    );

    ScopedPen border(PS_SOLID, 3, rgb(250, 250, 245));
    const HGDIOBJ old_pen = SelectObject(dc, border.get());
    const HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH)); // NULL_BRUSH 表示矩形内部不填充。
    Rectangle(dc, destination.x, destination.y, destination.x + destination.width, destination.y + destination.height);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
}

void draw_frame(HDC dc, AppState& state) {
    fill_rect(dc, {0, 0, PuzzleGame::kWindowWidth, PuzzleGame::kWindowHeight}, rgb(30, 34, 42));

    draw_text_centered(
        dc,
        {0, 18, PuzzleGame::kWindowWidth, 72},
        L"2x2 Puzzle",
        state.title_font,
        rgb(238, 238, 230)
    );

    const Rect board_back{
        PuzzleGame::kBoardLeft - 10,
        PuzzleGame::kBoardTop - 10,
        PuzzleGame::kBoardSize + 20,
        PuzzleGame::kBoardSize + 20,
    };

    ScopedBrush panel_brush(rgb(238, 238, 230));
    ScopedPen panel_pen(PS_SOLID, 4, rgb(24, 24, 28));
    const HGDIOBJ old_brush = SelectObject(dc, panel_brush.get());
    const HGDIOBJ old_pen = SelectObject(dc, panel_pen.get());
    RoundRect(
        dc,
        board_back.x,
        board_back.y,
        board_back.x + board_back.width,
        board_back.y + board_back.height,
        8, // 圆角宽度。
        8  // 圆角高度。
    );
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);

    const Tile* dragged_tile = state.game.dragged_tile();

    for (const Tile& tile : state.game.tiles()) {
        if (&tile == dragged_tile) {
            continue; // 正在拖动的块最后画，保证显示在最上面。
        }

        draw_tile(dc, state.source_image, tile, state.game.cell_rect(tile.current_cell));
    }

    if (dragged_tile != nullptr) {
        const Rect dragged_rect = state.game.dragged_rect();
        fill_rect(
            dc,
            {dragged_rect.x + 8, dragged_rect.y + 8, dragged_rect.width, dragged_rect.height},
            rgb(0, 0, 0)
        );
        draw_tile(dc, state.source_image, *dragged_tile, dragged_rect);

        ScopedPen accent_pen(PS_SOLID, 5, rgb(255, 214, 96));
        const HGDIOBJ previous_pen = SelectObject(dc, accent_pen.get());
        const HGDIOBJ previous_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(
            dc,
            dragged_rect.x,
            dragged_rect.y,
            dragged_rect.x + dragged_rect.width,
            dragged_rect.y + dragged_rect.height
        );
        SelectObject(dc, previous_brush);
        SelectObject(dc, previous_pen);
    }

    if (state.game.completed()) {
        const Rect message_back{
            PuzzleGame::kBoardLeft,
            PuzzleGame::kBoardTop + PuzzleGame::kBoardSize / 2 - 48,
            PuzzleGame::kBoardSize,
            96,
        };

        fill_rect(dc, message_back, rgb(20, 24, 30));
        draw_text_centered(dc, message_back, L"Complete!", state.message_font, rgb(255, 214, 96));
    }
}

AppState* state_from_window(HWND window) {
    // GWLP_USERDATA 是窗口自带的一块“用户数据槽”。
    // 我们在 WM_NCCREATE 里把 AppState* 存进去，这里再取出来。
    //
    // GetWindowLongPtrW 返回 LONG_PTR。
    // LONG_PTR 是“足够容纳一个指针的整数类型”：32 位程序里是 32 位，64 位程序里是 64 位。
    // 这里我们知道里面存的是 AppState*，所以用 reinterpret_cast 转回指针。
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

LRESULT window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_NCCREATE: {
        // WM_NCCREATE 是窗口创建早期消息，早于 WM_CREATE。
        // CreateWindowExW 最后一个参数 lpParam 会被放进 CREATESTRUCTW::lpCreateParams。
        // 我们用它把 AppState* 传进窗口过程。
        // 对 WM_NCCREATE 来说，l_param 里装的是 CREATESTRUCTW*。
        // Windows 消息接口为了通用，只把附加数据暴露成 LPARAM，所以这里需要转型。
        const auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA, // 指定要写入窗口的用户数据槽。
            reinterpret_cast<LONG_PTR>(create_struct->lpCreateParams)
        );
        return TRUE; // TRUE 表示允许窗口继续创建。
    }

    case WM_CREATE: {
        // WM_CREATE 表示窗口已经创建到可以初始化资源的阶段。
        AppState* state = state_from_window(window);
        if (state == nullptr || !state->gdiplus.ok() || !state->source_image.initialize(window)) {
            return -1; // 返回 -1 会让 CreateWindowExW 失败。
        }

        state->title_font = create_font(42, FW_BOLD);
        state->message_font = create_font(42, FW_BOLD);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        AppState* state = state_from_window(window);
        if (state != nullptr) {
            // 捕获鼠标后，即使鼠标拖出窗口，也能继续收到鼠标移动/松开消息。
            SetCapture(window);
            state->game.on_mouse_down({GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});

            // 标记整个客户区需要重画。FALSE 表示不先擦背景，减少闪烁。
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        AppState* state = state_from_window(window);
        if (state != nullptr && state->game.dragging()) {
            state->game.on_mouse_move({GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        AppState* state = state_from_window(window);
        if (state != nullptr) {
            if (GetCapture() == window) {
                ReleaseCapture();
            }
            state->game.on_mouse_up({GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        AppState* state = state_from_window(window);

        // 对 WM_KEYDOWN 来说，w_param 保存虚拟键码。
        // VK_ESCAPE 是 Esc 键，按 Esc 关闭窗口。
        // 'R' 是字母 R 的键码，按 R 重置游戏。
        if (w_param == VK_ESCAPE) {
            DestroyWindow(window); // 触发 WM_DESTROY，最终退出消息循环。
            return 0;
        }

        if (state != nullptr && (w_param == 'R' || w_param == 'r')) {
            state->game.reset();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }

        break;
    }

    case WM_PAINT: {
        // WM_PAINT 表示窗口某区域需要重绘。
        // BeginPaint / EndPaint 必须成对出现。
        AppState* state = state_from_window(window);
        PAINTSTRUCT paint{};
        HDC paint_dc = BeginPaint(window, &paint);

        if (state != nullptr && paint_dc != nullptr) {
            // 双缓冲：先画到内存 DC，再一次性复制到窗口 DC。
            // 这样拖动时不容易闪烁。
            HDC buffer_dc = CreateCompatibleDC(paint_dc);
            HBITMAP buffer_bitmap = CreateCompatibleBitmap(
                paint_dc,
                PuzzleGame::kWindowWidth,
                PuzzleGame::kWindowHeight
            );
            const HGDIOBJ old_bitmap = SelectObject(buffer_dc, buffer_bitmap);

            draw_frame(buffer_dc, *state);
            BitBlt(
                paint_dc,
                0,
                0,
                PuzzleGame::kWindowWidth,
                PuzzleGame::kWindowHeight,
                buffer_dc,
                0,
                0,
                SRCCOPY
            );

            SelectObject(buffer_dc, old_bitmap);
            DeleteObject(buffer_bitmap);
            DeleteDC(buffer_dc);
        }

        EndPaint(window, &paint);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0); // 让 GetMessageW 返回 0，消息循环结束。
        return 0;

    default:
        break;
    }

    // 没有自己处理的消息交给系统默认窗口过程。
    return DefWindowProcW(window, message, w_param, l_param);
}

int run_win64_app(HINSTANCE instance, int show_command) {
    // WNDCLASSEXW 描述一种窗口类型。
    // RegisterClassExW 注册后，CreateWindowExW 才能创建这种类型的窗口。
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class); // cb = count of bytes。结构体大小（字节），供系统识别结构版本。
    window_class.lpfnWndProc = window_proc;     // lpfn = long pointer to function。窗口过程回调函数。
    window_class.hInstance = instance;          // 当前程序实例句柄，来自 WinMain。
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW); // nullptr 表示加载系统内置光标；IDC_ARROW 是默认箭头。
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); // 系统颜色画刷的历史写法，不是普通 HBRUSH 资源。
    window_class.lpszClassName = kWindowClassName; // lpsz = long pointer to zero-terminated string。窗口类名。

    if (RegisterClassExW(&window_class) == 0) {
        // 返回 0 表示注册失败。首版直接用非 0 退出码结束程序。
        return 1;
    }

    // 这里的宽高指“客户区”大小，也就是实际绘图区域。
    // 窗口整体还包括标题栏和边框，所以后面用 AdjustWindowRect 扩大外框。
    RECT window_rect{0, 0, PuzzleGame::kWindowWidth, PuzzleGame::kWindowHeight};
    const DWORD style =
        WS_OVERLAPPED |   // 普通顶层窗口。
        WS_CAPTION |      // 标题栏。
        WS_SYSMENU |      // 系统菜单，包含关闭按钮。
        WS_MINIMIZEBOX;   // 最小化按钮。
    AdjustWindowRect(&window_rect, style, FALSE); // FALSE 表示没有菜单栏。

    // AppState 放在栈上，生命周期覆盖整个消息循环。
    // CreateWindowExW 会通过 WM_NCCREATE 把 &state 存进窗口，后续消息都能拿到它。
    AppState state;
    HWND window = CreateWindowExW(
        0,                                      // 扩展窗口样式。0 表示不使用额外样式。
        kWindowClassName,                       // 要创建的窗口类名，必须和注册时一致。
        kWindowTitle,                           // 标题栏文字。
        style,                                  // 普通窗口样式。
        CW_USEDEFAULT,                          // 让系统决定初始 x 坐标。
        CW_USEDEFAULT,                          // 让系统决定初始 y 坐标。
        window_rect.right - window_rect.left,   // 整个窗口宽度，包含边框。
        window_rect.bottom - window_rect.top,   // 整个窗口高度，包含标题栏和边框。
        nullptr,                                // 父窗口。nullptr 表示顶层窗口。
        nullptr,                                // 菜单句柄。当前游戏没有菜单。
        instance,                               // 程序实例句柄。
        &state                                  // 传给 WM_NCCREATE 的自定义参数。
    );

    if (window == nullptr) {
        return 1;
    }

    ShowWindow(window, show_command); // 按 Windows 给的显示方式显示窗口。
    UpdateWindow(window);             // 立刻触发一次绘制，而不是等下一次消息。

    MSG message{};
    // GetMessageW 从当前线程的消息队列取消息。
    // 第二个参数 nullptr 表示不只过滤某一个窗口，而是取这个线程所有窗口消息。
    // 后两个 0,0 表示不按消息编号范围过滤。
    //
    // 返回值：
    // - 大于 0：拿到普通消息，继续循环。
    // - 等于 0：收到 WM_QUIT，应该退出。
    // - 小于 0：出错。当前首版不做复杂错误处理。
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message); // 把键盘消息转换成字符消息。这里保留标准流程。
        DispatchMessageW(&message); // 把消息发送给 window_proc。
    }

    // message.wParam 是 PostQuitMessage(exit_code) 里传出的退出码。
    return static_cast<int>(message.wParam);
}

}  // namespace puzzle

#endif
