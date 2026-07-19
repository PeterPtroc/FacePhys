#pragma once

#include <array>
#include <cstddef>

namespace facephys {

struct StateMapEntry {
  int input_position;
  int output_position;
};

// Exact source map from inference_worker.js. LiteRT JS reorders input and
// output detail arrays, so these positions cannot be used directly with the
// TFLite C++ Interpreter ordering.
inline constexpr std::array<StateMapEntry, 46> kBrowserLiteRtStateMap{{
    {2, 1},   {3, 12},  {14, 23}, {25, 34}, {36, 42}, {43, 43},
    {44, 44}, {45, 45}, {46, 46}, {47, 2},  {4, 3},   {5, 4},
    {6, 5},   {7, 6},   {8, 7},   {9, 8},   {10, 9},  {11, 10},
    {12, 11}, {13, 13}, {15, 14}, {16, 15}, {17, 16}, {18, 17},
    {19, 18}, {20, 19}, {21, 20}, {22, 21}, {23, 22}, {24, 24},
    {26, 25}, {27, 26}, {28, 27}, {29, 28}, {30, 29}, {31, 30},
    {32, 31}, {33, 32}, {34, 33}, {35, 35}, {37, 36}, {38, 37},
    {39, 38}, {40, 39}, {41, 40}, {42, 41},
}};

// Verified native TFLite mapping. The FlatBuffer presents state_in_0..45 at
// positions 2..47 and their matching recurrent outputs Identity_1..46 at
// positions 1..46. This is a translation of the browser map above, not a
// different recurrence algorithm.
inline constexpr std::array<StateMapEntry, 46> kNativeTfliteStateMap{{
    {2, 1}, {3, 2}, {4, 3}, {5, 4}, {6, 5}, {7, 6}, {8, 7}, {9, 8},
    {10, 9}, {11, 10}, {12, 11}, {13, 12}, {14, 13}, {15, 14},
    {16, 15}, {17, 16}, {18, 17}, {19, 18}, {20, 19}, {21, 20},
    {22, 21}, {23, 22}, {24, 23}, {25, 24}, {26, 25}, {27, 26},
    {28, 27}, {29, 28}, {30, 29}, {31, 30}, {32, 31}, {33, 32},
    {34, 33}, {35, 34}, {36, 35}, {37, 36}, {38, 37}, {39, 38},
    {40, 39}, {41, 40}, {42, 41}, {43, 42}, {44, 43}, {45, 44},
    {46, 45}, {47, 46},
}};

}  // namespace facephys
