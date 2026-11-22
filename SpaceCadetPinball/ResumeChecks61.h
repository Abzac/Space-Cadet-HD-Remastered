#pragma once

#include "GameMenu.h"
#include "InputChecks.h"
#include "TFlipperEdge.h"
#include "TTextBox.h"

namespace resumeChecks61
{
template<class Capture>
bool run(FILE* report, const std::string& prefix, Capture capture)
{
    bool ok = true;
    auto check = [&](bool passed, const char* name) {
        fprintf(report, "%s  %s\n", passed ? "PASS" : "FAIL", name);
        ok &= passed;
    };
    auto frame = []() {
        if (winmain::CanAdvanceGame()) pb::frame(1000.f / 120.f);
    };
    auto blocked = []() { return !winmain::CanAdvanceGame() && winmain::ResumeStage() == -1; };
    using inputChecks::windowEvent;
    windowEvent(SDL_WINDOWEVENT_FOCUS_GAINED);
    inputChecks::finishResumeCountdown();
    auto table = pb::MainTable;
    for (auto bat : table->FlipperList) bat->Message(MessageCode::TFlipperRetract, 0);
    for (int i = 0; i < 24; ++i) frame();
    auto plunger = table->Plunger;
    const auto launchKey = options::Options.Key[static_cast<int>(GameBindings::Plunger)].Inputs[0].Value;
    inputChecks::key(launchKey, true);
    for (int i = 0; i < 20; ++i) frame();
    const bool charged = plunger->PullbackStartedFlag && plunger->Boost > 1.f;
    auto ball = table->BallList.front();
    ball->Speed = 8.f;
    ball->Direction = {0.f, -1.f, 0.f};
    const auto initialPosition = ball->Position;
    const int initialTicks = pb::time_ticks;
    windowEvent(SDL_WINDOWEVENT_FOCUS_LOST);
    inputChecks::key(launchKey, false);
    auto idlePlunger = [&]() { return !plunger->PullbackStartedFlag && plunger->Threshold > 1e8f && plunger->Boost == 0.f; };
    check(charged && idlePlunger(), "focus loss cancels a charged digital plunger before its off-window key release");
    inputChecks::tap(SDLK_ESCAPE);
    frame();
    check(blocked() && !GameMenu::IsOpen(), "unfocused game freezes and ignores a background menu key");
    windowEvent(SDL_WINDOWEVENT_FOCUS_GAINED);
    winmain::UpdateResumeCountdown(SDL_GetTicks64() + 1600);
    check(winmain::ResumeStage() == 1 && !winmain::CanAdvanceGame(), "focus starts a countdown that still holds the ball at one");
    for (auto interrupted : {SDL_WINDOWEVENT_HIDDEN, SDL_WINDOWEVENT_MINIMIZED})
    {
        // Cover interruption of both the final word and the subsequent empty field.
        winmain::UpdateResumeCountdown(SDL_GetTicks64() + (interrupted == SDL_WINDOWEVENT_HIDDEN ? 2200 : 2600));
        windowEvent(interrupted);
        windowEvent(SDL_WINDOWEVENT_SHOWN);
        windowEvent(SDL_WINDOWEVENT_RESTORED);
        windowEvent(SDL_WINDOWEVENT_TAKE_FOCUS);
        winmain::UpdateResumeCountdown(SDL_GetTicks64() + 6000);
        frame();
        check(blocked(), interrupted == SDL_WINDOWEVENT_HIDDEN ?
              "shown window without input focus stays frozen" : "restored window without input focus stays frozen");
        windowEvent(SDL_WINDOWEVENT_FOCUS_GAINED);
        check(winmain::ResumeStage() == 3 && !winmain::CanAdvanceGame(), "interrupted countdown restarts at three on focus");
    }

    const auto press = options::Options.Key[static_cast<int>(GameBindings::LeftFlipper)].Inputs[0].Value;
    const auto release = options::Options.Key[static_cast<int>(GameBindings::RightFlipper)].Inputs[0].Value;
    inputChecks::key(press, true);
    inputChecks::tap(release);
    const std::string language = options::Options.Language.V;
    const auto start = SDL_GetTicks64();
    uint32_t hashes[2][4]{};
    bool images = true, frozen = true;
    for (int stage = 3; stage >= 0; --stage)
    {
        winmain::UpdateResumeCountdown(start + (3-stage)*700 + 100);
        images &= winmain::ResumeStage() == stage;
        for (int locale = 0; locale < 2; ++locale)
        {
            const char* code = locale ? "ru" : "en";
            GameMenu::SetLanguage(code);
            auto snap = capture(prefix + "resume-" + code + "-" + (stage ? std::to_string(stage) : "go") + ".bmp");
            hashes[locale][stage] = snap.centerHash;
            images &= snap.saved && snap.countdownLensPixels > 30;
        }
        frame();
        frozen &= !winmain::CanAdvanceGame() && pb::time_ticks == initialTicks &&
                  ball->Position.X == initialPosition.X && ball->Position.Y == initialPosition.Y &&
                  table->FlipperL->FlipperEdge->CurrentAngle == 0.f && table->FlipperR->FlipperEdge->CurrentAngle == 0.f;
        if (stage == 1)
        {
            winmain::UpdateResumeCountdown(start + 2020);
            frame();
            frozen &= !winmain::CanAdvanceGame() && winmain::ResumeStage() == 1 && pb::time_ticks == initialTicks;
        }
    }
    GameMenu::SetLanguage(language.c_str());
    check(frozen, "table clock, moving ball and both flippers stay still through all three digits and GO");
    check(images && hashes[0][3] != hashes[0][2] && hashes[0][2] != hashes[0][1] && hashes[0][1] != hashes[0][3] &&
          hashes[0][0] != hashes[1][0], "three distinct LED digits and two localized GO messages render on the centered overlay");
    winmain::UpdateResumeCountdown(start + 2500);
    const bool clear = winmain::ResumeStage() < 0 && winmain::ResumePending() && !winmain::CanAdvanceGame();
    capture(prefix + "resume-clear.bmp");
    // Simulate a stalled presentation: the player has seen no clean frame yet.
    winmain::UpdateResumeCountdown(start + 3950);
    const bool waited = !winmain::CanAdvanceGame();
    winmain::ResumeFramePresented(start + 4000);
    winmain::UpdateResumeCountdown(start + 4299);
    frame();
    check(clear && waited && !winmain::CanAdvanceGame() && pb::time_ticks == initialTicks &&
          ball->Position.X == initialPosition.X && ball->Position.Y == initialPosition.Y,
          "GO clears completely; 299 ms after the first clean presentation the ball and table clock are still frozen");
    winmain::UpdateResumeCountdown(start + 4300);
    frame();
    check(winmain::CanAdvanceGame() && pb::time_ticks > initialTicks &&
          (ball->Position.X != initialPosition.X || ball->Position.Y != initialPosition.Y),
          "ball and table clock resume exactly 300 ms after presentation of an unobstructed field");
    check(table->FlipperL->FlipperEdge->CurrentAngle != 0.f && table->FlipperR->FlipperEdge->CurrentAngle == 0.f,
          "only the flipper still held when play resumes actuates");
    check(idlePlunger(), "refocusing and resuming play never fires a canceled digital plunger");
    inputChecks::key(press, false);
    check(!winmain::ResumePending() && winmain::CanAdvanceGame(), "countdown is fully dismissed after restart");

    inputChecks::tap(SDLK_F3);
    bool pauseLabels=true;
    for (const char* locale : {"ru","en"})
    {
        GameMenu::SetLanguage(locale);
        auto message=pb::InfoTextBox->CurrentMessage;
        const char* expected=strcmp(locale,"ru")==0 ? "Пауза\nF3 / Пробел" : "Paused\nF3 / Space";
        pauseLabels &= message && message->Text && strcmp(message->Text,expected)==0;
    }
    check(pauseLabels,"F3 pause identifies both resume keys on the Russian and English cabinet");
    GameMenu::SetLanguage(language.c_str());
    inputChecks::tap(SDLK_ESCAPE);
    const bool menuOverPause = GameMenu::IsOpen() && blocked();
    inputChecks::tap(SDLK_ESCAPE);
    check(menuOverPause && !GameMenu::IsOpen() && blocked(), "Escape in and out of the menu preserves an explicit F3 pause");
    windowEvent(SDL_WINDOWEVENT_FOCUS_LOST);
    windowEvent(SDL_WINDOWEVENT_FOCUS_GAINED);
    winmain::UpdateResumeCountdown(SDL_GetTicks64() + 6000);
    check(blocked(), "manual F3 pause survives leaving and refocusing the window");
    inputChecks::key(SDLK_SPACE,true);
    check(winmain::ResumeStage() == 3 && !winmain::CanAdvanceGame() && idlePlunger(),
          "Space resumes an explicit F3 pause through the countdown with no plunger charge");
    inputChecks::finishResumeCountdown();
    inputChecks::key(SDLK_SPACE,false);
    check(idlePlunger(),"releasing the resume Space key after the countdown does not fire the plunger");
    UsingSdlHint background(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    inputChecks::VirtualPad pad(0x054c, 0x0ce6, "Virtual DualSense resume");
    if (pad.joystick)
    {
        pad.button(SDL_CONTROLLER_BUTTON_START, true);
        pad.button(SDL_CONTROLLER_BUTTON_START, false);
        const bool menu = GameMenu::IsOpen() && blocked();
        pad.button(SDL_CONTROLLER_BUTTON_START, true);
        pad.button(SDL_CONTROLLER_BUTTON_START, false);
        check(menu && !GameMenu::IsOpen() && winmain::ResumeStage() == 3 && !winmain::CanAdvanceGame(),
              "controller Start opens the menu and starts the countdown on return");
        inputChecks::finishResumeCountdown();
    }
    else check(false, "virtual controller for Start resume check opened");
    auto& countdown = options::Options.ResumeCountdown;
    countdown = false;
    inputChecks::tap(SDLK_F3);
    inputChecks::tap(SDLK_SPACE);
    const bool manualOff = winmain::CanAdvanceGame() && !winmain::ResumePending() && idlePlunger();
    inputChecks::tap(SDLK_ESCAPE);
    inputChecks::tap(SDLK_ESCAPE);
    const bool menuOff = winmain::CanAdvanceGame() && !winmain::ResumePending();
    windowEvent(SDL_WINDOWEVENT_FOCUS_LOST);
    const bool frozenInBackground = blocked();
    windowEvent(SDL_WINDOWEVENT_FOCUS_GAINED);
    check(manualOff && menuOff && frozenInBackground && winmain::CanAdvanceGame() && !winmain::ResumePending(),
          "disabled countdown immediately restores play via F3, Escape and refocus; focus loss still pauses");
    countdown = true;
    return ok;
}
}
