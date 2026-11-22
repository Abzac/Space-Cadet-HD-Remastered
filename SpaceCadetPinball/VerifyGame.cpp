#include "pch.h"
#include "VerifyGame.h"
#include "VerifyTrace.h"
#include "PlungerTrace61.h"
#include "FairChecks.h"
#include "RuleChecks.h"
#include "InputChecks.h"
#include "GameMenu.h"
#include "HdrOutput.h"
#include "Sound.h"
#include "fullscrn.h"
#include "midi.h"
#include "render.h"
#include "winmain.h"
#include "VisualChecks.h"
#include "CabinetArtChecks.h"
#include "InsertChecks4.h"
#include "FieldArtChecks5.h"
#include "DetectorChecks6.h"
#include "MetalChecks4.h"
#include "EffectsChecks4.h"
#include "DomeVeinChecks632.h"
#include "StarStripChecks63.h"
#include "StarStripHdrChecks632.h"
#include "BallSpinChecks70.h"
#include "SilhouetteChecks701.h"
#include "PhysicsStepChecks70.h"
#include "AudioChecks70.h"
#include "PlanetChecks61.h"
#include "MatrixChecks61.h"
#include "SpringChecks61.h"
#include "ResumeChecks61.h"
#include "ResumeBallChecks62.h"
#include "MatrixLayoutChecks62.h"
#include "SpinnerRestChecks62.h"
#include "PausedArtChecks63.h"
#include "TTextBox.h"
#include "score.h"
#include "MaximizeChecks631.h"
#include "CabinetScreenshots.h"

namespace
{
    void step(int frames)
    {
        for (int i = 0; i < frames; ++i)
            pb::frame(1000.0f / 120);
    }

    void freshBall()
    {
        pb::replay_level(false);
        if (pb::MainTable->LightShowTimer)
            pb::replay_level(false);
        step(180);
    }

    struct Snapshot
    {
        uint32_t boardHash = 2166136261u;
        uint32_t centerHash = 2166136261u;
        int countdownLensPixels = 0;
        int litPixels = 0;
        int hintPixels = 0;
        float displayGreenOverBlue[3]{};
        int width = 0, height = 0;
        bool saved = false;
    };

    Snapshot capture(const std::string& fileName, void (*drawUi)(), bool probeHdr, bool diagnosticHdr = false,
                     const ImVec2* mousePosition = nullptr, bool mouseDown = false)
    {
        SDL_PumpEvents();
        fullscrn::window_size_changed();
        ImGui_ImplSDL2_NewFrame();
        ImGui_Render_NewFrame();
        if (mousePosition)
        {
            ImGui::GetIO().AddMousePosEvent(mousePosition->x,mousePosition->y);
            ImGui::GetIO().AddMouseButtonEvent(0,mouseDown);
        }
        ImGui::NewFrame();
        drawUi();
        auto renderer = winmain::Renderer;
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        render::PresentVScreen(pb::time_now);
        ImGui::Render();
        ImGui_Render_RenderDrawData(ImGui::GetDrawData());

        Snapshot result;
        if (SDL_GetRendererOutputSize(renderer, &result.width, &result.height) < 0)
            return result;
        auto surface = SDL_CreateRGBSurfaceWithFormat(0, result.width, result.height, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!surface) return result;
        if (SDL_RenderReadPixels(renderer, nullptr, surface->format->format, surface->pixels, surface->pitch) == 0)
        {
            result.saved = fileName.empty() || SDL_SaveBMP(surface, fileName.c_str()) == 0;
            auto info=pb::InfoTextBox, mission=pb::MissTextBox;
            auto score=pb::MainTable->CurScoreStruct;
            const SDL_Rect bounds{0,0,surface->w,surface->h};
            const SDL_Rect apertures[]={{score->OffsetX,score->OffsetY,score->Width,score->Height},
                {info->OffsetX,info->OffsetY,info->Width,info->Height},
                {mission->OffsetX,mission->OffsetY,mission->Width,mission->Height},
                {mission->OffsetX,render::vscreen->Height-9,mission->Width,9}};
            for (int i=0;i<4;++i)
            {
                auto box=fullscrn::GetScreenRectFromPinballRect(apertures[i]);
                if (!SDL_IntersectRect(&bounds,&box,&box)) continue;
                unsigned green=0,blue=0;
                for (int y=box.y;y<box.y+box.h;++y)
                {
                    auto row=reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(surface->pixels)+y*surface->pitch);
                    for (int x=box.x;x<box.x+box.w;++x)
                    {
                        const Uint32 rgb=row[x];
                        const int r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
                        if (i==3) { if (r>28 && g>28 && b>28) ++result.hintPixels; }
                        else if (b>40 && b>1.14*g && b>1.24*r) { green+=g; blue+=b; }
                    }
                }
                if (i<3 && blue) result.displayGreenOverBlue[i]=float(green)/blue;
            }
            // Ignore window chrome and scoreboard text in comparisons.
            for (int y = surface->h / 8; y < surface->h * 7 / 8; y += 2)
            {
                const auto row = reinterpret_cast<const uint32_t*>(
                    static_cast<const Uint8*>(surface->pixels) + y * surface->pitch);
                for (int x = surface->w / 8; x < surface->w / 2; x += 2)
                {
                    const uint32_t color = row[x] & 0x00ffffffu;
                    result.boardHash = (result.boardHash ^ color) * 16777619u;
                    if (color != 0) ++result.litPixels;
                }
            }
            if (winmain::ResumeStage() >= 0)
            {
                for (int y = surface->h * 38 / 100; y < surface->h * 62 / 100; y += 2)
                {
                    const auto row = reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(surface->pixels) + y * surface->pitch);
                    for (int x = surface->w / 8; x < surface->w * 7 / 8; x += 2)
                    {
                        // Opaque cores of the LED lenses in the centered overlay.
                        const bool core = (row[x] & 0xffffffu) == 0xd0ccffu;
                        result.centerHash = (result.centerHash ^ static_cast<unsigned>(core)) * 16777619u;
                        result.countdownLensPixels += core;
                    }
                }
            }
        }
        SDL_FreeSurface(surface);
        if (!probeHdr || !HdrOutput::Present(winmain::MainWindow, renderer, true, true, diagnosticHdr))
            SDL_RenderPresent(renderer);
        return result;
    }
}

bool VerifyGame(const std::string& directory, bool mixOpened, void (*drawUi)())
{
    const auto prefix = directory + PathSeparator;
    const char* scoreTest = SDL_getenv("SPACECADET_SCORE_TEST");
    FILE* report = fopenu((prefix + (scoreTest ? std::string("profile-")+scoreTest+".txt" : "report.txt")).c_str(), "wb");
    if (!report) return false;
    bool passed = true;
    auto check = [&](const char* name, bool ok) {
        fprintf(report, "%s  %s\n", ok ? "PASS" : "FAIL", name);
        fflush(report);
        passed &= ok;
    };
    fprintf(report, "Space Cadet runtime verification. Mode: %s\n", pb::FairMode ? "meditative" : "classic");
    const auto audioDriver=SDL_GetCurrentAudioDriver();
    fprintf(report,"SDL audio driver: %s\n",audioDriver ? audioDriver : "none");
    inputChecks::windowEvent(SDL_WINDOWEVENT_FOCUS_GAINED);
    inputChecks::finishResumeCountdown();
    check("cold start enters a live game and its native lamp show with the entire table visible",
          !GameMenu::IsOpen() && !winmain::single_step && pb::game_mode==GameModes::InGame && pb::MainTable->LightShowTimer);
    if (SDL_GetHintBoolean("SPACECADET_VERIFY_GALLERY", SDL_FALSE))
    {
        check("English release gallery from single-player rounds and their scores", cabinetScreenshots::take(report,
            [&](const std::string& name) { return capture(name.empty() ? "" : prefix+name+".bmp", drawUi, false).saved; }));
        return fclose(report)==0 && passed;
    }
    if (SDL_GetHintBoolean("SPACECADET_VERIFY_EDGES",SDL_FALSE))
    {
        check("HD silhouette coverage at different window sizes",silhouetteChecks701::run(report,prefix));
        return fclose(report)==0 && passed;
    }
    if (SDL_GetHintBoolean("SPACECADET_VERIFY_AUDIO",SDL_FALSE))
    {
        check("sound output and simultaneous voices",audioChecks70::run(report,prefix,mixOpened));
        return fclose(report)==0 && passed;
    }
    if (SDL_GetHintBoolean("SPACECADET_VERIFY_MUSIC",SDL_FALSE))
    {
        check("packaged instrument banks are available", mixOpened && midi::HasInstrumentChoice());
        check("fresh profile uses GeneralUser GS at 64 and table sounds at 128, music initially off",
              options::Options.MusicBank.V == 1 && !options::Options.Music && options::Options.Sounds &&
              options::Options.MusicVolume.V == 64 && options::Options.SoundVolume.V == 128 && Mix_VolumeMusic(-1) == 64);
        midi::music_stop();
        check("bank is silent until enabled", !Mix_PlayingMusic());
        midi::play_track(MidiTracks::Track1, true);
        midi::music_play();
        const auto duration = Mix_MusicDuration(nullptr);
        const bool ogg = Mix_PlayingMusic() && Mix_GetMusicType(nullptr) == MUS_OGG;
        const bool nearEnd = ogg && duration > 58.0 && duration < 59.0 && Mix_SetMusicPosition(duration - .2) == 0;
        SDL_Delay(650);
        const auto loopPosition = Mix_GetMusicPosition(nullptr);
        fprintf(report, "GeneralUser loop duration %.4f s, playback position after crossing loop %.4f s.\n", duration, loopPosition);
        check("Vorbis theme plays and loops at its musical boundary", nearEnd && Mix_PlayingMusic() && loopPosition >= 0 && loopPosition < 2.);
        for (int bank = 2; bank < midi::BankCount; ++bank)
        {
            check("packaged sound bank is selectable", midi::BankAvailable(bank));
            Mix_SetMusicPosition(12.0);
            midi::SelectBank(bank);
            const double position = Mix_GetMusicPosition(nullptr);
            check("live switch retains track, volume and loop position", midi::SelectedBank() == bank &&
                  Mix_GetMusicType(nullptr) == MUS_OGG && Mix_PlayingMusic() && position >= 11.8 &&
                  position < 13 && Mix_VolumeMusic(-1) == 64 && Mix_MusicDuration(nullptr) > 58.0);
        }
        Mix_SetMusicPosition(23.0);
        midi::music_stop();
        midi::SelectBank(2);
        check("changing banks while paused stays silent", !Mix_PlayingMusic());
        midi::music_play();
        check("resuming with a different recording keeps the phrase position",
              midi::SelectedBank() == 2 && Mix_GetMusicPosition(nullptr) >= 22.8 &&
              Mix_GetMusicPosition(nullptr) < 24.0);
        midi::SelectBank(0);
        check("original MIDI is playable on the same logical track", Mix_PlayingMusic() && Mix_GetMusicType(nullptr) == MUS_MID && midi::get_active_track() == MidiTracks::Track1);
        midi::music_stop();
        midi::SelectBank(1);
        midi::music_play();
        check("pause and resume retain selected bank", Mix_PlayingMusic() && Mix_GetMusicType(nullptr) == MUS_OGG);
        midi::music_stop();
        return fclose(report)==0 && passed;
    }
    if (SDL_GetHintBoolean("SPACECADET_VERIFY_PHYSICS",SDL_FALSE))
    {
        check("collision times and mirrored flippers",physicsStepChecks70::run(report));
        return fclose(report)==0 && passed;
    }
    if (SDL_GetHintBoolean("SPACECADET_VERIFY_SPIN",SDL_FALSE))
    {
        check("solid sphere spin, contact friction and moving steel grain",ballSpinChecks70::run(report,prefix));
        return fclose(report)==0 && passed;
    }
    if (SDL_GetHintBoolean("SPACECADET_VERIFY_DOME_HDR",SDL_FALSE) ||
        SDL_GetHintBoolean("SPACECADET_VERIFY_DOME_SDR",SDL_FALSE))
    {
        const bool hdr=SDL_GetHintBoolean("SPACECADET_VERIFY_DOME_HDR",SDL_FALSE);
        check("central glass vein readback",hdr ? domeVeinChecks632::hdr(report) :
              domeVeinChecks632::run(report,prefix));
        if (hdr) check("alternating star strip peaks", starStripHdrChecks632::run(report));
        return fclose(report)==0 && passed;
    }
    if (SDL_GetHintBoolean("SPACECADET_VERIFY_LAUNCH",SDL_FALSE))
    {
        auto csv=fopenu((prefix+"launch.csv").c_str(),"wb");
        bool ok=csv && writePlungerTrace61(csv);
        if (csv) ok &= fclose(csv)==0;
        check("nine repeatable digital and analog plunger trajectories",ok);
        return fclose(report)==0 && passed;
    }
    if (scoreTest)
    {
        GameMenu::SetLanguage("ru");
        winmain::SetWindowSize(1200);
        if (strcmp(scoreTest,"inspect")==0)
        {
            for (bool mode : {false,true})
            {
                high_score_struct rows[5];
                high_score::view(mode,rows);
                fprintf(report,"%s saved scores:",mode ? "Meditative":"Classic");
                for (const auto& row : rows) if (row.Score>0) fprintf(report," %d",row.Score);
                fprintf(report,"\n");
            }
        }
        else if (strcmp(scoreTest,"settings-write")==0)
        {
            check("a new profile enables the resume countdown by default", options::Options.ResumeCountdown.V);
            check("new profiles select GeneralUser GS at half volume", options::Options.MusicBank.V == 1 && options::Options.MusicVolume.V == 64);
            inputChecks::tap(SDLK_F3);
            inputChecks::tap(SDLK_ESCAPE);
            auto menuFrame = [&]() { capture("",drawUi,false); };
            auto menuKey = [&](SDL_Keycode key) {
                inputChecks::key(key,true); menuFrame();
                inputChecks::key(key,false); menuFrame();
            };
            menuFrame(); menuFrame();
            menuKey(SDLK_DOWN);
            menuKey(SDLK_SPACE);
            menuFrame();
            auto settingsPanel = ImGui::FindWindowByName("##FlightMenu");
            const auto timerId = settingsPanel->GetID("Таймер после паузы");
            for (int i=0;i<12 && ImGui::GetCurrentContext()->NavId!=timerId;++i) menuKey(SDLK_DOWN);
            const bool focusedTimer = ImGui::GetCurrentContext()->NavId==timerId;
            if (focusedTimer) menuKey(SDLK_SPACE);
            check("Russian settings checkbox disables the countdown through keyboard navigation",
                  focusedTimer && !options::Options.ResumeCountdown.V && options::GetSetting("Resume countdown","")=="0");
            capture(prefix+"settings-ru.bmp",drawUi,false);
            GameMenu::SetLanguage("en");
            capture(prefix+"settings-en.bmp",drawUi,false);
            GameMenu::SetLanguage("ru");
            menuFrame();
            if (midi::HasInstrumentChoice())
            {
                auto focusDown = [&](const char* label) {
                    const auto target = settingsPanel->GetID(label);
                    for (int i=0; i<25 && ImGui::GetCurrentContext()->NavId!=target; ++i) menuKey(SDLK_DOWN);
                    return ImGui::GetCurrentContext()->NavId==target;
                };
                if (focusDown("Сглаживание контуров")) menuKey(SDLK_SPACE);
                check("keyboard toggles smooth HD edges off", !options::Options.SmoothSilhouettes);
                if (focusDown("Музыка")) menuKey(SDLK_SPACE);
                if (focusDown("##musicBank"))
                {
                    menuKey(SDLK_SPACE); menuFrame();
                    menuKey(SDLK_UP); menuKey(SDLK_SPACE);
                }
                check("instrument bank can be changed in the Russian settings with the keyboard",
                      options::Options.Music && options::Options.MusicBank.V == 0);
            }
            inputChecks::tap(SDLK_ESCAPE); inputChecks::tap(SDLK_ESCAPE);
            check("Space navigates the settings menu above an F3 pause without resuming the table",
                  !GameMenu::IsOpen() && winmain::single_step && !winmain::ResumePending());
            inputChecks::tap(SDLK_SPACE);
            check("the next Space resumes the F3 pause immediately with the timer off",winmain::CanAdvanceGame());
            inputChecks::finishResumeCountdown();
        }
        else if (strcmp(scoreTest,"settings-read")==0)
        {
            check("a fresh process restores the disabled countdown preference", !options::Options.ResumeCountdown.V);
            if (midi::HasInstrumentChoice())
                check("a fresh process restores the HD edge preference", !options::Options.SmoothSilhouettes);
            if (midi::HasInstrumentChoice())
                check("a fresh process restores the user's original MIDI selection", options::Options.MusicBank.V == 0);
        }
        else if (strcmp(scoreTest,"write")==0)
        {
            // Keep this isolated two-process fixture repeatable on a reused QA directory.
            high_score::erase_records(false);
            high_score::erase_records(true);
            freshBall();
            pb::MainTable->AddScore(246850);
            pb::end_game();
            auto pending = high_score::pending();
            check("completed classic run committed before opening the name editor", pending && high_score::highscore_table[0].Score==246850);
            capture("",drawUi,false); capture("",drawUi,false);
            ImGui::GetIO().AddInputCharactersUTF8("Орион");
            capture(prefix+"record.bmp",drawUi,false);
            inputChecks::key(SDLK_RETURN,true);
            capture("",drawUi,false);
            inputChecks::key(SDLK_RETURN,false);
            capture("",drawUi,false);
            check("pilot name submitted with Enter in the actual record panel", !high_score::pending() &&
                  strcmp(high_score::highscore_table[0].Name,"Орион")==0);
            if (GameMenu::IsOpen()) {GameMenu::Toggle(); if (GameMenu::IsOpen()) GameMenu::Toggle();}
            const auto window = winmain::MainWindow;
            const auto renderer = winmain::Renderer;
            options::Options.FairRules = true;
            winmain::new_game();
            check("new mode starts in the same window and renderer", pb::FairMode && winmain::MainWindow==window && winmain::Renderer==renderer);
            freshBall();
            pb::MainTable->AddScore(813700);
            pb::MainTable->Message(MessageCode::GameOver,0);
            const bool awaitingGameOver = pb::MainTable->EndGameTimeoutTimer != 0;
            SDL_Event closeGame{SDL_QUIT};
            winmain::event_handler(&closeGame);
            check("closing during the native game-over delay commits the meditative result with a default name", awaitingGameOver && high_score::pending() &&
                  high_score::highscore_table[0].Score==813700);
        }
        else
        {

            high_score_struct classic[5], meditative[5];
            high_score::view(false,classic); high_score::view(true,meditative);
            check("fresh process reloads the classic score and UTF-8 name",classic[0].Score==246850 && !strcmp(classic[0].Name,"Орион"));
            check("fresh process reloads the other score even if the last name prompt was closed with the game",
                  meditative[0].Score==813700 && *meditative[0].Name);
            GameMenu::Scores();
            capture("",drawUi,false); capture(prefix+"scores-classic.bmp",drawUi,false);
            const auto panel = ImGui::FindWindowByName("##FlightMenu");
            const auto nav = ImGui::WindowRectRelToAbs(panel,panel->NavRectRel[ImGuiNavLayer_Main]);
            const auto firstTab = nav.GetCenter();
            const auto secondTab = ImVec2(firstTab.x+nav.GetWidth()+ImGui::GetStyle().ItemSpacing.x, firstTab.y);
            fprintf(report,"Tab centers %.1f / %.1f at y %.1f; navigation rectangle %.1f x %.1f.\n",
                    firstTab.x,secondTab.x,firstTab.y,nav.GetWidth(),nav.GetHeight());
            capture("",drawUi,false,false,&secondTab,false);
            capture("",drawUi,false,false,&secondTab,true);
            capture("",drawUi,false,false,&secondTab,false);
            const bool clickedMed = GameMenu::ViewingMeditativeScores();
            capture("",drawUi,false,false,&firstTab,false);
            capture("",drawUi,false,false,&firstTab,true);
            capture("",drawUi,false,false,&firstTab,false);
            check("mouse clicks select both visible tabs without changing game rules", clickedMed && !GameMenu::ViewingMeditativeScores() && !pb::FairMode);
            UsingSdlHint background(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
            inputChecks::VirtualPad dualsense(0x054c,0x0ce6,"Virtual DualSense");
            inputChecks::VirtualPad xbox(0x045e,0x02ea,"Virtual Xbox One");
            bool tabsWork = dualsense.joystick && xbox.joystick;
            if (tabsWork)
            {
                dualsense.button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,true);
                dualsense.button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,false);
                tabsWork = GameMenu::ViewingMeditativeScores() && !pb::FairMode;
                capture("",drawUi,false); capture(prefix+"scores-meditative.bmp",drawUi,false);
                xbox.button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,true);
                xbox.button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,false);
                tabsWork &= !GameMenu::ViewingMeditativeScores() && !pb::FairMode;
            }
            check("DualSense R1 and Xbox LB switch tabs without changing the game rules", tabsWork);
        }
        return fclose(report)==0 && passed;
    }
    check("new-profile defaults enable table sounds and mute music",
          options::Options.Sounds.DefaultValue && !options::Options.Music.DefaultValue);
    auto trace = fopenu((prefix + "trace.csv").c_str(), "wb");
    if (trace)
    {
        writeGameTrace(trace);
        const bool good = (ferror(trace) == 0);
        check("input / trajectory / score / lamp trace written", fclose(trace) == 0 && good);
    }
    else check("create trace.csv", false);

    pb::MainTable->Message(MessageCode::Reset,0);
    pb::replay_level(false);
    winmain::SetWindowSize(1200);
    step(20);
    auto intro = capture(prefix+"startup.bmp",drawUi,false);
    step(36);
    auto nextIntro = capture("",drawUi,false);
    check("native intro animates field pixels without an input event or menu", intro.saved && nextIntro.saved &&
          intro.boardHash!=nextIntro.boardHash && !GameMenu::IsOpen());
    check("shortcut legend visible below the instrument bezel before launch", intro.hintPixels>25);
    check("pink and violet printed outlines alternate across their full width", starStripChecks63::run(report,prefix));
    const auto introLanguage=options::Options.Language.V;
    inputChecks::tap(SDLK_F1);
    const bool gameHelp=GameMenu::IsHelpOpen() && winmain::single_step;
    GameMenu::SetLanguage("ru");
    capture("",drawUi,false); capture(prefix+"help-ru.bmp",drawUi,false);
    winmain::SetWindowSize(600);
    capture("",drawUi,false); capture(prefix+"help-ru-small.bmp",drawUi,false);
    GameMenu::SetLanguage("en");
    capture("",drawUi,false); capture(prefix+"help-en-small.bmp",drawUi,false);
    winmain::SetWindowSize(1200);
    GameMenu::SetLanguage(introLanguage.c_str());
    inputChecks::tap(SDLK_ESCAPE);
    check("Escape from F1 help resumes the prelaunch show immediately", gameHelp && !GameMenu::IsOpen() &&
          !winmain::ResumePending() && winmain::CanAdvanceGame());
    inputChecks::tap(SDLK_F3); inputChecks::tap(SDLK_F1); inputChecks::tap(SDLK_F1);
    check("closing F1 help restores a pre-existing F3 pause",winmain::single_step && !GameMenu::IsOpen());
    inputChecks::tap(SDLK_F3);
    inputChecks::finishResumeCountdown();
    capture("",drawUi,false);
    pb::mode_change(GameModes::GameOver);
    inputChecks::tap(SDLK_SPACE);
    check("Space starts a game from game over",pb::game_mode==GameModes::InGame);

    SDL_RendererInfo rendererInfo{};
    SDL_GetRendererInfo(winmain::Renderer, &rendererInfo);
    fprintf(report, "Renderer: %s; DAT: %s.\n", rendererInfo.name ? rendererInfo.name : "?", pb::DatFileName.c_str());
    if (mixOpened)
    {
        midi::music_play();
        const bool started = midi::play_track(MidiTracks::Track1, true);
        check("selected Space Cadet theme accepted for playback and mixer reports playing",
              started && Mix_PlayingMusic() && midi::get_active_track() == MidiTracks::Track1);
        auto wave = Sound::LoadWaveFile(pb::make_path_name("SOUND1.WAV"));
        int channel = wave ? Mix_PlayChannel(-1, wave, 0) : -1;
        if (channel < 0 && wave)
        {
            Mix_HaltChannel(0);
            channel = Mix_PlayChannel(0, wave, 0);
        }
        check("original SOUND1.WAV decoded and playing on a real mixer channel",
              wave && wave->alen > 0 && channel >= 0 && Mix_Playing(channel));
        if (channel >= 0) Mix_HaltChannel(channel);
        Sound::FreeSound(wave);
    }
    else fprintf(report, "SKIP  audio device unavailable or disabled; playback unverified\n");
    freshBall();
    auto ball = pb::MainTable->BallList.front();
    // The new ball initially rolls and bounces down the shooter lane. An early
    // release can miss the ball during the stock 25 ms plunger strike window.
    step(360);
    check("a fed ball gets the launch light rhythm",starStripChecks63::deployment(report,prefix,true));
    check("cabinet message and numeric lenses across three window sizes",
          matrixChecks61::awaiting(report,prefix,[&](const std::string& f,bool hdr){return capture(f,drawUi,hdr);}));
    const auto startY = ball->Position.Y;
    GameMenu::SetLanguage("en");
    capture(prefix+"awaiting-en.bmp",drawUi,false);
    GameMenu::SetLanguage("ru");
    capture(prefix+"awaiting-ru.bmp",drawUi,false);
    GameMenu::SetLanguage(introLanguage.c_str());
    const auto launchKey = options::Options.Key[static_cast<int>(GameBindings::Plunger)].Inputs[0];
    inputChecks::key(launchKey.Value,true);
    step(150);
    inputChecks::key(launchKey.Value,false);
    // Wait for the stock launcher to put the ball in the visible main playfield.
    bool travelled = false;
    bool struck = false;
    for (int i = 0; i < 1500; ++i)
    {
        step(1);
        if (!struck && ball->ActiveFlag && ball->Speed > 18.f)
        {
            struck = true;
            check("launch cue is off on the first fast tick after the plunger strikes",
                  starStripChecks63::deployment(report,prefix,false,"strike"));
        }
        const auto& rect = ball->RenderSprite->BmpRect;
        if (ball->ActiveFlag && ball->RenderSprite->Bmp && pb::MainTable->CurScore > 0 &&
            rect.XPosition > 55 && rect.XPosition < 265 && rect.YPosition > 100 &&
            rect.YPosition < 350 && std::abs(ball->Position.Y - startY) > ball->Radius * 3)
        {
            travelled = true;
            break;
        }
    }
    check("stock plunger launched the ball into the main playfield", travelled);
    check("launch impulse was observed",struck);
    check("deployment lights retire when the ball clears the launch lane",starStripChecks63::deployment(report,prefix,false));
    auto afterLaunch=capture("",drawUi,false);
    check("shortcut legend remains on the bezel strip during play", travelled && afterLaunch.hintPixels>25 &&
          afterLaunch.hintPixels==intro.hintPixels);
    fullscrn::set_screen_mode(0);
    winmain::SetWindowSize(600);
    int w1 = 0, h1 = 0;
    SDL_GetWindowSize(winmain::MainWindow, &w1, &h1);
    winmain::SetWindowSize(1200);
    int w2 = 0, h2 = 0;
    SDL_GetWindowSize(winmain::MainWindow, &w2, &h2);
    fprintf(report, "600 and 1200 window presets produced %d x %d and %d x %d client pixels.\n", w1, h1, w2, h2);
    check("two distinct resizable windowed presets", w2 > w1 && h2 > h1 &&
          (SDL_GetWindowFlags(winmain::MainWindow) & SDL_WINDOW_FULLSCREEN) == 0);
    auto originalGraphics = render::EnhancedGraphics;
    render::EnhancedGraphics = false;
    capture("", drawUi, false); // first ImGui frame establishes the menu height
    auto classic = capture(prefix + "classic.bmp", drawUi, false);
    render::EnhancedGraphics = true;
    auto hd = capture(prefix + "hd.bmp", drawUi, false);
    fprintf(report, "Same-state captures %d x %d, board hashes %08x / %08x. HD textures: %d, luminous regions: %zu.\n",
            hd.width, hd.height, classic.boardHash, hd.boardHash, render::HdAssetCount, render::EmissiveRegions.size());
    check("original-size scene and HD capture both draw a nonempty field",
          classic.saved && hd.saved && classic.width == hd.width && classic.height == hd.height &&
          classic.litPixels > 1500 && hd.litPixels > 1500);
    check("318 original DAT texture groups loaded and classic/HD field pixels differ",
          render::HdAssetCount == 318 && classic.boardHash != hd.boardHash);
    const auto priorPos = ball->Position;
    step(16);
    auto moving = capture(prefix + "hd-motion.bmp", drawUi, true);
    const bool moved = priorPos.X != ball->Position.X || priorPos.Y != ball->Position.Y;
    check("subsequent HD frame follows the moving ball and changes field pixels",
          moving.saved && moved && moving.boardHash != hd.boardHash);
    check("live lights registered for emissive rendering", !render::EmissiveRegions.empty());
    const bool nativeHdr = HdrOutput::Active();
    if (nativeHdr)
    {
        float peak = HdrOutput::LastPeak();
        fprintf(report, "DXGI HDR active. Measured FP16 peak %.3f scRGB (%.1f nits).\n", peak, peak * 80);
        check("measured HDR highlights above SDR diffuse white", peak > 2.0f);
        check("in-play message and score matrices share HDR diode optics",matrixChecks61::compare(report));
        const char* labels[]={"info","mission","score","ball","player"};
        for (unsigned i=0;i<5;++i)
        {
            auto m=HdrOutput::LastMatrix(i);
            fprintf(report,"Matrix %s: brightest %.1f, darkest %.3f nits (Rec.709 luminance in output FP16).\n",
                    labels[i],m.brightest,m.darkest);
        }
    }
    else
    {
        fprintf(report, "SKIP  Physical HDR presentation: %s. Screenshots are SDR.\n", HdrOutput::Status());
        if (rendererInfo.name && !strcmp(rendererInfo.name, "direct3d11"))
        {
            capture("", drawUi, true, true);
            fprintf(report, "Diagnostic FP16 swapchain on SDR output: %s; peak %.3f scRGB (%.1f nits encoded).\n",
                    HdrOutput::Status(), HdrOutput::LastPeak(), HdrOutput::LastPeak() * 80);
            check("diagnostic D3D11 FP16 backbuffer readback exceeds diffuse white",
                  HdrOutput::Active() && HdrOutput::LastPeak() > 2.0f);
        }
    }
    HdrOutput::Close();
    const std::string initialLanguage=options::Options.Language.V;
    GameMenu::SetLanguage("en");
    auto english=capture(prefix+"game-en.bmp",drawUi,false);
    fprintf(report,"Blue cabinet palette, integrated green/blue by aperture: score %.3f, status %.3f, mission %.3f.\n",
            english.displayGreenOverBlue[0],english.displayGreenOverBlue[1],english.displayGreenOverBlue[2]);
    for (int i : {1,2})
        check(i==1?"status LEDs match the blue numeric display":"mission LEDs match the blue numeric display",
              english.displayGreenOverBlue[i]>.40f &&
              std::abs(english.displayGreenOverBlue[i]-english.displayGreenOverBlue[0])<.055f);
    GameMenu::SetLanguage("ru");
    auto russian=capture(prefix+"game-ru.bmp",drawUi,false);
    check("Russian and English captures at the same simulation tick",
          english.saved && russian.saved && english.boardHash==russian.boardHash);
    GameMenu::SetLanguage(initialLanguage.c_str());
    inputChecks::tap(SDLK_ESCAPE);
    check("Escape opens menu and pauses simulation", GameMenu::IsOpen() && winmain::single_step);
    capture("", drawUi, false);
    auto menu = capture(prefix + "menu.bmp", drawUi, false);
    check("cabinet menu rendered over game", menu.saved && menu.boardHash != moving.boardHash);
    for (const auto confirm : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_SPACE})
    {
        // From Resume, navigate past Settings and Controls to High scores.
        capture("",drawUi,false);
        capture("",drawUi,false);
        for (int i=0;i<3;++i)
        {
            inputChecks::key(SDLK_DOWN,true);
            capture("",drawUi,false);
            inputChecks::key(SDLK_DOWN,false);
            capture("",drawUi,false);
        }
        inputChecks::key(confirm,true);
        capture("",drawUi,false);
        inputChecks::key(confirm,false);
        capture("",drawUi,false);
        const bool scoresOpen=GameMenu::ScoreShoulder(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,false);
        const std::string description=std::string(SDL_GetKeyName(confirm))+" opens the score page after arrow navigation";
        check(description.c_str(),scoresOpen);
        if (scoresOpen) inputChecks::tap(SDLK_ESCAPE);
    }
    GameMenu::Controls();
    capture("", drawUi, false);
    auto controls = capture(prefix + "controls.bmp", drawUi, false);
    check("controls page reached and rendered", controls.saved && GameMenu::IsOpen());
    inputChecks::tap(SDLK_ESCAPE);
    check("Escape returns from controls to home", GameMenu::IsOpen() && winmain::single_step);
    inputChecks::tap(SDLK_ESCAPE);
    check("Escape from home starts the return countdown", !GameMenu::IsOpen() && winmain::ResumeStage()==3 && !winmain::CanAdvanceGame());
    inputChecks::finishResumeCountdown();
    capture("", drawUi, false); // update ImGui capture flags after dismissing menu
    inputChecks::tap(SDLK_F3);
    const bool paused = winmain::single_step;
    inputChecks::tap(SDLK_F3);
    check("F3 pauses and starts the return countdown", paused && winmain::ResumeStage()==3 && !winmain::CanAdvanceGame());
    inputChecks::finishResumeCountdown();
    inputChecks::tap(SDLK_F4);
    const bool fullscreen = (SDL_GetWindowFlags(winmain::MainWindow) & SDL_WINDOW_FULLSCREEN) != 0;
    if (nativeHdr)
    {
        auto full = capture("", drawUi, true);
        fprintf(report, "HDR fullscreen %d x %d, peak %.1f encoded nits.\n", full.width,full.height,HdrOutput::LastPeak()*80);
        check("native fullscreen HDR", fullscreen && HdrOutput::Active() && HdrOutput::LastPeak()>2.0f);
        check("fullscreen cabinet letters and digits share HDR diode optics",matrixChecks61::compare(report));
    }
    inputChecks::tap(SDLK_F4);
    check("F4 toggles fullscreen and returns to windowed", fullscreen &&
          (SDL_GetWindowFlags(winmain::MainWindow) & SDL_WINDOW_FULLSCREEN) == 0);
    check("first frames after changing size fill the canvas at the original aspect ratio",
          maximizeChecks631::checkFrameSize(report,prefix));
#ifdef _WIN32
    if (strcmp(SDL_GetCurrentVideoDriver(),"windows")==0)
        check("Windows maximize fills the monitor and F4 restores the previous window",maximizeChecks631::run(report));
#endif
    auto doubleClick=[&](bool poster) {
        fullscrn::window_size_changed();
        auto r=render::DestinationRect;
        SDL_Event event{};
        event.type=SDL_MOUSEBUTTONDOWN; event.button.button=SDL_BUTTON_LEFT; event.button.clicks=2;
        event.button.x=int((r.x+r.w*(poster?.84f:.28f))/ImGui::GetIO().DisplayFramebufferScale.x);
        event.button.y=int((r.y+r.h*.2f)/ImGui::GetIO().DisplayFramebufferScale.y);
        winmain::event_handler(&event);
        event.type=SDL_MOUSEBUTTONUP;
        winmain::event_handler(&event);
    };
    doubleClick(false);
    const bool playfieldWindowed=!options::Options.FullScreen;
    doubleClick(true);
    const bool posterFullscreen=SDL_GetWindowFlags(winmain::MainWindow)&SDL_WINDOW_FULLSCREEN;
    doubleClick(true);
    check("backglass double-click toggles fullscreen and playfield double-click stays available for flippers",
          playfieldWindowed && posterFullscreen && !options::Options.FullScreen);
    winmain::SetWindowSize(0);
    int initialWidth, initialHeight;
    SDL_GetWindowSize(winmain::MainWindow, &initialWidth, &initialHeight);
    auto sizes = winmain::WindowSizes();
    winmain::SetWindowSize(sizes.front());
    auto next = std::next(sizes.begin());
    bool cycleWorks = true;
    for (; next != sizes.end(); ++next)
    {
        inputChecks::tap(SDLK_F7);
        int width, height;
        SDL_GetWindowSize(winmain::MainWindow, &width, &height);
        fprintf(report, "F7 preset %d x %d\n", width, height);
        cycleWorks &= width == *next && !(SDL_GetWindowFlags(winmain::MainWindow) & SDL_WINDOW_FULLSCREEN);
    }
    inputChecks::tap(SDLK_F7);
    const bool cycleFullscreen = SDL_GetWindowFlags(winmain::MainWindow) & SDL_WINDOW_FULLSCREEN;
    inputChecks::tap(SDLK_F7);
    int returnedWidth, returnedHeight;
    SDL_GetWindowSize(winmain::MainWindow, &returnedWidth, &returnedHeight);
    fprintf(report, "Adaptive preset %d x %d, returned %d x %d\n",
            initialWidth, initialHeight, returnedWidth, returnedHeight);
    check("F7 passes through larger half-step presets and fullscreen then returns to the adaptive size",
          cycleWorks && cycleFullscreen && !(SDL_GetWindowFlags(winmain::MainWindow) & SDL_WINDOW_FULLSCREEN) &&
          returnedWidth == initialWidth && returnedHeight == initialHeight);
    if (nativeHdr)
    {
        auto windowed = capture("", drawUi, true);
        fprintf(report, "HDR restored window %d x %d, peak %.1f encoded nits.\n", windowed.width,windowed.height,HdrOutput::LastPeak()*80);
        check("native windowed HDR after fullscreen", HdrOutput::Active() && HdrOutput::LastPeak()>2.0f);
        auto m=HdrOutput::LastMatrix(1);
        fprintf(report,"Windowed mission panel at adaptive size: LEDs %.1f nits, unlit %.3f nits.\n",m.brightest,m.darkest);
        check("mission matrix retains emissive headroom at the default window size",m.brightest>160.f && m.darkest<2.f);
        HdrOutput::Present(winmain::MainWindow,winmain::Renderer,false);
        check("in-game HDR switch releases scRGB output", !HdrOutput::Active());
        capture("", drawUi, true);
        check("re-enabling HDR restores emissive headroom", HdrOutput::Active() && HdrOutput::LastPeak()>2.0f);
        HdrOutput::Close();
    }
    freshBall();
    check("keyboard, mouse and standardized game controller input", inputChecks::run(report));
    freshBall();
    render::EnhancedGraphics = true;
    check("native sling collision, three live discharges, timer extinction and lit star bumper",
          visualChecks::run(report, prefix));
    check("independent nebulae, electrical discharge and field pulses",effectsChecks4::run(report,prefix));
    check("local light travels inside the breathing dome",domeVeinChecks632::run(report,prefix));
    check("slow surface rotation on the poster globe and isolated star flares",planetChecks61::run(report,prefix));
    freshBall();
    check("raised guides occlude live and unlit field inserts",insertChecks4::run(report,prefix));
    if (nativeHdr)
    {
        check("FP16 lens emission is occluded at the raised left return bracket",insertChecks4::runHdr(report));
        check("FP16 light travels along the dye in the central glass",domeVeinChecks632::hdr(report));
    }
    else
        fprintf(report,"SKIP  FP16 insert occlusion requires active Windows HDR.\n");
    freshBall();
    check("palette and HD views of all direction arrows and the red ramp insert",fieldArtChecks5::run(report,prefix));
    freshBall();
    check("all eleven native lane detectors and their registered artwork",detectorChecks6::run(report,prefix));
    freshBall();
    check("plunger travel, three drop-target banks and two spinner axles", cabinetArtChecks::run(report,prefix));
    freshBall();
    check("HD spring interpolation through native controls and eight original poses",springChecks61::run(report,prefix));
    freshBall();
    render::EnhancedGraphics = true;
    check("five additional metal receivers and surrounding print",metalChecks4::run(report,prefix));
    render::EnhancedGraphics = originalGraphics;
    check("table scoring, lamps, wormholes and missions", verifyTableRules(report));
    pb::MainTable->Message(MessageCode::Reset, 0);
    pb::replay_level(false);
    check("mode-specific physics, outlanes and tilt", verifyFairMechanics(report));
    auto info=pb::InfoTextBox, mission=pb::MissTextBox;
    info->Clear(); mission->Clear();
    info->Display(CabinetText(Msg::STRING111,1234567),8.f);
    info->Display(CabinetText(Msg::STRING195,7654321),3.f);
    mission->Display(CabinetText(Msg::STRING207,Msg::STRING161),-1.f);
    auto first=info->CurrentMessage;
    auto queued=first->NextMessage;
    auto objective=mission->CurrentMessage;
    const int timer=info->Timer, end=first->EndTicks, score=pb::MainTable->CurScore, tick=pb::time_ticks;
    GameMenu::SetLanguage("ru");
    const std::string russianInfo=first->Text,russianTask=objective->Text,russianNext=queued->Text;
    GameMenu::SetLanguage("en");
    check("switching language preserves running timers, objectives, points and queued numeric arguments",
          first==info->CurrentMessage && first->NextMessage==queued && timer==info->Timer && end==first->EndTicks &&
          tick==pb::time_ticks && score==pb::MainTable->CurScore && russianInfo!=first->Text &&
          russianTask!=objective->Text && russianNext!=queued->Text && strstr(first->Text,"1234567") && strstr(queued->Text,"7654321"));
    GameMenu::SetLanguage("ru");
    check("switching back restores both messages and the waiting queue",russianInfo==first->Text &&
          russianTask==objective->Text && russianNext==queued->Text && options::GetSetting("Language","")=="ru");
    if (nativeHdr)
    {
        fullscrn::set_screen_mode(1);
        capture("",drawUi,true);
        bool readable=true;
        for (unsigned i=0;i<2;++i)
        {
            auto matrix=HdrOutput::LastMatrix(i);
            fprintf(report,"Fullscreen HDR %s panel: LEDs %.1f nits, unlit %.3f nits (Rec.709 Y).\n",
                    i?"mission":"information",matrix.brightest,matrix.darkest);
            readable &= matrix.brightest>160.f && matrix.darkest<2.f;
        }
        check("both independent dynamic matrix panels exceed 160 nit reference white and keep black between LEDs",readable);
        fullscrn::set_screen_mode(0);
    }
    fullscrn::set_screen_mode(1);
    const auto originalMode = pb::FairMode;
    auto gameWindow = winmain::MainWindow;
    auto gameRenderer = winmain::Renderer;
    const Uint32 windowID = SDL_GetWindowID(gameWindow);
    bool stableScreen = true;
    for (bool mode : {originalMode, !originalMode, originalMode})
    {
        options::Options.FairRules = mode;
        inputChecks::tap(SDLK_F2);
        capture("", drawUi, nativeHdr);
        stableScreen &= pb::FairMode==mode && pb::game_mode==GameModes::InGame && !GameMenu::IsOpen() &&
                        winmain::MainWindow==gameWindow && winmain::Renderer==gameRenderer &&
                        SDL_GetWindowID(gameWindow)==windowID && (SDL_GetWindowFlags(gameWindow)&SDL_WINDOW_FULLSCREEN) &&
                        (!nativeHdr || HdrOutput::Active());
    }
    check("F2 and both rule changes keep the original fullscreen window, renderer and HDR output",stableScreen);
    fullscrn::set_screen_mode(0);
    pb::MainTable->Message(MessageCode::Reset,0);
    winmain::new_game();
    int introFrames = 0;
    while (introFrames < 1800 && (pb::MainTable->LightShowTimer || !pb::MainTable->BallList.front()->ActiveFlag))
    {
        step(1);
        ++introFrames;
    }
    check("intro completes and feeds the first playable ball automatically", introFrames>0 && introFrames<1800 &&
          !GameMenu::IsOpen() && !winmain::single_step && pb::MainTable->BallCount==pb::MainTable->MaxBallCount);
    inputChecks::windowEvent(SDL_WINDOWEVENT_FOCUS_LOST);
    const bool frozenBeforeLaunch = !winmain::CanAdvanceGame();
    inputChecks::windowEvent(SDL_WINDOWEVENT_FOCUS_GAINED);
    check("refocusing before deployment resumes immediately", frozenBeforeLaunch &&
          winmain::CanAdvanceGame() && !winmain::ResumePending());
    // The just-fed ball has to roll to the spring. An immediate analog
    // release arms the stock mechanism but its strike window expires empty.
    auto spring = pb::MainTable->Plunger;
    const bool pulled = spring->SetAnalogPull(.4f);
    spring->ReleaseAnalog();
    step(5);
    inputChecks::tap(SDLK_F3);
    inputChecks::tap(SDLK_SPACE);
    check("a missed plunger stroke permits immediate F3/Space resume", pulled &&
          spring->Threshold > 1e8f && !control::HasDeployedBall() &&
          winmain::CanAdvanceGame() && !winmain::ResumePending());
    step(480); // Allow the fed ball to settle against the spring, as in the independent launch tests.
    for (int attempt=0; attempt<12 && !control::HasDeployedBall(); ++attempt)
    {
        spring->SetAnalogPull(.9f);
        spring->ReleaseAnalog();
        step(7);
    }
    check("countdown fixture has a ball actually struck by the spring",control::HasDeployedBall());
    check("focus, pause, inputs and bilingual countdown on a moving ball",
          resumeChecks61::run(report, prefix, [&](const std::string& name){ return capture(name, drawUi, false); }));
    check("one localized cue outside the visible ball in HD and classic rendering",resumeBallChecks62::run(report,prefix));
    check("fixed LED lattice, page timing and bilingual message layout",
          matrixLayoutChecks62::run(report,prefix,[&](const std::string& file){return capture(file,drawUi,false).saved;}));
    check("spinner blades settle visibly after the last native turn",spinnerRestChecks62::run(report,prefix));
    freshBall();
    step(360);
    inputChecks::key(launchKey.Value,true);
    step(150);
    inputChecks::key(launchKey.Value,false);
    step(12);
    check("paused cabinet art advances while the ball, score and game timers stay frozen",pausedArtChecks63::run(report,prefix));
    check("launch cue returns for a weak shot and for a saved ball in an existing mission",
          starStripChecks63::replayAndWeakLaunch(report,prefix));
    check("time remaining after impact and mirrored flipper subdivision",physicsStepChecks70::run(report));
    check("solid sphere spin, contact friction and moving steel grain",ballSpinChecks70::run(report,prefix));
    GameMenu::SetLanguage(initialLanguage.c_str());
    fprintf(report, "\n%s\n", passed ? "PASS" : "FAIL");
    return fclose(report) == 0 && passed;
}
