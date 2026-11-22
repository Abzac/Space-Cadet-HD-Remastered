#pragma once
#include "InputChecks.h"
#include "StickInput.h"
#include "render.h"

// Run in a fresh verification process. The same probe also builds against the
// archived HD 6 sources, so launch mechanics can be compared independently.
inline bool writePlungerTrace61(FILE* out)
{
    auto table=pb::MainTable;
    auto plunger=table->Plunger;
    auto ball=table->BallList.front();
    constexpr int stick=17;
    constexpr float dt=1000.f/120;
    const auto press=options::Options.Key[static_cast<int>(GameBindings::Plunger)].Inputs[0];
    render::EnhancedGraphics=true;
    const int holds[]={13,64,155,272,440,0,0,0,0};
    const int pulls[]={0,0,0,0,0,9700,16384,24500,32767};
    bool ok=true;
    fprintf(out,"trial,tick,boost,threshold,x,y,z,dirX,dirY,speed,active,score\n");
    for (int trial=0;trial<9;++trial)
    {
        std::srand(71903+trial);
        table->Message(MessageCode::Reset,0);
        pb::replay_level(false);
        if (table->LightShowTimer) pb::replay_level(false);
        for (int i=0;i<540;++i) pb::frame(dt);
        const int held=pulls[trial]?48:holds[trial];
        const auto initial=ball->Position;
        float maxSpeed=0.f;
        for (int tick=0;tick<held+240;++tick)
        {
            if (pulls[trial])
            {
                if (tick==0) StickInput::Axis(stick,SDL_CONTROLLER_AXIS_RIGHTY,pulls[trial],true);
                if (tick==24) StickInput::Axis(stick,SDL_CONTROLLER_AXIS_RIGHTY,pulls[trial]*3/4,true);
                if (tick==held) StickInput::Axis(stick,SDL_CONTROLLER_AXIS_RIGHTY,0,true);
            }
            else if (tick==0) pb::InputDown(press);
            else if (tick==held) pb::InputUp(press);
            pb::frame(dt);
            if ((tick%2)==0)
            {
                render::PresentVScreen(pb::time_now);
                SDL_RenderPresent(winmain::Renderer);
            }
            if (tick>=held) maxSpeed=std::max(maxSpeed,ball->Speed);
            fprintf(out,"%d,%d,%a,%a,%a,%a,%a,%a,%a,%a,%d,%d\n",trial,tick,plunger->Boost,plunger->Threshold,
                    ball->Position.X,ball->Position.Y,ball->Position.Z,ball->Direction.X,ball->Direction.Y,
                    ball->Speed,int(ball->ActiveFlag),table->CurScore);
        }
        // Include weak shots that roll back down the lane. Strong shots should
        // always leave the plunger, so a missing input cannot yield a false match.
        if (trial==4 || trial==8)
            ok &= maxSpeed>20.f && (std::abs(ball->Position.Y-initial.Y)>ball->Radius || table->CurScore>0);
        StickInput::Remove(stick);
    }
    return ok && render::HdAssetCount==318 && !ferror(out);
}
