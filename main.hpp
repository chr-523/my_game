#pragma once

// main.hpp
// 根目录下的 main.cc 是可执行程序入口。
//
// 这个文件只作为 main.cc 的 include 集合：
// - 它集中包含 include/ 目录里的项目头文件。
// - 它不放具体实现。
// - 它不会被 include/ 目录里的其他头文件反向引用。
//
// 这样 main.cc 只需要 #include "main.hpp"，入口侧能看到需要的项目声明；
// 具体模块自己的 .cc 文件仍然应该 include 自己对应的 .hpp。

#include "puzzle_game.hpp"
#include "win64_app.hpp"
