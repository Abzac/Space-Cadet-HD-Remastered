#pragma once

// Stock upstream and the remaster compile this exact workload. Call just after
// pb::replay_level(false) in a fresh process, before advancing the first frame.
#include "pch.h"
#include "options.h"
#include "pb.h"
#include "TBall.h"
#include "TFlipper.h"
#include "TLight.h"
#include "TPinballTable.h"

inline void writeGameTrace(FILE* out)
{
    auto table = pb::MainTable;
    std::vector<TLight*> lights;
    for (auto component : table->ComponentList)
        if (auto lamp = dynamic_cast<TLight*>(component))
            lights.push_back(lamp);
    std::srand(70831);
    // The second New Game during the opening light show is the stock shortcut
    // to the first ball. No wall-clock or sound-duration dependence afterwards.
    pb::replay_level(false);
    auto input = [](GameBindings binding, bool pressed) {
        auto key = options::Options.Key[static_cast<int>(binding)].Inputs[0];
        if (pressed) pb::InputDown(key);
        else pb::InputUp(key);
    };
    fprintf(out, "frame,ball,remaining,score,x,y,z,vx,vy,speed,collision_mask,left,right,lamp_hash\n");
    auto ball = table->BallList.front();
    for (int i = 0; i < 2400; ++i)
    {
        if (i % 600 == 150) input(GameBindings::Plunger, true);
        if (i % 600 == 300) input(GameBindings::Plunger, false);
        if (i > 330 && i % 103 == 0) input(GameBindings::LeftFlipper, true);
        if (i > 330 && i % 103 == 24) input(GameBindings::LeftFlipper, false);
        if (i > 330 && i % 131 == 0) input(GameBindings::RightFlipper, true);
        if (i > 330 && i % 131 == 22) input(GameBindings::RightFlipper, false);
        pb::frame(1000.0f / 120);
        uint32_t lampHash = 2166136261u;
        for (const auto lamp : lights)
        {
            const uint32_t state = (lamp->LightOnFlag ? 1u : 0u)
                 | (lamp->FlasherOnFlag ? 2u : 0u)
                 | (static_cast<uint32_t>(lamp->PreviousBitmap + 1) << 2);
            lampHash = (lampHash ^ state) * 16777619u;
        }
        fprintf(out, "%d,%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%d,%d,%08x\n",
                i, ball->ActiveFlag, table->BallCount, table->CurScore, ball->Position.X,
                ball->Position.Y, ball->Position.Z, ball->Direction.X, ball->Direction.Y,
                ball->Speed, ball->CollisionMask, table->FlipperL->BmpIndex,
                table->FlipperR->BmpIndex, lampHash);
        if (pb::game_mode != GameModes::InGame || table->EndGameTimeoutTimer)
            break;
    }
}
