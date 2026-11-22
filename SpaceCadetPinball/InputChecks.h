#pragma once
#include "options.h"
#include "pb.h"
#include "TFlipper.h"
#include "TBall.h"
#include "TPlunger.h"
#include "nudge.h"
#include "TPinballTable.h"
#include "winmain.h"

namespace inputChecks
{
    inline void key(SDL_Keycode code, bool down)
    {
        SDL_Event event{};
        event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
        event.key.keysym.sym = code;
        event.key.keysym.scancode = SDL_GetScancodeFromKey(code);
        winmain::event_handler(&event);
    }
    inline void tap(SDL_Keycode code) { key(code, true); key(code, false); }
    inline void finishResumeCountdown()
    {
        if (!winmain::ResumePending()) return;
        const auto clear = SDL_GetTicks64() + 2600;
        winmain::UpdateResumeCountdown(clear);
        winmain::ResumeFramePresented(clear);
        winmain::UpdateResumeCountdown(clear + 300);
    }
    inline void windowEvent(SDL_WindowEventID kind)
    {
        SDL_Event event{};
        event.type = SDL_WINDOWEVENT;
        event.window.windowID = SDL_GetWindowID(winmain::MainWindow);
        event.window.event = static_cast<Uint8>(kind);
        winmain::event_handler(&event);
    }

    inline void dispatchPads()
    {
        SDL_PumpEvents();
        SDL_Event event;
        while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_CONTROLLERAXISMOTION,
                              SDL_CONTROLLERDEVICEREMAPPED) == 1)
            winmain::event_handler(&event);
    }

    struct VirtualPad
    {
        SDL_JoystickID instance{-1};
        SDL_GameController* controller{};
        SDL_Joystick* joystick{};

        int deviceIndex() const
        {
            const int count = SDL_NumJoysticks();
            for (int index = 0; index < count; ++index)
                if (SDL_JoystickGetDeviceInstanceID(index) == instance) return index;
            return -1;
        }

        VirtualPad(Uint16 vendor, Uint16 product, const char* name)
        {
            SDL_VirtualJoystickDesc desc{};
            desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
            desc.naxes = SDL_CONTROLLER_AXIS_MAX;
            desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
            desc.vendor_id = vendor;
            desc.product_id = product;
            desc.name = name;
            desc.axis_mask = (1u << desc.naxes) - 1;
            desc.button_mask = (1u << desc.nbuttons) - 1;
            SDL_LockJoysticks();
            const int attachedIndex = SDL_JoystickAttachVirtualEx(&desc);
            if (attachedIndex >= 0) instance = SDL_JoystickGetDeviceInstanceID(attachedIndex);
            SDL_UnlockJoysticks();
            if (instance < 0) return;
            dispatchPads();
            // SDL assigns a canonical mapping to a virtual game controller.
            // A physical hot-plug in the same pump can renumber every device.
            SDL_LockJoysticks();
            const int index = deviceIndex();
            if (index >= 0) controller = SDL_GameControllerOpen(index);
            SDL_UnlockJoysticks();
            if (controller)
                joystick = SDL_GameControllerGetJoystick(controller);
        }
        void disconnect()
        {
            if (instance < 0) return;
            SDL_LockJoysticks();
            const int index = deviceIndex();
            if (index >= 0) SDL_JoystickDetachVirtual(index);
            SDL_UnlockJoysticks();
            dispatchPads();
            if (controller) SDL_GameControllerClose(controller);
            controller = nullptr; joystick = nullptr; instance = -1;
        }
        ~VirtualPad() { disconnect(); }
        void button(SDL_GameControllerButton name, bool down)
        {
            auto binding = SDL_GameControllerGetBindForButton(controller, name);
            if (binding.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON)
                SDL_JoystickSetVirtualButton(joystick, binding.value.button, down ? SDL_PRESSED : SDL_RELEASED);
            dispatchPads();
        }
        void axis(SDL_GameControllerAxis name, Sint16 value)
        {
            auto binding = SDL_GameControllerGetBindForAxis(controller, name);
            if (binding.bindType == SDL_CONTROLLER_BINDTYPE_AXIS)
                SDL_JoystickSetVirtualAxis(joystick, binding.value.axis, value);
            dispatchPads();
        }
    };

    inline bool run(FILE* file)
    {
        bool ok = true;
        auto check = [&](bool value, const char* name) {
            fprintf(file, "%s  %s\n", value ? "PASS" : "FAIL", name);
            ok &= value;
        };
        auto only = [](GameInput input, GameBindings action) {
            auto map = options::MapGameInput(input);
            return map.size() == 1 && map.front() == action;
        };
        const auto left = GameBindings::LeftFlipper, right = GameBindings::RightFlipper;
        check(only({InputTypes::Keyboard, SDLK_LSHIFT}, left) && only({InputTypes::Keyboard, SDLK_LCTRL}, left) &&
              only({InputTypes::Keyboard, SDLK_z}, left) && only({InputTypes::Keyboard, SDLK_RSHIFT}, right) &&
              only({InputTypes::Keyboard, SDLK_RCTRL}, right) && only({InputTypes::Keyboard, SDLK_PERIOD}, right) &&
              only({InputTypes::Keyboard, SDLK_RIGHT}, GameBindings::RightTableBump),
              "three keyboard bindings per flipper and a separate right nudge");
        std::vector<GameInput> previous;
        for (auto& action : options::Options.Key)
            for (auto input : action.Inputs) previous.push_back(input);
        options::BeginBinding(left, 4); tap(SDLK_j);
        options::Options.Key[~left].Load();
        check(only({InputTypes::Keyboard,SDLK_j},left) && only({InputTypes::Keyboard,SDLK_LSHIFT},left) &&
              only({InputTypes::Keyboard,SDLK_LCTRL},left), "editing and reloading a third key preserves the first two");
        for (const auto confirm : {SDLK_RETURN, SDLK_KP_ENTER})
        {
            options::BeginBinding(left);
            tap(confirm);
            check(!options::WaitingForInput() && only({InputTypes::Keyboard,confirm},left),
                  confirm==SDLK_RETURN ? "Return can still be bound to a game action" :
                                         "keypad Enter can still be bound to a game action");
        }
        options::BeginBinding(left); tap(SDLK_LCTRL);
        options::BeginBinding(right); tap(SDLK_RCTRL);
        check(only({InputTypes::Keyboard, SDLK_LCTRL}, left) && only({InputTypes::Keyboard, SDLK_RCTRL}, right),
              "binding capture distinguishes left and right Control");
        auto& lrow = options::Options.Key[static_cast<int>(left)];
        auto& rrow = options::Options.Key[static_cast<int>(right)];
        const auto leftEntry = std::string(lrow.Name) + " 0 input=" + std::to_string(SDLK_LCTRL);
        const auto rightEntry = std::string(rrow.Name) + " 0 input=" + std::to_string(SDLK_RCTRL);
        std::string settings;
        auto ini = fopenu(ImGui::GetIO().IniFilename, "rb");
        if (ini)
        {
            char line[512];
            while (fgets(line, sizeof(line), ini)) settings += line;
            fclose(ini);
        }
        lrow.Reset(); rrow.Reset();
        lrow.Load(); rrow.Load();
        check(settings.find(leftEntry) != std::string::npos && settings.find(rightEntry) != std::string::npos &&
              only({InputTypes::Keyboard, SDLK_LCTRL}, left) && only({InputTypes::Keyboard, SDLK_RCTRL}, right),
              "saved profile contains both modifier codes and controls reload them");
        options::BeginBinding(right); tap(SDLK_ESCAPE);
        check(!options::WaitingForInput() && !winmain::CabinetMenuOpen() && only({InputTypes::Keyboard, SDLK_RCTRL}, right),
              "Escape cancels binding and preserves the previous key");
        SDL_Event mouse{};
        mouse.type = SDL_MOUSEBUTTONDOWN;
        mouse.button.button = SDL_BUTTON_X2;
        mouse.button.state = SDL_PRESSED;
        options::BeginBinding(left);
        winmain::event_handler(&mouse);
        mouse.type = SDL_MOUSEBUTTONUP;
        mouse.button.state = SDL_RELEASED;
        winmain::event_handler(&mouse);
        check(only({InputTypes::Mouse, SDL_BUTTON_X2}, left), "mouse side-button assignment");
        options::BeginBinding(right); tap(SDLK_LCTRL);
        check(only({InputTypes::Keyboard, SDLK_LCTRL}, right), "duplicate binding moves to one action");
        options::BeginBinding(left); tap(SDLK_LCTRL);
        UsingSdlHint background(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        VirtualPad xbox(0x045e, 0x02ea, "Virtual Xbox One controller");
        VirtualPad dualsense(0x054c, 0x0ce6, "Virtual DualSense controller");
        check(xbox.joystick && dualsense.joystick, "two SDL virtual game controllers opened");
        if (xbox.joystick && dualsense.joystick)
        {
            fprintf(file, "SDL virtual controller types %d / %d; names %s / %s\n",
                    SDL_GameControllerGetType(xbox.controller), SDL_GameControllerGetType(dualsense.controller),
                    SDL_GameControllerName(xbox.controller), SDL_GameControllerName(dualsense.controller));
            options::BeginBinding(left);
            xbox.button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true);
            xbox.button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, false);
            check(only({InputTypes::GameController, SDL_CONTROLLER_BUTTON_LEFTSHOULDER}, left),
                  "virtual Xbox shoulder captured through SDL events");
            options::BeginBinding(right);
            dualsense.axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
            dualsense.axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
            dualsense.axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
            check(only({InputTypes::ControllerTrigger, SDL_CONTROLLER_AXIS_TRIGGERRIGHT}, right),
                  "virtual DualSense analog trigger captured through SDL events");
            auto advance = []() { for (int i = 0; i < 26; ++i) pb::frame(1000.0f / 120); };
            auto flipper = pb::MainTable->FlipperL;
            key(SDLK_LCTRL, true); advance();
            const bool keyRaised = flipper->BmpIndex > 0;
            xbox.button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true);
            key(SDLK_LCTRL, false); advance();
            const bool padHeld = flipper->BmpIndex > 0;
            xbox.button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, false); advance();
            check(keyRaised && padHeld && flipper->BmpIndex == 0, "two devices hold and release the same flipper independently");
            dualsense.axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767); advance();
            auto pressed = pb::MainTable->FlipperR->BmpIndex > 0;
            dualsense.axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768); advance();
            check(pressed && pb::MainTable->FlipperR->BmpIndex == 0, "trigger actuates right flipper through gameplay loop");

            auto table = pb::MainTable;
            auto plunger = table->Plunger;
            auto launch = [&](VirtualPad& pad, float fraction) {
                pb::replay_level(false);
                if (table->LightShowTimer) pb::replay_level(false);
                for (int i=0; i<540; ++i) pb::frame(1000.f/120);
                auto ball = table->BallList.front();
                pad.axis(SDL_CONTROLLER_AXIS_RIGHTY, Sint16(32767 * fraction));
                const float peak = plunger->Boost;
                const bool pulling = plunger->AnalogPull;
                // Ease it halfway back before release to verify peak retention.
                pad.axis(SDL_CONTROLLER_AXIS_RIGHTY, Sint16(32767 * fraction * .5f));
                pad.axis(SDL_CONTROLLER_AXIS_RIGHTY, 0);
                check(pulling && !plunger->AnalogPull && plunger->Threshold == 0.f &&
                      plunger->Boost == peak, "right-stick release restores the peak pull");
                float upSpeed = 0.f;
                std::srand(1903);
                for (int i=0; i<12; ++i)
                {
                    pb::frame(1000.f/120);
                    upSpeed = std::max(upSpeed, -ball->Speed * ball->Direction.Y);
                }
                fprintf(file, "Right-stick %g%%, native plunger boost %.3f, actual upward ball speed %.3f\n",
                        100 * fraction, peak, upSpeed);
                return upSpeed;
            };
            const float weak = launch(xbox, .30f);
            const float strong = launch(dualsense, .90f);
            check(weak > 5.f && strong > 2.f * weak,
                  "30% and 90% sticks launch the ball with different physical speeds");

            nudge::reset();
            xbox.axis(SDL_CONTROLLER_AXIS_LEFTX, -29000);
            const bool fromLeft = nudge::nudged_right;
            xbox.axis(SDL_CONTROLLER_AXIS_LEFTX, 30000);
            const bool held = !nudge::nudged_left && nudge::nudged_right;
            for (int i=0; i<6; ++i) pb::frame(1000.f/120);
            xbox.axis(SDL_CONTROLLER_AXIS_LEFTX, 0);
            const bool release = !nudge::nudged_left && !nudge::nudged_right;
            for (int i=0; i<162; ++i) pb::frame(1000.f/120);
            xbox.axis(SDL_CONTROLLER_AXIS_LEFTX, 29000);
            const bool fromRight = nudge::nudged_left;
            xbox.axis(SDL_CONTROLLER_AXIS_LEFTX, 0);
            for (int i=0; i<162; ++i) pb::frame(1000.f/120);
            xbox.axis(SDL_CONTROLLER_AXIS_LEFTY, -30000);
            check(fromLeft && fromRight && held && release && nudge::nudged_up && !table->TiltLockFlag,
                  "left stick nudges from both sides or below and must recenter between gestures");
            xbox.axis(SDL_CONTROLLER_AXIS_LEFTY, 0);

            xbox.axis(SDL_CONTROLLER_AXIS_RIGHTY, 26000);
            const bool beforeMenu = plunger->AnalogPull;
            tap(SDLK_ESCAPE);
            xbox.axis(SDL_CONTROLLER_AXIS_RIGHTY, 24000);
            const bool inMenu = winmain::CabinetMenuOpen() && !plunger->AnalogPull && plunger->Boost == 0.f;
            tap(SDLK_ESCAPE);
            finishResumeCountdown();
            xbox.axis(SDL_CONTROLLER_AXIS_RIGHTY, 28000);
            const bool afterMenu = !plunger->AnalogPull && plunger->Threshold > 1e8f;
            xbox.axis(SDL_CONTROLLER_AXIS_RIGHTY, 0);
            xbox.axis(SDL_CONTROLLER_AXIS_RIGHTY, 28000);
            check(beforeMenu && inMenu && afterMenu && plunger->AnalogPull,
                  "menu cancels analog charge; held stick must recenter after resume");
            tap(SDLK_F3);
            xbox.axis(SDL_CONTROLLER_AXIS_RIGHTY, 0);
            const bool paused = winmain::single_step && !plunger->AnalogPull && plunger->Boost == 0.f;
            tap(SDLK_F3);
            finishResumeCountdown();
            dualsense.axis(SDL_CONTROLLER_AXIS_RIGHTY, 27000);
            const bool beforeDetach = plunger->AnalogPull;
            dualsense.disconnect();
            check(paused && beforeDetach && !plunger->AnalogPull && plunger->Threshold > 1e8f && plunger->Boost == 0.f,
                  "pause and controller disconnect both cancel a charged plunger without firing");
        }
        options::CancelBinding();
        options::ReleaseActiveInputs();
        size_t i = 0;
        for (auto& action : options::Options.Key)
        {
            for (auto& input : action.Inputs) input = previous[i++];
            action.Save();
        }
        return ok;
    }
}
