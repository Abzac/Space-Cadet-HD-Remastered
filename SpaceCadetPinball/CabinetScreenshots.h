#pragma once

// Record the release gallery from normal play on a private verification profile.
// The only simulated actions are launching the ball and pressing the two flippers.
namespace cabinetScreenshots
{
template <class Capture>
bool take(FILE* log, Capture capture)
{
    const char* video = SDL_GetCurrentVideoDriver();
    if (!video || strcmp(video, "dummy")) return false;
    SDL_SetWindowSize(winmain::MainWindow, 1800, 1248);
    fullscrn::window_size_changed();
    GameMenu::SetLanguage("en");
    auto table = pb::MainTable;
    auto ball = table->BallList.front();
    auto press = [&](GameBindings binding, bool down) {
        const auto key = options::Options.Key[static_cast<int>(binding)].Inputs[0];
        if (down) pb::InputDown(key); else pb::InputUp(key);
    };
    const char* pilots[]{"LUNA", "ION", "NOVA", "VEGA", "SOL"};
    int field = 0, sparks = 0;
    bool good = true;
    std::vector<high_score_struct> scores;
    for (int round = 0; round < 5; ++round)
    {
        std::srand(71903 + round);
        table->Message(MessageCode::Reset, 0);
        pb::replay_level(false);
        if (table->LightShowTimer) pb::replay_level(false);
        auto advance = [](int ticks) { while (ticks--) pb::frame(1000.f / 120.f); };
        advance(540);
        press(GameBindings::Plunger, true);
        advance(160 + round*31);
        press(GameBindings::Plunger, false);
        int lastField = -1000, lastSpark = -1000;
        bool deployed = false;
        for (int tick = 0; tick < 90*120; ++tick)
        {
            if (tick % 103 == 0) press(GameBindings::LeftFlipper, true);
            if (tick % 103 == 24) press(GameBindings::LeftFlipper, false);
            if (tick % 131 == 0) press(GameBindings::RightFlipper, true);
            if (tick % 131 == 22) press(GameBindings::RightFlipper, false);
            advance(1);
            deployed |= control::HasDeployedBall();
            if (deployed && !ball->ActiveFlag) break;
            if (!deployed || !ball->ActiveFlag || !ball->RenderSprite->Bmp || !table->CurScore) continue;
            const auto p = proj::xform_to_2d(ball->Position);
            const bool openField = p.X > 52 && p.X < 301 && p.Y > 64 && p.Y < 389;
            const bool electrical = CabinetEffects::FlashStrength(0,pb::time_now) > .48f ||
                                    CabinetEffects::FlashStrength(1,pb::time_now) > .48f;
            std::string name;
            if (electrical && tick-lastSpark>40 && sparks<10) {
                name = "rebound-"+std::to_string(++sparks);
                lastSpark=tick;
            }
            else if (openField && tick-lastField>140 && field < (round+1)*3) {
                name = "table-"+std::to_string(++field);
                lastField=tick;
            }
            if (!name.empty()) {
                good &= capture(name);
                fprintf(log, "%s at tick %d, player score %d, projected ball (%d, %d)\n",
                        name.c_str(),tick,table->CurScore,p.X,p.Y);
            }
        }
        if (table->CurScore>0) {
            high_score_struct entry{};
            SDL_strlcpy(entry.Name,pilots[round],sizeof(entry.Name));
            entry.Score=table->CurScore;
            scores.push_back(entry);
        }
        press(GameBindings::LeftFlipper, false);
        press(GameBindings::RightFlipper, false);
    }
    std::sort(scores.begin(),scores.end(),[](const high_score_struct& a, const high_score_struct& b) {
        return a.Score>b.Score;
    });
    high_score::erase_records(false);
    std::copy(scores.begin(),scores.end(),high_score::highscore_table);
    high_score::write();
    GameMenu::Toggle();
    capture("");
    good &= capture("menu");
    GameMenu::Scores();
    capture("");
    good &= capture("high-scores");
    return good && field>0 && sparks>0 && scores.size()>=2;
}
}
