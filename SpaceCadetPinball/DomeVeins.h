#pragma once
#include "SDL.h"
#include <cstdint>

namespace DomeVeins
{
void Init(const SDL_Surface* board);
void Reset();
void ClearHighlight();
void Draw(float seconds, const SDL_FRect& dest, float sx, float sy, const uint16_t* owners, int stride);
void CoverWithBall(const uint32_t* pixels, int side, int left, int top);
void PublishHighlight(const SDL_FRect& dest, float sx, float sy, int outputWidth, int outputHeight);
void SetDiagnosticBypass(bool enabled);
void SetDiagnosticHdrBypass(bool enabled);
} // namespace DomeVeins
