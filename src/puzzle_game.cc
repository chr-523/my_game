#include "puzzle_game.hpp"

#include <algorithm>
#include <numeric>

namespace puzzle {

// 先把四块拼图恢复到基础状态，然后随机打乱 current_cell
void PuzzleGame::reset() {
    // 初始化reset()时的基础状态：每块拼图都在正确位置。
    initialize_tiles();
    // 关卡初始化位置数据为 0,1,2,3，表示每块拼图都在正确位置。
    std::array<int, kTileCount> positions{};
    std::iota(positions.begin(), positions.end(), 0);

    // 这里不用 is_complete()
    // positions 是暂存的数组，不是 tiles_。
    auto already_solved = [&positions]() {
        for (int index = 0; index < kTileCount; ++index) {
            if (positions[index] != index) {
                return false;
            }
        }

        return true;
    };

    // 随机打乱 positions 直到不是“已完成”状态。
    do {
        std::shuffle(positions.begin(), positions.end(), random_engine_);
    } while (already_solved());

    // 把随机位置写回每个 Tile。
    for (int index = 0; index < kTileCount; ++index) {
        tiles_[index].current_cell = positions[index];
    }

    // 重置所有拖拽相关状态。
    // 如果玩家按 R 时刚好正在拖动，也会取消拖动。
    dragged_index_.reset();
    original_cell_.reset();
    drag_offset_ = {};
    drag_position_ = {};

    // 重新计算完成状态。
    // 理论上 这个值由lambda保证为false
    completed_ = is_complete();
}

// 处理鼠标左键按下。
//
// 这里不直接移动拼图，只是判断“是否开始拖动某一块”。
void PuzzleGame::on_mouse_down(Point position) {
    // 已完成后禁止继续拖动。
    if (completed_) {
        return;
    }

    // 把鼠标位置转换成棋盘格子编号。
    // 如果点在棋盘外，返回空。
    const std::optional<int> selected_cell = cell_at(position);
    if (!selected_cell.has_value()) {
        return;
    }

    // 找出当前位于 selected_cell 的拼图块下标。
    const int selected_index = tile_index_at_cell(*selected_cell);
    if (selected_index < 0) {
        return;
    }

    // 记录“当前正在拖哪一块”。
    dragged_index_ = selected_index;

    // 记录拖动开始时这块拼图原本在哪个格子。
    // 松手交换时，目标拼图要被放回这个格子。
    original_cell_ = tiles_[selected_index].current_cell;

    // 计算这块拼图当前所在格子的矩形区域。
    const Rect rect = cell_rect(tiles_[selected_index].current_cell);

    // 记录鼠标按下点相对拼图左上角的偏移。
    //
    // 如果玩家点在拼图中间，拖动时拼图也应该保持“鼠标抓住中间”的感觉，
    // 而不是让拼图左上角突然跳到鼠标位置。
    drag_offset_ = {position.x - rect.x, position.y - rect.y};

    // 根据鼠标位置和偏移，算出拖动块左上角坐标。
    drag_position_ = {position.x - drag_offset_.x, position.y - drag_offset_.y};
}

// 处理鼠标移动。
//
// 只有正在拖动拼图时，鼠标移动才会改变游戏显示状态。
void PuzzleGame::on_mouse_move(Point position) {
    if (!dragged_index_.has_value()) {
        return;
    }

    // 保持鼠标与拼图左上角的相对偏移不变。
    drag_position_ = {position.x - drag_offset_.x, position.y - drag_offset_.y};
}

// 处理鼠标左键松开。
//
// 松手时才真正决定是否交换拼图块。
void PuzzleGame::on_mouse_up(Point position) {
    // 如果当前没有拖动任何拼图，松手事件不需要处理。
    if (!dragged_index_.has_value()) {
        return;
    }

    // 判断松手位置落在哪个格子。
    const std::optional<int> target_cell = cell_at(position);

    // 只有松手在棋盘内，并且我们知道原始格子，才尝试交换。
    if (target_cell.has_value() && original_cell_.has_value()) {
        // 找出目标格子里的拼图块。
        const int target_index = tile_index_at_cell(*target_cell);

        // 如果目标格子里有拼图，并且目标不是自己，就交换两块的位置。
        if (target_index >= 0 && target_index != *dragged_index_) {
            // 目标拼图回到被拖动拼图原来的格子。
            tiles_[target_index].current_cell = *original_cell_;

            // 被拖动拼图进入松手目标格子。
            tiles_[*dragged_index_].current_cell = *target_cell;
        }
    }

    // 无论是否成功交换，松手后拖动都结束。
    dragged_index_.reset();
    original_cell_.reset();
    drag_offset_ = {};
    drag_position_ = {};

    // 松手后检查是否完成。
    completed_ = is_complete();
}

// 返回正在拖动的 Tile 指针。
//
// 用指针的原因：
// - 有拖动时返回 Tile*
// - 没拖动时返回 nullptr
const Tile* PuzzleGame::dragged_tile() const {
    if (!dragged_index_.has_value()) {
        return nullptr;
    }

    return &tiles_[*dragged_index_];
}

// 把格子编号转换成屏幕矩形。
Rect PuzzleGame::cell_rect(int cell) const {
    // 行号：0/1。
    const int row = cell / kGridSize;

    // 列号：0/1。
    const int col = cell % kGridSize;

    // 用棋盘左上角加上行列偏移，得到格子左上角坐标。
    return {
        kBoardLeft + col * kTileSize,
        kBoardTop + row * kTileSize,
        kTileSize,
        kTileSize,
    };
}

// 把屏幕坐标转换成棋盘格子编号。
std::optional<int> PuzzleGame::cell_at(Point position) const {
    // 先构造整个棋盘的矩形区域。
    const Rect board{
        kBoardLeft,
        kBoardTop,
        kBoardSize,
        kBoardSize,
    };

    // 鼠标不在棋盘内，就没有对应格子。
    if (!board.contains(position)) {
        return std::nullopt;
    }

    // 转换成棋盘内部坐标，再除以单块大小得到列号和行号。
    const int col = (position.x - kBoardLeft) / kTileSize;
    const int row = (position.y - kBoardTop) / kTileSize;

    // 2x2 编号：
    // row=0,col=0 -> 0
    // row=0,col=1 -> 1
    // row=1,col=0 -> 2
    // row=1,col=1 -> 3
    return row * kGridSize + col;
}

// 初始化 Tile 数组。
void PuzzleGame::initialize_tiles() {
    for (int index = 0; index < kTileCount; ++index) {
        // Tile{index, index} 表示：
        // - correct_cell = index
        // - current_cell = index
        //
        // 也就是每块图都在正确位置。
        tiles_[index] = Tile{index, index};
    }
}

// 查找当前位于某格子的 Tile 下标。
int PuzzleGame::tile_index_at_cell(int cell) const {
    for (int index = 0; index < kTileCount; ++index) {
        if (tiles_[index].current_cell == cell) {
            return index;
        }
    }

    // 正常状态下不应该出现找不到的情况。
    // 返回 -1 是为了让调用方可以安全处理异常状态。
    return -1;
}

// 检查是否每块拼图都回到了正确格子。
bool PuzzleGame::is_complete() const {
    return std::all_of(tiles_.begin(), tiles_.end(), [](const Tile& tile) {
        return tile.correct_cell == tile.current_cell;
    });
}

}  // namespace puzzle
