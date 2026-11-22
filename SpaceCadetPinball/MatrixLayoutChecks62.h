#pragma once

#include "GameMenu.h"
#include "TTextBox.h"
#include "fullscrn.h"
#include "options.h"
#include "pb.h"
#include "winmain.h"

namespace matrixLayoutChecks62
{
inline std::vector<Uint32> readPanel(const std::string& file, TTextBox* panel, int width, int height)
{
    SDL_Surface* saved=SDL_LoadBMP(file.c_str());
    if (!saved) return {};
    auto surface=SDL_ConvertSurfaceFormat(saved,SDL_PIXELFORMAT_ARGB8888,0);
    SDL_FreeSurface(saved);
    if (!surface) return {};
    const auto box=fullscrn::GetScreenRectFromPinballRect({panel->OffsetX,panel->OffsetY,width,height});
    std::vector<Uint32> data;
    if (box.x>=0 && box.y>=0 && box.x+box.w<=surface->w && box.y+box.h<=surface->h)
        for (int y=box.y; y<box.y+box.h; ++y)
        {
            const auto* line=reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(surface->pixels)+y*surface->pitch);
            data.insert(data.end(),line+box.x,line+box.x+box.w);
        }
    SDL_FreeSurface(surface);
    return data;
}

template<class Capture>
bool run(FILE* report,const std::string& prefix,Capture capture)
{
    auto status=pb::InfoTextBox,mission=pb::MissTextBox;
    int initialWidth,initialHeight;
    SDL_GetWindowSize(winmain::MainWindow,&initialWidth,&initialHeight);
    const auto language=options::Options.Language.V;
    bool ok=true;
    for (const int width : {600,900,1200})
    {
        winmain::SetWindowSize(width);
        for (const char* locale : {"ru","en"})
        {
            GameMenu::SetLanguage(locale);
            const auto stem=prefix+"led62-"+locale+"-"+std::to_string(width);
            const auto shortFile=stem+"-short.bmp", longFile=stem+"-long.bmp";
            mission->Clear(); mission->Display(Msg::STRING205,-1.f);
            status->Clear(); status->Display(Msg::STRING127,-1.f);    // Player 1
            capture("");
            ok &= capture(shortFile);
            status->Clear(); status->Display(Msg::STRING198,-1.f);    // Player 1 Shoot Again
            ok &= capture(longFile);
            const auto shortPrefix=readPanel(shortFile,status,47,20);
            const auto longPrefix=readPanel(longFile,status,47,20);
            const auto lit=std::count_if(shortPrefix.begin(),shortPrefix.end(),[](Uint32 c) {
                return (c&255)>90 && (c&255)>((c>>16)&255);
            });
            const bool same=lit>30 && shortPrefix==longPrefix;
            fprintf(report,"%s  %s %d: short and wrapped status have identical LED positions and intensities in their shared prefix\n",
                    same ? "PASS":"FAIL",locale,width);
            ok &= same;
        }
    }

    const auto tail=prefix+"led62-tail.bmp", first=prefix+"led62-page1.bmp", last=prefix+"led62-page2.bmp";
    status->Clear(); status->Display("333",-1.f);
    mission->Clear(); mission->Display("555",-1.f);
    ok &= capture(tail);
    status->Clear(); status->Display("111\n222\n333",2.f);
    mission->Clear(); mission->Display("111\n222\n333\n444\n555",2.f);
    ok &= capture(first);
    const auto ticks=pb::time_ticks;
    // Advance only the display clock. A live collision must not replace these
    // messages while their texture pages are being compared.
    pb::time_ticks=ticks+1100;
    ok &= capture(last);
    for (auto panel : {status,mission})
    {
        const auto expected=readPanel(tail,panel,panel->Width,panel->Height);
        const bool shown=!expected.empty() && readPanel(last,panel,panel->Width,panel->Height)==expected &&
                         readPanel(first,panel,panel->Width,panel->Height)!=expected;
        fprintf(report,"%s  last %s page matches its reference at native pitch before the message expires\n",
                shown ? "PASS":"FAIL",panel==status ? "status":"mission");
        ok &= shown;
    }
    const auto held=prefix+"led62-held.bmp";
    status->Display("111\n222\n333",2.f); // repeated switch extends the current notice
    ok &= capture(held);
    const auto panelPixels=[&](const std::string& path) { return readPanel(path,status,status->Width,status->Height); };
    const bool extended=panelPixels(held)==panelPixels(last);
    fprintf(report,"%s  extending a notice keeps its current LED page\n",extended?"PASS":"FAIL");
    ok &= extended;
    pb::time_ticks=ticks;
    status->Clear(); status->Display("111\n222\n333",2.f);
    status->CurrentMessage->EndTicks=ticks-100; // a queued notice in its final grace period
    ok &= capture(held);
    const auto grace=panelPixels(held);
    bool steady=!grace.empty();
    for (const int ms : {51,123,249})
    {
        pb::time_ticks=ticks+ms;
        ok &= capture(held);
        steady &= panelPixels(held)==grace;
    }
    fprintf(report,"%s  an overdue notice stays on one page for its final 250 milliseconds\n",steady?"PASS":"FAIL");
    ok &= steady;
    pb::time_ticks=ticks;
    status->Clear(); mission->Clear();
    GameMenu::SetLanguage(language.c_str());
    winmain::SetWindowSize(initialWidth,initialHeight);
    return ok;
}
}
