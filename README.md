# my_game

A small C++ Windows x64 2x2 drag-and-swap puzzle game.

## Layout

- `main.cc`: executable entry.
- `main.hpp`: include bundle used by `main.cc`.
- `include/puzzle_game.hpp`: puzzle state and game rules declarations.
- `src/puzzle_game.cc`: implementation for `include/puzzle_game.hpp`.
- `include/win64_app.hpp`: Windows x64 app entry declaration.
- `src/win64_app.cc`: implementation for `include/win64_app.hpp`.
- `picture/xuk.png`: source picture loaded at runtime and split into the 2x2 puzzle.
- `lib/`: external libraries. Empty for now because this project has no external library dependency.

## Asset

The game loads `picture/xuk.png` with Windows GDI+.
The current board is square, so the image is center-cropped to a square before being scaled to the board.

## Build

```powershell
cmake -S . -B build
cmake --build build
```

## Controls

- Drag a tile onto another tile to swap them.
- Press `R` to reshuffle.
- Press `Esc` to quit.
