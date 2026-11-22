#pragma once

#include "RailReflections.h"
#include "VisualChecks.h"

namespace metalChecks4
{
struct Case
{
    const char* name;
    vector2i groundPoint;
    SDL_Rect metal;
    SDL_Rect surroundingPrint;
};

inline bool run(FILE* report, const std::string& prefix)
{
    // All positions run through the normal sphere projection and lighting.
    // Pause between readbacks so the ball, switches and cabinet glow have
    // identical states with and without the new reflected-ball receivers.
    const Case cases[] = {
        {"left-return",{41,343},{28,333,9,14},{42,330,5,6}},
        {"right-bridge",{296,260},{280,242,22,15},{300,256,5,5}},
        {"upper-left-bridge",{131,41},{133,27,12,15},{123,44,6,5}},
        {"upper-right-bridge",{234,42},{219,25,18,18},{236,44,5,5}},
        {"target-hood",{259,231},{254,211,23,20},{250,231,7,6}}
    };
    auto ball=pb::MainTable->BallList.front();
    ball->ActiveFlag=1;
    ball->CollisionDisabledFlag=false;
    ball->CollisionComp=nullptr;
    ball->CollisionFlag=0;
    ball->EdgeCollisionCount=0;
    ball->CollisionMask=1;
    ball->Speed=0;
    bool good=true;
    for (const auto& test:cases)
    {
        const auto world=proj::ReverseXForm(test.groundPoint);
        ball->Position={world.X,world.Y,ball->Radius};
        ball->Repaint();
        render::update();
        const auto state=prefix+"metal-"+test.name;
        uint32_t metalOn=0,metalOff=0,printOn=0,printOff=0;
        RailReflections::SetAdditionalDiagnosticBypass(false);
        bool captured=visualChecks::capture(state+".bmp",test.metal,metalOn);
        uint32_t overview=0;
        const SDL_Rect context={test.metal.x-13,test.metal.y-12,test.metal.w+26,test.metal.h+24};
        captured &= visualChecks::capture(state+"-context.bmp",context,overview);
        captured&=visualChecks::capture(state+"-print.bmp",test.surroundingPrint,printOn);
        RailReflections::SetAdditionalDiagnosticBypass(true);
        captured&=visualChecks::capture(state+"-reference.bmp",test.metal,metalOff);
        captured&=visualChecks::capture(state+"-print-reference.bmp",test.surroundingPrint,printOff);
        RailReflections::SetAdditionalDiagnosticBypass(false);
        float radius;
        const auto center=proj::BallImage(ball->Position,ball->Radius,radius);
        const bool matches=captured && metalOn!=metalOff && printOn==printOff;
        fprintf(report,"%s  %s: sphere (%.2f, %.2f), metal %08x / %08x, print %08x / %08x.\n",
                matches?"PASS":"FAIL",test.name,center.X,center.Y,metalOn,metalOff,printOn,printOff);
        good &= matches;
    }
    return good;
}
}
