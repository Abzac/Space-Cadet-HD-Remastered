#pragma once

#include <string>
#include "SDL.h"

// Pages are cached at the DAT panel's native dot pitch. The window scale
// applies to the complete cabinet, including this aperture and the score.
class CabinetDisplay
{
public:
    ~CabinetDisplay();
    void Draw(SDL_Renderer* renderer, SDL_Rect aperture, int nativeWidth, int nativeHeight,
              const char* text, int endTicks);
    void BeginMessage(int ticks);
    static bool SupportsLanguage();
    static void DrawNumber(const std::string& digits, float right, float centerY, float pitch, Uint32 color);

private:
    SDL_Texture* texture = nullptr;
    std::string cachedText;
    int width = 0, height = 0, pages = 0, startTicks = 0;
    int pageDwellMs = 0;
    bool cachedHD=false;
};
