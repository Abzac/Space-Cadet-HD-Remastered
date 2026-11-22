#pragma once

#include "GameMenu.h"
#include "HdrOutput.h"
#include "options.h"
#include "winmain.h"

namespace matrixChecks61
{
inline bool compare(FILE* report)
{
    HdrOutput::MatrixMeasurement numbers{};
    for (unsigned i : {2u,3u,4u})
    {
        const auto value=HdrOutput::LastMatrix(i);
        if (!value.litPixels) return false;
        numbers.brightest+=value.brightest/3;
        numbers.litMean+=value.litMean/3;
        numbers.redOverBlue+=value.redOverBlue/3;
        numbers.greenOverBlue+=value.greenOverBlue/3;
    }
    fprintf(report,"  Numeric lenses: peak %.1f, mean lit %.1f nits, linear R/B %.3f, G/B %.3f.\n",
            numbers.brightest,numbers.litMean,numbers.redOverBlue,numbers.greenOverBlue);
    bool ok=true;
    for (unsigned i : {0u,1u})
    {
        const auto m=HdrOutput::LastMatrix(i);
        fprintf(report,"  %s lenses: peak %.1f, mean lit %.1f nits, linear R/B %.3f, G/B %.3f.\n",
                i?"Mission":"Status",m.brightest,m.litMean,m.redOverBlue,m.greenOverBlue);
        ok &= m.litPixels>0 && m.darkest<2 && numbers.brightest>0 &&
            std::abs(m.brightest/numbers.brightest-1)<.17f &&
            std::abs(m.litMean/numbers.litMean-1)<.27f &&
            std::abs(m.redOverBlue-numbers.redOverBlue)<.10f &&
            std::abs(m.greenOverBlue-numbers.greenOverBlue)<.09f;
    }
    return ok;
}

// Run while the first ball rests on the plunger. Language and gameplay time
// remain stable while SDL renders the same physical lamps at each window size.
template <class Capture>
bool awaiting(FILE* report,const std::string& prefix,Capture capture)
{
    int initialWidth,initialHeight;
    SDL_GetWindowSize(winmain::MainWindow,&initialWidth,&initialHeight);
    const std::string language=options::Options.Language.V;
    bool ok=true;
    for (int width : {600,900,1200})
    {
        winmain::SetWindowSize(width);
        for (const char* locale : {"ru","en"})
        {
            GameMenu::SetLanguage(locale);
            const std::string label=std::string("cabinet61-")+locale+"-"+std::to_string(width);
            capture("",false); // refresh SDL framebuffer and font-size state
            auto frame=capture(prefix+label+".bmp",true);
            ok &= frame.saved && frame.width>0;
            if (HdrOutput::Active())
            {
                fprintf(report,"%s, %d x %d:\n",label.c_str(),frame.width,frame.height);
                ok &= compare(report);
            }
        }
    }
    GameMenu::SetLanguage(language.c_str());
    winmain::SetWindowSize(initialWidth);
    return ok;
}
}
