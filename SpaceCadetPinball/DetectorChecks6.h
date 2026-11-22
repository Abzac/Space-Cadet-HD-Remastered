#pragma once

#include "TRollover.h"
#include "VisualChecks.h"

namespace detectorChecks6
{
inline bool run(FILE* report, const std::string& prefix)
{
    const char* names[] = {"a_roll1","a_roll2","a_roll3","a_roll4","a_roll5","a_roll6",
                           "a_roll7","a_roll8","a_roll110","a_roll111","a_roll112"};
    const char* lamps[] = {"lite10","lite9","lite8",nullptr,nullptr,nullptr,nullptr,nullptr,
                           "lite169","lite170","lite171"};
    auto table = pb::MainTable;
    auto ball = table->BallList.front();
    const vector3 oldPosition = ball->Position;
    const bool oldGraphics = render::EnhancedGraphics;
    render::EnhancedGraphics = true;
    bool ok = true;
    std::vector<TRollover*> rolls;
    auto findEdge = [](TRollover* roll, size_t tag) -> TEdgeSegment* {
        for (const auto edge : roll->EdgeList)
            if (reinterpret_cast<size_t>(edge->WallValue) == tag) return edge;
        return nullptr;
    };
    for (int i = 0; i < 11; ++i)
    {
        auto roll = dynamic_cast<TRollover*>(table->find_component(names[i]));
        if (!roll || roll->GroupIndex != 520+i || !findEdge(roll,600) || !findEdge(roll,603))
        {
            render::EnhancedGraphics = oldGraphics;
            return false;
        }
        rolls.push_back(roll);
        roll->Message(MessageCode::Reset,0);
    }
    // Start with an unlit set to test an ordinary pass, independently of a
    // bumper-upgrade bonus, current mission, and the attract light pattern.
    for (auto name : {"lite10","lite9","lite8","lite169","lite170","lite171","lite16","lite27","lite28"})
        table->find_component(name)->Message(MessageCode::TLightResetAndTurnOff,0);
    vector2 position{oldPosition.X,oldPosition.Y}, direction{0,-1};
    for (int i=0; i<11; ++i)
    {
        auto roll=rolls[i];
        auto light=lamps[i] ? dynamic_cast<TLight*>(table->find_component(lamps[i])) : nullptr;
        const int before=table->CurScore;
        ok &= roll->RenderSprite->Bmp!=nullptr && !roll->RolloverFlag;
        ball->EdgeCollisionCount=0;
        roll->Collision(ball,&position,&direction,0,findEdge(roll,600));
        ok &= roll->RolloverFlag && !roll->RenderSprite->Bmp && table->CurScore>before;
        if (lamps[i]) ok &= light && light->light_on();
        fprintf(report, "%s: score +%d, native press %d, associated lamp %d.\n",
                names[i],table->CurScore-before,int(roll->RolloverFlag),light?int(light->light_on()):-1);
        if (light) light->Message(MessageCode::TLightResetAndTurnOff,0);
    }
    // The substrate, light state and game clock are identical in these two
    // captures. Only the metal leaves travel through their respective slots.
    const SDL_Rect crops[]={{146,44,68,30},{36,279,69,37},{255,277,52,40},{45,174,59,36}};
    const char* areas[]={"top","left","right","ramp"};
    uint32_t down[4]{},up[4]{};
    render::update();
    for (int j=0;j<4;++j)
        ok &= visualChecks::capture(prefix+"detector6-"+areas[j]+"-down.bmp",crops[j],down[j]);
    for (auto roll : rolls)
    {
        const int before=table->CurScore;
        ball->EdgeCollisionCount=0;
        roll->Collision(ball,&position,&direction,0,findEdge(roll,603));
        ok &= !roll->RolloverFlag && roll->RenderSprite->Bmp && !roll->ActiveFlag && table->CurScore==before;
    }
    render::update();
    for (int j=0;j<4;++j)
    {
        ok &= visualChecks::capture(prefix+"detector6-"+areas[j]+"-up.bmp",crops[j],up[j]);
        ok &= up[j]!=down[j];
    }
    // Eleven releases at the same simulated instant are an artificial load.
    // timer::check normally pops two due callbacks per tick. Let the real
    // scheduler deliver the entire queue after the native 100 ms debounce.
    auto ready=[&]() {
        return std::all_of(rolls.begin(),rolls.end(),[](TRollover* r){return r->ActiveFlag && !r->RolloverFlag;});
    };
    int ticks=0;
    while (!ready() && ticks<60) { pb::frame(1000.f/120.f); ++ticks; }
    fprintf(report,"11 lane switches: input/render %s, release queue after %d ticks %s.\n",
            ok?"OK":"FAIL",ticks,ready()?"OK":"FAIL");
    ok &= ready();
    ball->EdgeCollisionCount=0;
    render::EnhancedGraphics=oldGraphics;
    return ok;
}
}
