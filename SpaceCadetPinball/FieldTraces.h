#pragma once
#include "SDL.h"
#include <cstdint>

namespace FieldTraces
{
void Init(const SDL_Surface *board);
void Draw(float seconds, const SDL_FRect &dest, int width, int height, const uint16_t *owners, int stride);
void Reset();
void SetDiagnosticBypass(bool bypass);
} // namespace FieldTraces
