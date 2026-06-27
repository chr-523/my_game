#pragma once

// puzzle_game.hpp
// 这里声明游戏的核心逻辑类 PuzzleGame。
//
// 这一层不画窗口、不调用 Windows API，也不处理 GDI 资源。
// 它只负责保存和更新“拼图游戏状态”：
// - 四块拼图当前在哪
// - 哪一块正在被拖动
// - 鼠标和拖动块之间的偏移
// - 游戏是否完成
//
// 渲染层只需要读取 PuzzleGame 暴露出来的状态，然后把它画出来。

#include <array>
#include <optional>
#include <random>

namespace puzzle {

// Point 表示一个二维点。
// 鼠标位置或拖拽位置。
struct Point {
    // 从左到右
    int x = 0;
    // 从上到下
    int y = 0;
};

// Rect 表示一个矩形区域。
// 在当前游戏中，它通常表示：
// - 整个棋盘的区域
// - 某个格子的区域
// - 正在拖动的拼图块区域
struct Rect {
    // 矩形左上角的 x 坐标。
    int x = 0;
    // 矩形左上角的 y 坐标。
    int y = 0;
    // 矩形宽度。
    int width = 0;
    // 矩形高度。
    int height = 0;

    // 判断一个点是否落在这个矩形里面。
    // 这里使用左闭右开、上闭下开的判断。
    
    bool contains(Point point) const {
        return point.x >= x &&
               point.x < x + width &&
               point.y >= y &&
               point.y < y + height;
    }
};

// Tile 一块拼图的正确位置和当前位置标签。
struct Tile {
    // correct_cell 表示这块拼图的正确格子编号。
    // eg. 
    // - 2x2 棋盘的编号方式是：
    //      0 1
    //      2 3
    // - correct_cell == 0 表示它本来就是完整图片左上角那块。
    int correct_cell = 0;
    // current_cell 表示这块拼图当前正在棋盘的哪个格子里。
    // 打乱拼图时会修改 current_cell。
    // 拖拽交换时会交换 current_cell。
    int current_cell = 0;
    // 判断是否通关时，只需要检查每个 Tile 的：
    // current_cell == correct_cell
};

class PuzzleGame {
public:
    // 游戏窗口的逻辑宽度，单位是像素。
    static constexpr int kWindowWidth = 720;
    // 游戏窗口的逻辑高度，单位是像素。
    static constexpr int kWindowHeight = 720;
    // 棋盘是正方形，这里设置边长为 520 像素。
    static constexpr int kBoardSize = 520;
    // 当前只做 2x2 关卡，所以每行每列都是 2 格。
    static constexpr int kGridSize = 2;
    // 单个拼图块边长。
    // 520 / 2 = 260 像素。
    static constexpr int kTileSize = kBoardSize / kGridSize;
    // 棋盘左上角 x 坐标。
    // 这样计算可以让棋盘在窗口中水平居中。
    static constexpr int kBoardLeft = (kWindowWidth - kBoardSize) / 2;
    // 棋盘左上角 y 坐标。
    // 上方留出空间给标题。
    static constexpr int kBoardTop = 120;
    // 拼图块总数。
    // 2x2 下就是 4。
    static constexpr int kTileCount = kGridSize * kGridSize;
    // 构造函数会初始化随机数引擎，并调用 reset() 生成第一局。
    PuzzleGame()
        : random_engine_(std::random_device{}()) {
        // 构造完成后立刻开始一局。
        reset();
    }
    // 重新开始当前 2x2 关卡。
    // 会恢复四块拼图的基础数据，然后随机打乱 current_cell。
    void reset();
    // 鼠标左键按下时调用。
    // 如果点中了棋盘内的一块拼图，就进入“拖动中”状态。
    void on_mouse_down(Point position);
    // 鼠标移动时调用。
    // 如果当前正在拖动拼图，就更新拖动块的显示位置。
    void on_mouse_move(Point position);
    // 鼠标左键松开时调用。
    // 如果松手位置在另一块拼图上，就交换两块 current_cell。
    void on_mouse_up(Point position);
    // 当前是否已经拼好。
    bool completed() const {
        return completed_;
    }
    // 当前是否有一块拼图正在被鼠标拖动。
    bool dragging() const {
        return dragged_index_.has_value();
    }
    // 读取全部 Tile。
    // 返回 const 引用，外部可以读，但不能直接改内部状态。
    const std::array<Tile, kTileCount>& tiles() const {
        return tiles_;
    }
    // 返回正在拖动的 Tile。
    // 没有拖动时返回 nullptr。
    const Tile* dragged_tile() const;
    // 根据格子编号计算这个格子在窗口里的矩形区域。
    Rect cell_rect(int cell) const;
    // 返回正在拖动拼图当前应该画在哪个矩形区域。
    // 只有 dragging() 为 true 时，这个值才有实际意义。
    Rect dragged_rect() const {
        return {
            drag_position_.x,
            drag_position_.y,
            kTileSize,
            kTileSize,
        };
    }
    // 把屏幕坐标转换成棋盘格子编号。
    // 如果 position 不在棋盘内，则返回 std::nullopt。
    std::optional<int> cell_at(Point position) const;

private:
    // 把 tiles_ 初始化成“已完成”的基础状态：
    // 第 0 块在第 0 格，第 1 块在第 1 格，以此类推。
    void initialize_tiles();
    // 根据 current_cell 查找当前位于某个格子的 Tile 下标。
    // 找不到时返回 -1。
    int tile_index_at_cell(int cell) const;
    // 实际检查是否全部归位。
    bool is_complete() const;
    // 保存四块拼图的数据。
    // 每个 Tile 只有 correct_cell 和 current_cell 两个核心字段。
    std::array<Tile, kTileCount> tiles_{};
    // 随机数引擎
    std::mt19937 random_engine_;
    // 当前正在拖动的拼图在 tiles_ 里的下标。
    // 没有拖动时为空。
    std::optional<int> dragged_index_;
    // 鼠标点下去的位置相对于拼图左上角的偏移。
    //
    // 例如玩家点在拼图中间拖动，如果没有这个偏移，拼图左上角会突然跳到鼠标位置。
    // 保存偏移后，拖动时鼠标仍然保持在拼图的同一个相对位置。
    Point drag_offset_{};
    // 正在拖动的拼图左上角屏幕坐标。
    // 渲染层会用它画出“跟着鼠标走”的拼图块。
    Point drag_position_{};
    // 拖动开始时，被拖动拼图原来所在的格子。
    // 松手交换时，目标拼图会被放回这个原格子。
    std::optional<int> original_cell_;
    // 缓存当前是否已完成。
    // 每次 reset() 或 mouse_up 后更新。
    bool completed_ = false;
};

}  // namespace puzzle
