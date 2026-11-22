#pragma once

#include "HdrOutput.h"
#include "TLight.h"
#include "fullscrn.h"
#include "pb.h"
#include "render.h"
#include "winmain.h"

namespace starStripHdrChecks632
{
inline bool run(FILE* report)
{
    auto table = pb::MainTable;
    auto renderer = winmain::Renderer;
    auto pink = dynamic_cast<TLight*>(table->find_component("lite1"));
    auto violet = dynamic_cast<TLight*>(table->find_component("lite48"));
    if (!pink || !violet || !table->LightShowTimer) return false;

    // Use the same window and control lamps as the HD 6.3.1 FP16 measurement.
    winmain::SetWindowSize(1200);
    fullscrn::window_size_changed();
    int width = 0, height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) || width <= 0 || height <= 0) return false;
    const auto dest = render::DestinationRect;
    const float sx = float(dest.w) / render::vscreen->Width, sy = float(dest.h) / render::vscreen->Height;
    if (sx <= 0 || sy <= 0) return false;
    pink->SetSpriteBmp(pink->BmpArr[1]);
    violet->SetSpriteBmp(violet->BmpArr[1]);
    render::update();

    std::vector<HdrOutput::LuminanceSite> sites;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const float u = (x + .5f - dest.x) / sx, v = (y + .5f - dest.y) / sy;
            if (u >= 114 && u <= 252 && v >= 307 && v < 416)
                sites.push_back({(x + .5f) / width, (y + .5f) / height});
        }
    if (sites.empty()) return false;

    const char* hint = "SPACECADET_BREATHE_STAR_OUTLINES";
    const char* original = SDL_GetHint(hint);
    const bool hadHint = original != nullptr;
    const std::string saved = original ? original : "";
    auto sample = [&](float seconds, bool breathe, std::vector<HdrOutput::LuminanceSite>& frame) {
        if (!SDL_SetHintWithPriority(hint, breathe ? "1" : "0", SDL_HINT_OVERRIDE)) return false;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        render::PresentVScreen(seconds);
        frame = sites;
        return HdrOutput::Present(winmain::MainWindow, renderer, true, true, false, frame.data(),
                                  unsigned(frame.size())) && HdrOutput::Active();
    };
    bool ok = true;
    for (int c = 0; c < 2; ++c)
    {
        // At a fixed instant the two styles differ solely in strip intensity.
        // Only the active circuit brightens, so its visible footprint can be
        // identified independently of moving lights and the printed background.
        const float select = c ? .65f : .3f, peakTime = c ? .9f : .3f;
        std::vector<HdrOutput::LuminanceSite> bright, dim, current, former;
        if (!sample(select, false, bright) || !sample(select, true, dim) ||
            !sample(peakTime, false, current) || !sample(c ? 1.f : .8f, true, former))
        {
            ok = false;
            break;
        }
        float peak = 0, previousPeak = 0, lamp = 0;
        int measured = 0;
        for (size_t i = 0; i < sites.size(); ++i)
        {
            const float x = (sites[i].u * width - dest.x) / sx;
            const float y = (sites[i].v * height - dest.y) / sy;
            if (fabsf(x - 182.f) < 3.f && fabsf(y - (c ? 312.f : 394.f)) < 3.f)
                lamp = std::max(lamp, current[i].nits);
            if (bright[i].nits - dim[i].nits <= .5f) continue;
            ++measured;
            peak = std::max(peak, current[i].nits);
            previousPeak = std::max(previousPeak, former[i].nits);
        }
        fprintf(report, "FP16 star strip %s at %.2fs: %d pixels, peak %.3f nits, former crest %.3f, lamp %.3f.\n",
                c ? "violet" : "pink", peakTime, measured, peak, previousPeak, lamp);
        ok &= measured > 100 && peak > 12 && lamp > 160 && peak < lamp && peak <= previousPeak + .1f;
    }
    if (hadHint) SDL_SetHintWithPriority(hint, saved.c_str(), SDL_HINT_OVERRIDE);
    else SDL_ResetHint(hint);
    return ok;
}
}
