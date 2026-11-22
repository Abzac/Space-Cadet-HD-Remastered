#pragma once
#include <string>
#include <cstdint>
#include "SDL.h"

namespace RailReflections
{
    void Init(const std::string& hdDirectory);
    void Shutdown();
    void Draw(SDL_Renderer* renderer, const SDL_FRect& destination, int viewWidth, int viewHeight,
              const uint16_t* owners, int hdPitch, int boardWidth, float cx, float cy, float radius,
              const uint32_t* ballPixels, int side, int originX, int originY);
    int LastSampleCount();
    void SetDiagnosticBypass(bool bypass);
    void SetAdditionalDiagnosticBypass(bool bypass);
}
