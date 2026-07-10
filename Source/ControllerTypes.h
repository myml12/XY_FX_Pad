#pragma once

#include <array>

struct ControllerSample
{
    float x = 0.5f;
    float y = 0.0f;
    bool touching = false;
    float totalGrams = 0.0f;
    std::array<float, 4> grams {};
};
