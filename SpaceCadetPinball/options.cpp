#include "pch.h"
#include "options.h"

#include "fullscrn.h"
#include "midi.h"
#include "pb.h"
#include "render.h"
#include "Sound.h"
#include "winmain.h"
#include "translations.h"
#include "StickInput.h"
#include "TPinballTable.h"

namespace { void closeControllers(); }

constexpr int options::MaxUps, options::MaxFps, options::MinUps, options::MinFps, options::DefUps, options::DefFps;
constexpr int options::MaxSoundChannels, options::MinSoundChannels, options::DefSoundChannels;
constexpr int options::MaxVolume, options::MinVolume, options::DefVolume;

std::unordered_map<std::string, std::string> options::settings{};
bool options::ShowDialog = false;
std::vector<OptionBase*> options::AllOptions{};

optionsStruct options::Options
{
	{
		{
			"Left Flipper key",
			Msg::KEYMAPPER_FlipperL,
			{InputTypes::Keyboard, SDLK_LSHIFT},
			{InputTypes::Mouse, SDL_BUTTON_LEFT},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
			{InputTypes::Keyboard, SDLK_LCTRL}, {InputTypes::Keyboard, SDLK_z}
		},
		{
			"Right Flipper key",
			Msg::KEYMAPPER_FlipperR,
			{InputTypes::Keyboard, SDLK_RSHIFT},
			{InputTypes::Mouse,SDL_BUTTON_RIGHT},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
			{InputTypes::Keyboard, SDLK_RCTRL}, {InputTypes::Keyboard, SDLK_PERIOD}
		},
		{
			"Plunger key",
			Msg::KEYMAPPER_Plunger,
			{InputTypes::Keyboard, SDLK_SPACE},
			{InputTypes::Mouse,SDL_BUTTON_MIDDLE},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_A}
		},
		{
			"Left Table Bump key",
			Msg::KEYMAPPER_BumpLeft,
			{InputTypes::Keyboard, SDLK_x},
			{InputTypes::Mouse,SDL_BUTTON_X1},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_DPAD_LEFT}
		},
		{
			"Right Table Bump key",
			Msg::KEYMAPPER_BumpRight,
			{InputTypes::Keyboard, SDLK_RIGHT},
			{InputTypes::Mouse,SDL_BUTTON_X2},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_DPAD_RIGHT}
		},
		{
			"Bottom Table Bump key",
			Msg::KEYMAPPER_BumpBottom,
			{InputTypes::Keyboard, SDLK_UP},
			{},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_DPAD_UP}
		},
		{
			"New Game",
			Msg::Menu1_New_Game,
			{InputTypes::Keyboard, SDLK_F2},
			{},
			{}
		},
		{
			"Toggle Pause",
			Msg::Menu1_Pause_Resume_Game,
			{InputTypes::Keyboard, SDLK_F3},
			{},
			{}
		},
		{
			"Toggle FullScreen",
			Msg::Menu1_Full_Screen,
			{InputTypes::Keyboard, SDLK_F4},
			{},
			{}
		},
		{
			"Toggle Sounds",
			Msg::Menu1_Sounds,
			{InputTypes::Keyboard, SDLK_F5},
			{},
			{}
		},
		{
			"Toggle Music",
			Msg::Menu1_Music,
			{InputTypes::Keyboard, SDLK_F6},
			{},
			{}
		},
		{
			"Show Control Dialog",
			Msg::Menu1_Player_Controls,
			{InputTypes::Keyboard, SDLK_F8},
			{},
			{}
		},
		{
			"Toggle Menu Display",
			Msg::Menu1_ToggleShowMenu,
			{InputTypes::Keyboard, SDLK_ESCAPE},
			{},
			{InputTypes::GameController, SDL_CONTROLLER_BUTTON_START}
		},
		{
			"Exit",
			Msg::Menu1_Exit,
			{},
			{},
			{}
		},
		{"Cycle window size", Msg::Menu1_Full_Screen,
			{InputTypes::Keyboard, SDLK_F7}, {}, {}},
	},
	{"Sounds", true},
	{"Music", false},
	{"FullScreen", false},
	{"Players", 1},
	{"Screen Resolution", -1},
	{"UI Scale", 1.0f},
	{"Uniform scaling", true},
	{"Linear Filtering", true},
	{"Frames Per Second", DefFps},
	{"Updates Per Second", DefUps},
	{"ShowMenu", true},
	{"Uncapped Updates Per Second", false},
	{"Sound Channels", DefSoundChannels},
	{"HybridSleep", false},
	{"Prefer 3DPB Game Data", false},
	{"Integer Scaling", false},
	{"Sound Volume", DefVolume},
	{"Music Volume", DefVolume / 2},
	{"Stereo Sound Effects", false},
	{"Debug Overlay", false},
	{"Debug Overlay Grid", true},
	{"Debug Overlay All Edges", true},
	{"Debug Overlay Ball Position", true},
	{"Debug Overlay Ball Edges", true},
	{"Debug Overlay Collision Mask", true},
	{"Debug Overlay Sprites", true},
	{"Debug Overlay Sounds", true},
	{"Debug Overlay Ball Depth Grid", true},
	{"Debug Overlay AABB", true},
#ifdef _WIN32
	{"FontFileName", "C:/Windows/Fonts/segoeui.ttf"},
	{"Language", "ru"},
#else
	{"FontFileName", ""},
	{"Language", "en"},
#endif
	{"Hide Cursor", false},
	{"Meditative mode", false},
	{"Enhanced artwork", true},
	{"Smooth HD silhouettes", true},
	{"Automatic HDR", true},
	{"Window width", 0},
	{"Window height", 0},
	{"Analog plunger", true},
	{"Stick nudge", true},
	{"Resume countdown", true},
	{"GeneralUser music bank", 1}, // Preserve 7.0.x profile IDs: MIDI (0) and GeneralUser GS (1).
};

void options::InitPrimary()
{
	for (int i = 0; i < SDL_NumJoysticks(); ++i) ControllerAdded(i);
	auto imContext = ImGui::GetCurrentContext();
	ImGuiSettingsHandler ini_handler;
	ini_handler.TypeName = "Pinball";
	ini_handler.TypeHash = ImHashStr(ini_handler.TypeName);
	ini_handler.ReadOpenFn = MyUserData_ReadOpen;
	ini_handler.ReadLineFn = MyUserData_ReadLine;
	ini_handler.WriteAllFn = MyUserData_WriteAll;
	imContext->SettingsHandlers.push_back(ini_handler);

	// Settings are loaded from disk on the first frame
	if (!imContext->SettingsLoaded)
	{
		ImGui::LoadIniSettingsFromDisk(imContext->IO.IniFilename);
		imContext->SettingsLoaded = true;
	}

	for (const auto opt : AllOptions)
		opt->Load();
	if (!get_int("Keyboard alternatives revision", 0))
	{
		// Slots 0–2 already existed. Never take a key from an explicit binding.
		auto occupied = [](GameInput key) {
			for (const auto& row : Options.Key)
				for (int slot = 0; slot < 3; ++slot)
					if (row.Inputs[slot] == key) return true;
			return false;
		};
		auto& nudge = Options.Key[~GameBindings::RightTableBump].Inputs[0];
		if (nudge == GameInput{InputTypes::Keyboard, SDLK_PERIOD} &&
		    !occupied({InputTypes::Keyboard, SDLK_RIGHT}))
			nudge = {InputTypes::Keyboard, SDLK_RIGHT};
		for (auto& row : Options.Key)
		{
			for (int slot = 3; slot < ControlOption::SlotCount; ++slot)
				if (row.Inputs[slot].Type == InputTypes::Keyboard && occupied(row.Inputs[slot]))
					row.Inputs[slot] = {};
			row.Save();
		}
		set_int("Keyboard alternatives revision", 1);
	}
	if (!get_int("Sound voice revision",0))
	{
		// 6.x wrote its eight-voice default even for untouched profiles.
		if (Options.SoundChannels.V==8)
		{
			Options.SoundChannels=DefSoundChannels;
			Options.SoundChannels.Save();
		}
		set_int("Sound voice revision",1);
	}
	if (!get_int("Window preset revision",0))
	{
		// Earlier builds wrote their default dimensions even if the player never resized.
		if (Options.WindowWidth.V==1200 && Options.WindowHeight.V==832)
		{
			Options.WindowWidth=0; Options.WindowHeight=0;
		}
		set_int("Window preset revision",1);
	}
	PostProcessOptions();
}

void options::InitSecondary()
{
	auto maxRes = fullscrn::GetMaxResolution();
	if (Options.Resolution >= 0 && Options.Resolution > maxRes)
		Options.Resolution = maxRes;
	fullscrn::SetResolution(Options.Resolution == -1 ? maxRes : Options.Resolution);
}

void options::uninit()
{
	closeControllers();
	Options.Language.V = translations::GetCurrentLanguage()->ShortName;
	for (const auto opt : AllOptions)
	{
		opt->Save();
	}
}


int options::get_int(LPCSTR lpValueName, int defaultValue)
{
	auto value = GetSetting(lpValueName, std::to_string(defaultValue));
	return std::stoi(value);
}

void options::set_int(LPCSTR lpValueName, int data)
{
	SetSetting(lpValueName, std::to_string(data));
}

float options::get_float(LPCSTR lpValueName, float defaultValue)
{
	auto value = GetSetting(lpValueName, std::to_string(defaultValue));
	return std::stof(value);
}

void options::set_float(LPCSTR lpValueName, float data)
{
	SetSetting(lpValueName, std::to_string(data));
}

void options::GetInput(const std::string& rowName, GameInput (&values)[3])
{
	for (auto i = 0u; i <= 2; i++)
	{
		auto name = rowName + " " + std::to_string(i);
		auto inputType = static_cast<InputTypes>(get_int((name + " type").c_str(), -1));
		auto input = get_int((name + " input").c_str(), -1);
		if (inputType >= InputTypes::Keyboard && inputType <= InputTypes::ControllerTrigger && input != -1)
			values[i] = {inputType, input};
	}
}

void options::SetInput(const std::string& rowName, GameInput (&values)[3])
{
	for (auto i = 0u; i <= 2; i++)
	{
		auto input = values[i];
		auto name = rowName + " " + std::to_string(i);
		set_int((name + " type").c_str(), static_cast<int>(input.Type));
		set_int((name + " input").c_str(), input.Value);
	}
}

void options::toggle(Menu1 uIDCheckItem)
{
	switch (uIDCheckItem)
	{
	case Menu1::Sounds:
		Options.Sounds ^= true;
		Sound::Enable(Options.Sounds);
		return;
	case Menu1::SoundStereo:
		Options.SoundStereo ^= true;
		return;
	case Menu1::Music:
		Options.Music ^= true;
		if (!Options.Music)
			midi::music_stop();
		else
			midi::music_play();
		return;
	case Menu1::Show_Menu:
		Options.ShowMenu ^= true;
		fullscrn::window_size_changed();
		return;
	case Menu1::Full_Screen:
		if (fullscrn::set_screen_mode(!Options.FullScreen))
			Options.FullScreen ^= true;
		return;
	case Menu1::OnePlayer:
	case Menu1::TwoPlayers:
	case Menu1::ThreePlayers:
	case Menu1::FourPlayers:
		Options.Players = static_cast<int>(uIDCheckItem) - static_cast<int>(Menu1::OnePlayer) + 1;
		break;
	case Menu1::MaximumResolution:
	case Menu1::R640x480:
	case Menu1::R800x600:
	case Menu1::R1024x768:
		{
			auto restart = false;
			int newResolution = static_cast<int>(uIDCheckItem) - static_cast<int>(Menu1::R640x480);
			if (uIDCheckItem == Menu1::MaximumResolution)
			{
				restart = fullscrn::GetResolution() != fullscrn::GetMaxResolution();
				Options.Resolution = -1;
			}
			else if (newResolution <= fullscrn::GetMaxResolution())
			{
				restart = newResolution != (Options.Resolution == -1
					                            ? fullscrn::GetMaxResolution()
					                            : fullscrn::GetResolution());
				Options.Resolution = newResolution;
			}

			if (restart)
				winmain::Restart();
			break;
		}
	case Menu1::WindowUniformScale:
		Options.UniformScaling ^= true;
		fullscrn::window_size_changed();
		break;
	case Menu1::WindowLinearFilter:
		Options.LinearFiltering ^= true;
		render::recreate_screen_texture();
		break;
	case Menu1::Prefer3DPBGameData:
		Options.Prefer3DPBGameData ^= true;
		winmain::Restart();
		break;
	case Menu1::WindowIntegerScale:
		Options.IntegerScaling ^= true;
		fullscrn::window_size_changed();
		break;
	default:
		break;
	}
}

namespace
{
GameBindings capture = GameBindings::Max;
int captureKeyboardSlot = 0;
std::string bindingFeedback;
std::map<SDL_JoystickID, SDL_GameController*> controllers;
std::map<std::pair<SDL_JoystickID, int>, bool> triggers;
SDL_JoystickID navigationDevice = -1;
bool waitForNavRelease = false;

struct HeldInput
{
	GameInput input;
	SDL_JoystickID device;
	GameBindings action;
};
std::vector<HeldInput> heldInputs;
std::vector<HeldInput> menuKeys;

const char* textForLanguage(const char* ru, const char* en)
{
	return translations::GetCurrentLanguage()->Language == Lang::Russian ? ru : en;
}

const char* actionName(GameBindings binding)
{
	switch (binding)
	{
	case GameBindings::ToggleMenuDisplay: return textForLanguage("Меню игры", "Game menu");
	case GameBindings::CycleWindowSize: return textForLanguage("Следующий размер окна", "Cycle window size");
	case GameBindings::ShowControlDialog: return textForLanguage("Настроить управление", "Configure controls");
	default: return pb::get_rc_string(options::Options.Key[~binding].Description);
	}
}

void saveControls()
{
	for (const auto& control : options::Options.Key) control.Save();
	auto& io = ImGui::GetIO();
	if (io.IniFilename) ImGui::SaveIniSettingsToDisk(io.IniFilename);
}

bool containsAction(const std::vector<HeldInput>& list, GameBindings action)
{
	return std::any_of(list.begin(), list.end(), [action](const HeldInput& h){ return h.action == action; });
}

void releaseDevice(SDL_JoystickID id, bool all)
{
	for (size_t i = heldInputs.size(); i > 0; --i)
	{
		auto h = heldInputs[i - 1];
		if (!all && h.device != id) continue;
		heldInputs.erase(heldInputs.begin() + i - 1);
		if (!containsAction(heldInputs, h.action) && pb::MainTable) pb::InputUp(h.input);
	}
	menuKeys.erase(std::remove_if(menuKeys.begin(), menuKeys.end(), [id, all](const HeldInput& h)
		{ return all || h.device == id; }), menuKeys.end());
}

void closeControllers()
{
	heldInputs.clear();
	menuKeys.clear();
	triggers.clear();
	for (auto& entry : controllers) SDL_GameControllerClose(entry.second);
	controllers.clear();
	navigationDevice = -1;
	options::CancelBinding();
}
} // namespace

// Borrow the most recently used live gamepad without taking a new SDL reference.
// Also suppress widget activation until the button used to finish a capture lifts.
SDL_GameController* CabinetNavigationController()
{
	if (options::WaitingForInput()) return nullptr;
	auto it = controllers.find(navigationDevice);
	if (it == controllers.end() || !SDL_GameControllerGetAttached(it->second)) return nullptr;
	if (waitForNavRelease)
	{
		for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i)
			if (SDL_GameControllerGetButton(it->second, static_cast<SDL_GameControllerButton>(i)))
				return nullptr;
		waitForNavRelease = false;
	}
	return it->second;
}

void options::ConfigureControllers()
{
#ifdef _WIN32
	SDL_SetHintWithPriority(SDL_HINT_DIRECTINPUT_ENABLED, "0", SDL_HINT_OVERRIDE);
	// SDL's WGI driver defers Xbox-compatible devices if either XInput or
	// the RawInput gamepad backend is enabled. Let WGI accept those devices.
	SDL_SetHintWithPriority(SDL_HINT_XINPUT_ENABLED, "0", SDL_HINT_OVERRIDE);
	SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_RAWINPUT, "0", SDL_HINT_OVERRIDE);
	SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_RAWINPUT_CORRELATE_XINPUT, "0", SDL_HINT_OVERRIDE);
	SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_WGI, "1", SDL_HINT_OVERRIDE);
	SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1", SDL_HINT_DEFAULT);
	SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1", SDL_HINT_DEFAULT);
#endif
}

void options::ControllerAdded(int index)
{
	if (!SDL_IsGameController(index)) return;
	auto id = SDL_JoystickGetDeviceInstanceID(index);
	if (id < 0 || controllers.count(id)) return;
	auto controller = SDL_GameControllerOpen(index);
	if (!controller) return;
	controllers.emplace(id, controller);
	if (navigationDevice < 0) navigationDevice = id;
}

void options::ControllerUsed(SDL_JoystickID id)
{
	if (controllers.count(id)) navigationDevice = id;
}

void options::ControllerRemoved(SDL_JoystickID id)
{
	StickInput::Remove(id);
	releaseDevice(id, false);
	triggers.erase({id, SDL_CONTROLLER_AXIS_TRIGGERLEFT});
	triggers.erase({id, SDL_CONTROLLER_AXIS_TRIGGERRIGHT});
	auto it = controllers.find(id);
	if (it == controllers.end()) return;
	SDL_GameControllerClose(it->second);
	controllers.erase(it);
	if (id == navigationDevice) navigationDevice = controllers.empty() ? -1 : controllers.begin()->first;
}

bool options::TriggerChanged(SDL_JoystickID id, int axis, int value, bool& down)
{
	if (axis != SDL_CONTROLLER_AXIS_TRIGGERLEFT && axis != SDL_CONTROLLER_AXIS_TRIGGERRIGHT) return false;
	auto& old = triggers[{id, axis}];
	down = old ? value > 11000 : value >= 18000;
	if (down == old) return false;
	old = down;
	ControllerUsed(id);
	return true;
}

void options::ReleaseActiveInputs()
{
	StickInput::Cancel();
	releaseDevice(-1, true);
	triggers.clear();
}

void options::ResumeHeldFlippers()
{
	if (!pb::MainTable || winmain::DemoActive || pb::game_mode != GameModes::InGame) return;
	if (containsAction(heldInputs, GameBindings::LeftFlipper))
		pb::MainTable->Message(MessageCode::LeftFlipperInputPressed, pb::time_now);
	if (containsAction(heldInputs, GameBindings::RightFlipper))
		pb::MainTable->Message(MessageCode::RightFlipperInputPressed, pb::time_now);
}

void options::RouteInput(GameInput input, SDL_JoystickID device, bool down, bool allowGame)
{
	// Keyboard and mouse use distinct input types even if their device ids match.
	auto matches = [input,device](const HeldInput& h){ return h.input == input && h.device == device; };
	auto it = std::find_if(heldInputs.begin(), heldInputs.end(), matches);
	if (!down)
	{
		menuKeys.erase(std::remove_if(menuKeys.begin(), menuKeys.end(), matches), menuKeys.end());
		if (it != heldInputs.end())
		{
			auto h = *it;
			heldInputs.erase(it);
			if (!containsAction(heldInputs, h.action)) pb::InputUp(h.input);
		}
		return;
	}
	if (WaitingForInput())
	{
		InputDown(input);
		return;
	}
	const bool fixedMenu = input == GameInput(InputTypes::Keyboard, SDLK_ESCAPE) ||
	                       input == GameInput(InputTypes::GameController, SDL_CONTROLLER_BUTTON_START);
	const bool goBack = winmain::CabinetMenuOpen() &&
	                    (input == GameInput(InputTypes::GameController, SDL_CONTROLLER_BUTTON_BACK) ||
	                     input == GameInput(InputTypes::GameController, SDL_CONTROLLER_BUTTON_B));
	const auto bindings = MapGameInput(input);
	const auto action = bindings.empty() ? GameBindings::Max : bindings.front();
	if (fixedMenu || goBack || action == GameBindings::ToggleMenuDisplay)
	{
		if (std::find_if(menuKeys.begin(), menuKeys.end(), matches) != menuKeys.end()) return;
		bool first = menuKeys.empty();
		if (first) winmain::ToggleCabinetMenu();
		menuKeys.push_back({input, device, GameBindings::ToggleMenuDisplay});
		return;
	}
	if (!allowGame || it != heldInputs.end()) return;
	if (winmain::CabinetMenuOpen() && action != GameBindings::ToggleFullScreen && action != GameBindings::CycleWindowSize) return;
	if (winmain::single_step && action >= GameBindings::LeftFlipper && action <= GameBindings::BottomTableBump &&
		!(winmain::ResumePending() && (action == GameBindings::LeftFlipper || action == GameBindings::RightFlipper))) return;
	bool first = action == GameBindings::Max || !containsAction(heldInputs, action);
	heldInputs.push_back({input, device, action});
	if (first) pb::InputDown(input);
}

bool options::WaitingForInput() { return capture != GameBindings::Max; }

void options::BeginBinding(GameBindings action, int keyboardSlot)
{
	if (action < GameBindings::Min || action >= GameBindings::Max) return;
	capture = action;
	captureKeyboardSlot = keyboardSlot;
	bindingFeedback.clear();
}

void options::CancelBinding()
{
	capture = GameBindings::Max;
	bindingFeedback.clear();
	waitForNavRelease = true;
}

void options::InputDown(GameInput input)
{
	if (!WaitingForInput()) return;
	if (input == GameInput(InputTypes::Keyboard, SDLK_ESCAPE))
	{
		CancelBinding();
		return;
	}
	if (input == GameInput(InputTypes::Keyboard, SDLK_F1))
	{
		bindingFeedback = textForLanguage("F1 открывает помощь. Нажмите другую клавишу.",
		                                  "F1 opens Help. Press another key.");
		return;
	}
	if (input == GameInput(InputTypes::GameController, SDL_CONTROLLER_BUTTON_START) && capture != GameBindings::ToggleMenuDisplay)
	{
		bindingFeedback = textForLanguage("Start / Options открывает меню. Нажмите другую кнопку.",
		                                  "Start / Options opens the menu. Press another button.");
		return;
	}
	int slot = -1;
	switch (input.Type)
	{
	case InputTypes::Keyboard: slot = captureKeyboardSlot; break;
	case InputTypes::Mouse: slot = 1; break;
	case InputTypes::ControllerTrigger:
	case InputTypes::GameController: slot = 2; break;
	default: return;
	}
	auto previous = GameBindings::Max;
	for (auto action = GameBindings::Min; action < GameBindings::Max; action++)
		for (auto& value : Options.Key[~action].Inputs)
			if (value == input)
			{
				value = {};
				if (action != capture) previous = action;
			}
	auto applied = capture;
	Options.Key[~capture].Inputs[slot] = input;
	CancelBinding();
	bindingFeedback = std::string(actionName(applied)) + "  ·  " + input.GetShortInputDescription();
	if (previous != GameBindings::Max)
		bindingFeedback += std::string(textForLanguage("  (перенесено с «", "  (moved from “")) +
		                   actionName(previous) + textForLanguage("»)", "”)");
	saveControls();
}

void options::DrawControlBindings()
{
	if (WaitingForInput())
		ImGui::TextWrapped("%s  ·  %s", actionName(capture), textForLanguage("Нажмите клавишу или кнопку. Esc — отмена.",
		                                                                     "Press a key or button. Esc cancels."));
	else
		ImGui::TextWrapped("%s", textForLanguage("Выберите действие и нажмите нужную клавишу, кнопку мыши или геймпада.",
		                                         "Select an action and press a key, mouse button or gamepad control."));
	if (!bindingFeedback.empty()) ImGui::TextWrapped("%s", bindingFeedback.c_str());
	if (!controllers.empty())
	{
		auto it = controllers.find(navigationDevice);
		if (it != controllers.end())
			ImGui::TextDisabled("%s", SDL_GameControllerName(it->second));
	}
	ImGui::Spacing();
	ImGui::Checkbox(textForLanguage("Правый стик — пружина запуска", "Right stick pulls the plunger"), &Options.StickPlunger.V);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", textForLanguage("Потяните вниз и отпустите в центр. Сила зависит от глубины хода.",
		                                       "Pull down and release to center. The pull distance sets the launch strength."));
	ImGui::Checkbox(textForLanguage("Левый стик — толчок стола", "Left stick nudges the table"), &Options.StickNudge.V);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", textForLanguage("Отклоните влево, вправо или вверх. Для следующего толчка верните стик в центр.",
		                                       "Flick left, right or up. Return to center before the next nudge."));
	ImGui::Spacing();
	static int keyboardColumn = 0;
	constexpr int keyboardSlots[] = {0, 3, 4};
	ImGui::TextUnformatted(textForLanguage("Назначение клавиши", "Keyboard binding"));
	for (int i = 0; i < 3; ++i)
	{
		ImGui::SameLine();
		const char* labels[] = {"1", "2", "3"};
		ImGui::RadioButton(labels[i], &keyboardColumn, i);
	}
	if (ImGui::BeginTable("##bindings", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn(textForLanguage("Действие", "Action"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
		ImGui::TableSetupColumn(textForLanguage("Клавиши", "Keyboard"), ImGuiTableColumnFlags_WidthStretch, 1.15f);
		ImGui::TableSetupColumn(textForLanguage("Мышь", "Mouse"), ImGuiTableColumnFlags_WidthStretch, 1.f);
		ImGui::TableSetupColumn(textForLanguage("Геймпад", "Gamepad"), ImGuiTableColumnFlags_WidthStretch, 1.1f);
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		for (int col = 0; col < 4; ++col)
		{
			ImGui::TableSetColumnIndex(col);
			ImGui::TextDisabled("%s", ImGui::TableGetColumnName(col));
		}
		for (auto action = GameBindings::Min; action < GameBindings::Max; action++)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(~action);
			if (ImGui::Selectable(actionName(action), capture == action, ImGuiSelectableFlags_SpanAllColumns,
			                      ImVec2(0, ImGui::GetFrameHeight()))) BeginBinding(action, keyboardSlots[keyboardColumn]);
			for (int i = 0; i < 3; ++i)
			{
				ImGui::TableSetColumnIndex(i + 1);
				const auto& input = Options.Key[~action].Inputs[i == 0 ? keyboardSlots[keyboardColumn] : i];
				ImGui::TextUnformatted(input.Type == InputTypes::None ? "-" : input.GetShortInputDescription().c_str());
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	ImGui::Spacing();
	if (ImGui::Button(textForLanguage("Вернуть стандартные кнопки", "Restore default bindings")))
	{
		for (auto& control : Options.Key) control.Reset();
		CancelBinding();
		saveControls();
	}
}

void options::ShowControlDialog()
{
	ShowDialog = true;
	CancelBinding();
}

void options::RenderControlDialog()
{
	if (!ShowDialog) return;
	if (ImGui::Begin(pb::get_rc_string(Msg::KEYMAPPER_Caption), &ShowDialog)) DrawControlBindings();
	ImGui::End();
	if (!ShowDialog) CancelBinding();
}

std::vector<GameBindings> options::MapGameInput(GameInput key)
{
	if (key.Type == InputTypes::StickNudge && key.Value >= ~GameBindings::LeftTableBump &&
	    key.Value <= ~GameBindings::BottomTableBump)
		return {static_cast<GameBindings>(key.Value)};
	std::vector<GameBindings> result;
	for (auto inputId = GameBindings::Min; inputId < GameBindings::Max; inputId++)
	{
		for (auto& inputValue : Options.Key[~inputId].Inputs)
		{
			if (key == inputValue)
			{
				result.push_back(inputId);
				break;
			}
		}
	}
	return result;
}

void options::ResetAllOptions()
{
	for (const auto opt : AllOptions)
		opt->Reset();
	PostProcessOptions();
}

void options::MyUserData_ReadLine(ImGuiContext* ctx, ImGuiSettingsHandler* handler, void* entry, const char* line)
{
	auto& keyValueStore = *static_cast<std::unordered_map<std::string, std::string>*>(entry);
	std::string keyValue = line;
	auto separatorPos = keyValue.find('=');
	if (separatorPos != std::string::npos)
	{
		auto key = keyValue.substr(0, separatorPos);
		auto value = keyValue.substr(separatorPos + 1, keyValue.size());
		keyValueStore[key] = value;
	}
}

void* options::MyUserData_ReadOpen(ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name)
{
	// There is only one custom entry
	return strcmp(name, "Settings") == 0 ? &settings : nullptr;
}

void options::MyUserData_WriteAll(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf)
{
	buf->appendf("[%s][%s]\n", handler->TypeName, "Settings");
	for (const auto& setting : settings)
	{
		buf->appendf("%s=%s\n", setting.first.c_str(), setting.second.c_str());
	}
	buf->append("\n");
}

void options::PostProcessOptions()
{
	winmain::ImIO->FontGlobalScale = Options.UIScale;
	Options.FramesPerSecond = Clamp(Options.FramesPerSecond.V, MinFps, MaxFps);
	Options.UpdatesPerSecond = Clamp(Options.UpdatesPerSecond.V, MinUps, MaxUps);
	Options.UpdatesPerSecond = std::max(Options.UpdatesPerSecond.V, Options.FramesPerSecond.V);
	Options.SoundChannels = Clamp(Options.SoundChannels.V, MinSoundChannels, MaxSoundChannels);
	Options.SoundVolume = Clamp(Options.SoundVolume.V, MinVolume, MaxVolume);
	Options.MusicVolume = Clamp(Options.MusicVolume.V, MinVolume, MaxVolume);
	translations::SetCurrentLanguage(Options.Language.V.c_str());
	winmain::UpdateFrameRate();
}

std::string GameInput::GetFullInputDescription() const
{
	std::string prefix;
	switch (Type)
	{
	case InputTypes::Keyboard:
		prefix = "Keyboard\n";
		break;
	case InputTypes::Mouse:
		prefix = "Mouse\n";
		break;
	case InputTypes::GameController:
	case InputTypes::ControllerTrigger:
		prefix = "Controller\n";
		break;
	case InputTypes::None:
	default:
		return "Unused";
	}

	return prefix + GetShortInputDescription();
}

std::string GameInput::GetShortInputDescription() const
{
	static LPCSTR mouseButtons[]
	{
		nullptr,
		"Left",
		"Middle",
		"Right",
		"X1",
		"X2",
	};

	static LPCSTR controllerButtons[] =
	{
		"A",
		"B",
		"X",
		"Y",
		"View / Create",
		"Guide",
		"Start / Options",
		"LS / L3",
		"RS / R3",
		"LB / L1",
		"RB / R1",
		"D-pad Up",
		"D-pad Down",
		"D-pad Left",
		"D-pad Right",
		"Misc1",
		"Paddle1",
		"Paddle2",
		"Paddle3",
		"Paddle4",
		"Touchpad",
	};

	std::string keyName;
	switch (Type)
	{
	case InputTypes::Keyboard:
		switch (Value)
		{
		case SDLK_LSHIFT: return textForLanguage("Левый Shift", "Left Shift");
		case SDLK_RSHIFT: return textForLanguage("Правый Shift", "Right Shift");
		case SDLK_LCTRL: return textForLanguage("Левый Ctrl", "Left Ctrl");
		case SDLK_RCTRL: return textForLanguage("Правый Ctrl", "Right Ctrl");
		default: keyName = SDL_GetKeyName(Value); break;
		}
		break;
	case InputTypes::Mouse:
		if (Value == SDL_BUTTON_LEFT) keyName = textForLanguage("Левая", "Left");
		else if (Value == SDL_BUTTON_MIDDLE) keyName = textForLanguage("Средняя", "Middle");
		else if (Value == SDL_BUTTON_RIGHT) keyName = textForLanguage("Правая", "Right");
		else if (Value >= SDL_BUTTON_X1 && Value <= SDL_BUTTON_X2)
			keyName = mouseButtons[Value];
		else
			keyName = "MButton" + std::to_string(Value);
		break;
	case InputTypes::GameController:
		if (Value >= SDL_CONTROLLER_BUTTON_A && Value <= SDL_CONTROLLER_BUTTON_Y)
		{
			auto pad = controllers.find(navigationDevice);
			auto type = pad != controllers.end() ? SDL_GameControllerGetType(pad->second) : SDL_CONTROLLER_TYPE_UNKNOWN;
			if (type == SDL_CONTROLLER_TYPE_PS4 || type == SDL_CONTROLLER_TYPE_PS5 || type == SDL_CONTROLLER_TYPE_PS3)
			{
				const char* symbols[] = { textForLanguage("Крест", "Cross"), textForLanguage("Круг", "Circle"),
				                          textForLanguage("Квадрат", "Square"), textForLanguage("Треугольник", "Triangle") };
				return symbols[Value];
			}
		}
		if (Value >= SDL_CONTROLLER_BUTTON_A && Value < std::min(static_cast<int>(SDL_CONTROLLER_BUTTON_MAX), 21))
			keyName = controllerButtons[Value];
		else
			keyName = "CButton" + std::to_string(Value);
		break;
	case InputTypes::ControllerTrigger:
		keyName = Value == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? "LT / L2" : "RT / R2";
		break;
	case InputTypes::None:
	default:
		break;
	}

	return keyName;
}

const std::string& options::GetSetting(const std::string& key, const std::string& defaultValue)
{
	auto setting = settings.find(key);
	if (setting == settings.end())
	{
		auto inserted = settings.emplace(key, defaultValue);
		if (ImGui::GetCurrentContext())
			ImGui::MarkIniSettingsDirty();
		return inserted.first->second;
	}
	return setting->second;
}

void options::SetSetting(const std::string& key, const std::string& value)
{
	settings[key] = value;
	if (ImGui::GetCurrentContext())
		ImGui::MarkIniSettingsDirty();
}

OptionBase::OptionBase(LPCSTR name): Name(name)
{
	options::AllOptions.push_back(this);
}

OptionBase::~OptionBase()
{
	auto& vec = options::AllOptions;
	auto position = std::find(vec.begin(), vec.end(), this);
	if (position != vec.end())
		vec.erase(position);
}

std::string ControlOption::GetShortcutDescription() const
{
	std::string result;
	for (const auto& input : Inputs)
	{
		if (input.Type != InputTypes::None)
		{
			result = input.GetShortInputDescription();
			break;
		}
	}
	return result;
}
