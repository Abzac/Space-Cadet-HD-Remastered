#pragma once
#include "SDL.h"
#include <cstdint>
#include <vector>

namespace CabinetEffects
{
struct Light
{
	float x, y, radius, intensity;
};
void Init(const SDL_Surface* board, const SDL_Surface* poster, const SDL_Surface* planetMap);
void SetSling(int side, int x, int y, SDL_Surface* const frames[3]);
void Strike(int side, float time);
void Draw(float gameTime, float decorTime, const SDL_FRect& destination, int viewWidth, int viewHeight, const uint16_t* owners,
		  int stride, std::vector<Light>& lights);
float FlashStrength(int side, float time);
void SetCelestialAccentDiagnosticBypass(bool bypass);
void Reset();
} // namespace CabinetEffects
