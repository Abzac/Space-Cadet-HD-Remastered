#include "pch.h"
#include "winmain.h"

#include "control.h"
#include "EmbeddedData.h"
#include "fullscrn.h"
#include "midi.h"
#include "options.h"
#include "pb.h"
#include "render.h"
#include "Sound.h"
#include "translations.h"
#include "font_selection.h"
#include "HdrOutput.h"
#include "GameMenu.h"
#include "ResumeOverlay.h"
#include "ResumeBallCue.h"
#include "StickInput.h"
#include "VerifyGame.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TTextBox.h"
#include "score.h"
#include "timer.h"
#include "nudge.h"

namespace
{
	constexpr int clearFieldStage = -2;
	constexpr Uint64 digitMs = 700, digitsEnd = 3 * digitMs, goEnd = digitsEnd + 400;
	constexpr Uint64 clearFieldMs = 300, cueStart = 1650;
	int resumeStage = -1;
	float stageProgress = 0.f;
	float cueProgress = -1.f;
	Uint64 resumeStarted = 0;
	Uint64 clearFrameTime = 0;
	bool clearFrameShown = false;
	bool resumeOnFocus = false;
	bool nativePauseMessage = false;

	void releaseForPause()
	{
		// Releasing a held launch key here would arm a shot on the first live tick.
		if (pb::MainTable && pb::MainTable->Plunger) pb::MainTable->Plunger->CancelUserPull();
		options::ReleaseActiveInputs();
	}

	const char* wording(const char* russian, const char* english)
	{
		return translations::GetCurrentLanguage()->Language == Lang::Russian ? russian : english;
	}

	SDL_Rect usableDisplay()
	{
		SDL_Rect bounds{0,0,1280,720};
		int display=SDL_GetWindowDisplayIndex(winmain::MainWindow);
		SDL_GetDisplayUsableBounds(std::max(0,display),&bounds);
		int top=0,left=0,bottom=0,right=0;
		SDL_GetWindowBordersSize(winmain::MainWindow,&top,&left,&bottom,&right);
		bounds.w-=left+right+12; bounds.h-=std::max(38,top)+bottom+12;
		return bounds;
	}
	void applyWindowSize(int width, int height)
	{
		auto window = winmain::MainWindow;
		const auto& table = fullscrn::resolution_array[fullscrn::GetResolution()];
		if (!width)
		{
			auto choices=winmain::WindowSizes();
			const float target=usableDisplay().w*.36f;
			auto first=std::lower_bound(choices.begin(),choices.end(),900);
			if (first==choices.end()) first=choices.begin();
			width=*std::min_element(first,choices.end(),[target](int a, int b) { return std::abs(a-target)<std::abs(b-target); });
			height=0;
		}
		width = std::max(320, std::min(width, 7680));
		if (height <= 0) height = (width * table.TableHeight + table.TableWidth / 2) / table.TableWidth;
		height = std::max(240, std::min(height, 7680));
		SDL_Rect bounds{};
		int display = SDL_GetWindowDisplayIndex(window);
		if (SDL_GetDisplayUsableBounds(display >= 0 ? display : 0, &bounds) == 0)
		{
			int top=0,left=0,bottom=0,right=0;
			if (SDL_GetWindowBordersSize(window,&top,&left,&bottom,&right) != 0 || top==0) top=38;
			float fit = std::min(1.f, std::min((bounds.w-left-right-8)/float(width), (bounds.h-top-bottom-8)/float(height)));
			if (fit>0) { width = int(width*fit); height = int(height*fit); }
		}
		SDL_SetWindowSize(window,width,height);
		if (display >= 0) SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED_DISPLAY(display),SDL_WINDOWPOS_CENTERED_DISPLAY(display));
		fullscrn::window_size_changed();
	}
}
std::vector<int> winmain::WindowSizes()
{
	const auto& table=fullscrn::resolution_array[fullscrn::GetResolution()];
	auto bounds=usableDisplay();
	std::vector<int> sizes;
	for (int halfSteps=2;halfSteps<=24;++halfSteps)
	{
		int w=halfSteps*table.TableWidth/2;
		if (w>bounds.w || halfSteps*table.TableHeight/2>bounds.h) break;
		sizes.push_back(w);
	}
	if (sizes.empty()) sizes.push_back(std::max(1,std::min(bounds.w,bounds.h*table.TableWidth/table.TableHeight)));
	return sizes;
}
void winmain::CycleWindowSize()
{
	if (Options.FullScreen)
	{
		options::toggle(Menu1::Full_Screen);
		SetWindowSize(0);
		return;
	}
	auto sizes=WindowSizes();
	int width=0;
	SDL_GetWindowSize(MainWindow,&width,nullptr);
	auto it=std::upper_bound(sizes.begin(),sizes.end(),width+1);
	if (it==sizes.end()) options::toggle(Menu1::Full_Screen);
	else { SDL_RestoreWindow(MainWindow); SetWindowSize(*it); }
}

void winmain::SetWindowSize(int width, int height)
{
	applyWindowSize(width,height);
}

void winmain::UpdateWindowTitle()
{
	SDL_SetWindowTitle(MainWindow, pb::FairMode ? wording("Space Cadet HD 7.0.2 — Медитативный режим","Space Cadet HD 7.0.2 — Meditative mode") :
		wording("Space Cadet HD 7.0.2 — Классика","Space Cadet HD 7.0.2 — Classic"));
}

constexpr const char* winmain::Version;

SDL_Window* winmain::MainWindow = nullptr;
SDL_Renderer* winmain::Renderer = nullptr;
ImGuiIO* winmain::ImIO = nullptr;

int winmain::return_value = 0;
bool winmain::bQuit = false;
bool winmain::activated = false;
bool winmain::DispFrameRate = false;
bool winmain::DispGRhistory = false;
bool winmain::single_step = false;
bool winmain::has_focus = true;
int winmain::last_mouse_x;
int winmain::last_mouse_y;
int winmain::mouse_down;
bool winmain::no_time_loss = false;

bool winmain::restart = false;

std::vector<float> winmain::gfrDisplay{};
unsigned winmain::gfrOffset = 0;
float winmain::gfrWindow = 5.0f;
bool winmain::ShowAboutDialog = false;
bool winmain::ShowImGuiDemo = false;
bool winmain::ShowSpriteViewer = false;
bool winmain::ShowExitPopup = false;
bool winmain::LaunchBallEnabled = true;
bool winmain::HighScoresEnabled = true;
bool winmain::DemoActive = false;
int winmain::MainMenuHeight = 0;
std::string winmain::FpsDetails, winmain::PrevSdlError;
unsigned winmain::PrevSdlErrorCount = 0;
double winmain::UpdateToFrameRatio;
winmain::DurationMs winmain::TargetFrameTime;
optionsStruct& winmain::Options = options::Options;
winmain::DurationMs winmain::SpinThreshold = DurationMs(0.005);
WelfordState winmain::SleepState{};
int winmain::CursorIdleCounter = 0;

int winmain::WinMain(LPCSTR lpCmdLine)
{
	std::set_new_handler(memalloc_failure);

	printf("Game version: %s\n", Version);
	printf("Command line: %s\n", lpCmdLine);
	printf("Compiled with: SDL %d.%d.%d;", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
	printf(" SDL_mixer %d.%d.%d;", SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_PATCHLEVEL);
	printf(" ImGui %s %s\n", IMGUI_VERSION, ImGuiRender);

	// SDL init
	const char* verifyEnv = strstr(lpCmdLine, "-verify") ? SDL_getenv("SPACECADET_VERIFY_DIR") : nullptr;
	const std::string verifyPath = verifyEnv ? verifyEnv : "";
	const char* verifyDir = verifyPath.empty() ? nullptr : verifyPath.c_str();
	if (verifyDir) SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	SDL_SetMainReady();
	options::ConfigureControllers();
#ifdef _WIN32
	SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
	SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "direct3d11", SDL_HINT_DEFAULT);
	SDL_SetHint(SDL_HINT_RENDER_DIRECT3D_THREADSAFE, "1");
#endif
	if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO |
		SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0)
	{
		pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not initialize SDL2", SDL_GetError());
		return 1;
	}

	pb::quickFlag = strstr(lpCmdLine, "-quick") != nullptr;

	// SDL window
	SDL_Window* window = SDL_CreateWindow
	(
		pb::get_rc_string(Msg::STRING139),
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		800, 556,
		SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE
	);
	MainWindow = window;
	if (!window)
	{
		pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not create window", SDL_GetError());
		return 1;
	}

	// If HW fails, fallback to SW SDL renderer.
	SDL_Renderer* renderer = nullptr;
	auto swOffset = strstr(lpCmdLine, "-sw") != nullptr ? 1 : 0;
	for (int i = swOffset; i < 2 && !renderer; i++)
	{
		Renderer = renderer = SDL_CreateRenderer
		(
			window,
			-1,
			i == 0 ? SDL_RENDERER_ACCELERATED : SDL_RENDERER_SOFTWARE
		);
	}
	if (!renderer)
	{
		pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not create renderer", SDL_GetError());
		return 1;
	}
	SDL_RendererInfo rendererInfo{};
	if (!SDL_GetRendererInfo(renderer, &rendererInfo))
		printf("Using SDL renderer: %s\n", rendererInfo.name);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

	auto prefPath = verifyDir ? SDL_strdup(verifyDir) : SDL_GetPrefPath("", "SpaceCadetHD");
	auto basePath = SDL_GetBasePath();
	const std::string packagedData = std::string(basePath) + "Data" + PathSeparator;

	// SDL mixer init
	bool mixOpened = false, noAudio = strstr(lpCmdLine, "-noaudio") != nullptr;
	if (!noAudio)
	{
		if (verifyDir)
		{
			// Windows native MIDI opens a separate WinMM device even when SDL
			// audio is dummy. Exercise the MIDI parser and playback clock on
			// the software mixer with an empty, deliberately silent patch bank.
			SDL_setenv("SDL_MIXER_DISABLE_NATIVEMIDI", "1", 1);
			SDL_setenv("SDL_NATIVE_MUSIC", "0", 1);
			const auto bank = std::string(verifyDir) + PathSeparator + "verify-timidity.cfg";
			if (auto file = fopenu(bank.c_str(), "wb"))
			{
				fputs("bank 0\n", file);
				fclose(file);
			}
			SDL_setenv("TIMIDITY_CFG", bank.c_str(), 1);
		}
		if ((Mix_Init(MIX_INIT_MID_Proxy) & MIX_INIT_MID_Proxy) == 0)
		{
			printf("Could not initialize SDL MIDI, music might not work.\nSDL Error: %s\n", SDL_GetError());
			SDL_ClearError();
		}
		// Sum overlapping sounds in float before converting to the device format.
		if (Mix_OpenAudioDevice(MIX_DEFAULT_FREQUENCY, AUDIO_F32SYS, 2, 1024, nullptr, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE) != 0 &&
			Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, MIX_DEFAULT_FORMAT, 2, 1024) != 0)
		{
			printf("Could not open audio device, continuing without audio.\nSDL Error: %s\n", SDL_GetError());
			SDL_ClearError();
		}
		else
			mixOpened = true;
	}

	{
		// Load SDL Game Controller definitions from DB
		unsigned decompressedSize{};
		const auto controllerDb = ImFontAtlas::DecompressCompressedStbData(
			EmbeddedData::SDL_GameControllerDB_compressed_data,
			EmbeddedData::SDL_GameControllerDB_compressed_size,
			decompressedSize);
		auto rw = SDL_RWFromMem(controllerDb, decompressedSize);
		const auto added = SDL_GameControllerAddMappingsFromRW(rw, 1);
		IM_FREE(controllerDb);
		if (added < 0)
		{
			printf("Could not load game controller DB.\nSDL Error: %s\n", SDL_GetError());
			SDL_ClearError();
		}
	}

	auto resetAllOptions = strstr(lpCmdLine, "-reset") != nullptr;
	do
	{
		restart = false;

		// ImGui init
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		ImIO = &io;
		auto iniPath = std::string(prefPath) + PathSeparator + "imgui_pb.ini";
		io.IniFilename = iniPath.c_str();

		// First option initialization step: just load settings from .ini. Needs ImGui context.
		options::InitPrimary();
		if (resetAllOptions)
		{
			resetAllOptions = false;
			options::ResetAllOptions();
		}
		if (verifyDir)
		{
			Options.FairRules = strstr(lpCmdLine, "-meditative") != nullptr || strstr(lpCmdLine, "-fair") != nullptr;
			Options.FullScreen = false;
		}

		ImVector<ImWchar> ranges;
		GameMenu::SetTitleFont(nullptr);
		GameMenu::SetBodyFont(nullptr);
		if (!Options.FontFileName.V.empty())
		{
			translations::GetGlyphRange(&ranges);
			ImFontConfig fontConfig{};

			// ToDo: further tweak font options, maybe try imgui_freetype
			fontConfig.OversampleV = 2;
			fontConfig.OversampleH = 4;

			// ToDo: improve font file test, checking if file exists is not enough
			auto fontLoaded = false;
			auto fileName = Options.FontFileName.V.c_str();
			auto fileHandle = fopenu(fileName, "rb");
			if (fileHandle)
			{
				fclose(fileHandle);

				// ToDo: Bind font size to UI scale
				if (io.Fonts->AddFontFromFileTTF(fileName, 17.f, &fontConfig, ranges.Data))
					fontLoaded = true;
#ifdef _WIN32
				const char* heading = "C:/Windows/Fonts/bahnschrift.ttf";
				if (auto file = fopenu(heading, "rb"))
				{
					fclose(file);
					GameMenu::SetTitleFont(io.Fonts->AddFontFromFileTTF(heading,42.f,&fontConfig,ranges.Data));
					if (Options.FontFileName.V==Options.FontFileName.DefaultValue)
						GameMenu::SetBodyFont(io.Fonts->AddFontFromFileTTF(heading,18.f,&fontConfig,ranges.Data));
				}
#endif
			}

			if (!fontLoaded)
				printf("Failed to load font: %s, using embedded font.\n", fileName);
			io.Fonts->Build();
		}
		ImGui_Render_Init(renderer);
		ImGui::StyleColorsDark();
		GameMenu::Style();

		ImGui_ImplSDL2_InitForSDLRenderer(window, Renderer);
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

		// Data search order: WD, executable path, user pref path, platform specific paths.
		std::vector<const char*> searchPaths
		{
			{
				packagedData.c_str(),
				basePath,
				"",
				prefPath
			}
		};
		searchPaths.insert(searchPaths.end(), std::begin(PlatformDataPaths), std::end(PlatformDataPaths));
		pb::SelectDatFile(searchPaths);

		// Second step: run updates that depend on .DAT file selection
		options::InitSecondary();
		pb::FairMode = Options.FairRules;
		render::EnhancedGraphics = Options.EnhancedArt;

		Sound::Init(mixOpened, Options.SoundChannels, Options.Sounds, Options.SoundVolume);
		if (!mixOpened)
			Options.Sounds = false;

		if (!midi::music_init(mixOpened, Options.MusicVolume))
			Options.Music = false;

		if (pb::init())
		{
			std::string message = "The .dat file is missing.\n"
				"Make sure that the game data is present in any of the following locations:\n";
			for (auto path : searchPaths)
			{
				if (path)
				{
					message = message + (path[0] ? path : "working directory") + "\n";
				}
			}
			pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, "Could not load game data", message.c_str());
			return 1;
		}

		fullscrn::init();

		pb::reset_table();
		pb::firsttime_setup();

		if (strstr(lpCmdLine, "-fullscreen"))
		{
			Options.FullScreen = true;
		}

		SetWindowSize(Options.WindowWidth, Options.WindowHeight);
		UpdateWindowTitle();
		if (!verifyDir || strstr(lpCmdLine, "-showtest")) SDL_ShowWindow(window);
		if (!fullscrn::set_screen_mode(Options.FullScreen)) Options.FullScreen = false;

		if (strstr(lpCmdLine, "-demo"))
			pb::toggle_demo();
		else
			pb::replay_level(false);

		if (verifyDir)
			return_value = VerifyGame(verifyDir,mixOpened,RenderUi) ? 0 : 2;
		else
			MainLoop();

		HdrOutput::Close();
		if (!Options.FullScreen && !(SDL_GetWindowFlags(window)&(SDL_WINDOW_MINIMIZED|SDL_WINDOW_MAXIMIZED)))
			SDL_GetWindowSize(window,&Options.WindowWidth.V,&Options.WindowHeight.V);
		options::uninit();
		midi::music_shutdown();
		Sound::Close();
		pb::uninit();

		GameMenu::ReleaseArt();
		ImGui_Render_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
	}
	while (restart);

	if (!noAudio)
	{
		if (mixOpened)
			Mix_CloseAudio();
		Mix_Quit();
	}

	SDL_free(basePath);
	SDL_free(prefPath);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return return_value;
}

void winmain::MainLoop()
{
	bQuit = false;
	unsigned updateCounter = 0, frameCounter = 0;
	auto frameStart = Clock::now();
	double UpdateToFrameCounter = 0;
	DurationMs sleepRemainder(0), frameDuration(TargetFrameTime);
	auto prevTime = frameStart;
	const auto cabinetStarted = SDL_GetTicks64();
	Uint64 lastBackgroundFrame = 0;

	while (true)
	{
		if (DispFrameRate)
		{
			auto curTime = Clock::now();
			if (curTime - prevTime > DurationMs(1000))
			{
				char buf[60];
				auto elapsedSec = DurationMs(curTime - prevTime).count() * 0.001;
				snprintf(buf, sizeof buf, "Updates/sec = %02.02f Frames/sec = %02.02f ",
				         updateCounter / elapsedSec, frameCounter / elapsedSec);
				SDL_SetWindowTitle(MainWindow, buf);
				FpsDetails = buf;
				frameCounter = updateCounter = 0;
				prevTime = curTime;
			}
		}

		if (!ProcessWindowMessages() || bQuit)
			break;

		UpdateResumeCountdown(SDL_GetTicks64());
		if (!has_focus && BackgroundArtVisible())
		{
			const auto now = SDL_GetTicks64();
			if (now - lastBackgroundFrame >= 66)
			{
				RenderFrame(float(double(now - cabinetStarted) * .001));
				lastBackgroundFrame = now;
			}
		}
		if (has_focus)
		{
			if (mouse_down && !single_step)
			{
				int x, y, w, h;
				SDL_GetMouseState(&x, &y);
				SDL_GetWindowSize(MainWindow, &w, &h);
				float dx = static_cast<float>(last_mouse_x - x) / static_cast<float>(w);
				float dy = static_cast<float>(y - last_mouse_y) / static_cast<float>(h);
				pb::ballset(dx, dy);

				// Original creates continuous mouse movement with mouse capture.
				// Alternative solution: mouse warp at window edges.
				int xMod = 0, yMod = 0;
				if (x == 0 || x >= w - 1)
					xMod = w - 2;
				if (y == 0 || y >= h - 1)
					yMod = h - 2;
				if (xMod != 0 || yMod != 0)
				{
					// Mouse warp does not work over remote desktop or in some VMs
					x = abs(x - xMod);
					y = abs(y - yMod);
					SDL_WarpMouseInWindow(MainWindow, x, y);
				}

				last_mouse_x = x;
				last_mouse_y = y;
			}
			if (CanAdvanceGame() && !no_time_loss)
			{
				auto dt = static_cast<float>(frameDuration.count());
				pb::frame(dt);
				if (DispGRhistory)
				{
					auto targetSize = static_cast<unsigned>(static_cast<float>(Options.UpdatesPerSecond) * gfrWindow);
					if (gfrDisplay.size() != targetSize)
					{
						gfrDisplay.resize(targetSize, static_cast<float>(TargetFrameTime.count()));
						gfrOffset = 0;
					}
					gfrDisplay[gfrOffset] = dt;
					gfrOffset = (gfrOffset + 1) % gfrDisplay.size();
				}
				updateCounter++;
			}
			no_time_loss = false;

			if (UpdateToFrameCounter >= UpdateToFrameRatio)
			{
				RenderFrame(float(double(SDL_GetTicks64() - cabinetStarted) * .001));
				if (resumeStage == clearFieldStage) ResumeFramePresented(SDL_GetTicks64());
				frameCounter++;
				UpdateToFrameCounter -= UpdateToFrameRatio;
			}

			auto sdlError = SDL_GetError();
			if (sdlError[0] || !PrevSdlError.empty())
			{
				if (sdlError[0])
					SDL_ClearError();

				// Rate limit duplicate SDL error messages.
				if (sdlError != PrevSdlError)
				{
					PrevSdlError = sdlError;
					if (PrevSdlErrorCount > 0)
					{
						printf("SDL Error: ^ Previous Error Repeated %u Times\n", PrevSdlErrorCount + 1);
						PrevSdlErrorCount = 0;
					}

					if (sdlError[0])
						printf("SDL Error: %s\n", sdlError);
				}
				else
				{
					PrevSdlErrorCount++;
				}
			}

			auto updateEnd = Clock::now();
			auto targetTimeDelta = TargetFrameTime - DurationMs(updateEnd - frameStart) - sleepRemainder;

			TimePoint frameEnd;
			if (targetTimeDelta > DurationMs::zero() && !Options.UncappedUpdatesPerSecond)
			{
				if (Options.HybridSleep)
					HybridSleep(targetTimeDelta);
				else
					std::this_thread::sleep_for(targetTimeDelta);
				frameEnd = Clock::now();
			}
			else
			{
				frameEnd = updateEnd;
			}

			// Limit duration to 2 * target time
			sleepRemainder = Clamp(DurationMs(frameEnd - updateEnd) - targetTimeDelta, -TargetFrameTime,
			                       TargetFrameTime);
			frameDuration = std::min<DurationMs>(DurationMs(frameEnd - frameStart), 2 * TargetFrameTime);
			frameStart = frameEnd;
			UpdateToFrameCounter++;

			CursorIdleCounter = std::max(CursorIdleCounter - static_cast<int>(frameDuration.count()), 0);
		}
	}

	if (PrevSdlErrorCount > 0)
	{
		printf("SDL Error: ^ Previous Error Repeated %u Times\n", PrevSdlErrorCount);
	}
}

bool winmain::CabinetMenuOpen() { return GameMenu::IsOpen(); }
void winmain::ToggleCabinetMenu() { GameMenu::Toggle(); }

bool winmain::BackgroundArtVisible()
{
	const auto flags = SDL_GetWindowFlags(MainWindow);
	return render::EnhancedGraphics && (flags & SDL_WINDOW_SHOWN) && !(flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED));
}

void winmain::RenderFrame(float decorTime)
{
	// A Windows placement change can reach the swap chain before the queued size event.
	fullscrn::window_size_changed();
	if (has_focus && Options.HideCursor && CursorIdleCounter <= 0)
		ImGui::SetMouseCursor(ImGuiMouseCursor_None);
	ImGui_ImplSDL2_NewFrame(has_focus);
	ImGui_Render_NewFrame();
	ImGui::NewFrame();
	RenderUi();

	SDL_RenderClear(Renderer);
	SDL_RenderFillRect(Renderer, nullptr);
	render::PresentVScreen(decorTime);
	ImGui::Render();
	ImGui_Render_RenderDrawData(ImGui::GetDrawData());
	if (!HdrOutput::Present(MainWindow, Renderer, Options.AutoHDR && render::EnhancedGraphics))
		SDL_RenderPresent(Renderer);
}

void winmain::RenderUi()
{
	gdrv::grtext_draw_ttext_in_box();
	auto panel = pb::MissTextBox;
	if (panel && !pb::FullTiltMode && !GameMenu::IsOpen())
	{
		// The original Windows cabinet ends ten native pixels above the bottom
		// of the framebuffer. The strip below its metal bezel belongs to the hints.
		const int bottom = render::vscreen->Height;
		const auto strip = fullscrn::GetScreenRectFromPinballRect({panel->OffsetX, bottom-10, panel->Width, 10});
		const auto scale = ImGui::GetIO().DisplayFramebufferScale;
		if (strip.h > 0 && scale.x > 0.f && scale.y > 0.f)
		{
			auto font = ImGui::GetFont();
			auto hint = wording("F1 Помощь    Esc Меню    F4 / F7 Экран",
			                    "F1 Help    Esc Menu    F4 / F7 Display");
			const float width = strip.w / scale.x;
			const float height = strip.h / scale.y;
			const float natural = font->CalcTextSizeA(1.f, FLT_MAX, 0.f, hint).x;
			const float size = std::min(height * .98f, (width - 2.f) / natural);
			const auto right = (strip.x + strip.w) / scale.x;
			const auto top = strip.y / scale.y;
			auto draw = ImGui::GetBackgroundDrawList();
			draw->PushClipRect({strip.x / scale.x, top}, {right, top + height}, true);
			draw->AddText(font, size, {floorf(right - natural * size), top + (height - size) * .5f},
			              IM_COL32(181, 193, 206, 235), hint);
			draw->PopClipRect();
		}
	}
	GameMenu::Draw();
	if (resumeStage >= 0 && !GameMenu::IsOpen())
	{
		ResumeOverlay::Draw(resumeStage, stageProgress, translations::GetCurrentLanguage()->Language == Lang::Russian);
		if (cueProgress >= 0.f) ResumeBallCue::Draw(cueProgress);
	}
}

int winmain::event_handler(const SDL_Event* event)
{
	GameInput input;
	bool down = false;
	SDL_JoystickID device = -1;
	switch (event->type)
	{
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		if (event->key.repeat) return 1;
		input = {InputTypes::Keyboard, event->key.keysym.sym};
		down = event->type == SDL_KEYDOWN;
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		CursorIdleCounter = 1000;
		input = {InputTypes::Mouse, event->button.button};
		down = event->type == SDL_MOUSEBUTTONDOWN;
		if (down && event->button.button==SDL_BUTTON_LEFT && event->button.clicks==2 &&
		    !CabinetMenuOpen() && !options::WaitingForInput() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
		    pb::MainTable && pb::MainTable->CurScoreStruct && !pb::FullTiltMode)
		{
			const int edge=pb::MainTable->Width;
			auto poster=fullscrn::GetScreenRectFromPinballRect({edge,0,render::vscreen->Width-edge,pb::MainTable->CurScoreStruct->OffsetY});
			auto scale=ImIO->DisplayFramebufferScale;
			SDL_Point point{int(event->button.x*scale.x),int(event->button.y*scale.y)};
			if (SDL_PointInRect(&point,&poster)) { options::toggle(Menu1::Full_Screen); return 1; }
		}
		break;
	case SDL_MOUSEMOTION:
	case SDL_MOUSEWHEEL:
		CursorIdleCounter = 1000;
		break;
	case SDL_CONTROLLERDEVICEADDED:
		options::ControllerAdded(event->cdevice.which);
		return 1;
	case SDL_CONTROLLERDEVICEREMOVED:
		options::ControllerRemoved(event->cdevice.which);
		return 1;
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
		device = event->cbutton.which;
		options::ControllerUsed(device);
		input = {InputTypes::GameController, event->cbutton.button};
		down = event->type == SDL_CONTROLLERBUTTONDOWN;
		if (GameMenu::ScoreShoulder(event->cbutton.button, down)) return 1;
		break;
	case SDL_CONTROLLERAXISMOTION:
	{
		device = event->caxis.which;
		const auto controller=SDL_GameControllerFromInstanceID(device);
		if (!controller || !SDL_GameControllerGetAttached(controller))
		{
			StickInput::Remove(device);
			return 1;
		}
		StickInput::Axis(device,event->caxis.axis,event->caxis.value,has_focus && !single_step && !CabinetMenuOpen() &&
			!options::WaitingForInput() && !DemoActive && !ImIO->WantTextInput);
		if (std::abs(int(event->caxis.value)) > 17000) options::ControllerUsed(device);
		if (options::TriggerChanged(device, event->caxis.axis, event->caxis.value, down))
			input = {InputTypes::ControllerTrigger, event->caxis.axis};
		break;
	}
	}

	if (!has_focus && down && input.Type != InputTypes::None) return 1;
	if (options::WaitingForInput() && input.Type != InputTypes::None && down)
	{
		options::InputDown(input);
		return 1;
	}
	ImGui_ImplSDL2_ProcessEvent(event);
	const bool mouse = input.Type == InputTypes::Mouse;
	const bool uiWantsInput = mouse ? ImIO->WantCaptureMouse :
		(ImIO->WantTextInput || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
	if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_SPACE &&
	    !uiWantsInput && !CabinetMenuOpen() && single_step && nativePauseMessage && !ResumePending())
	{
		RequestResume();
		return 1;
	}
	if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F1 &&
	    !ImIO->WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
	{
		GameMenu::Help();
		return 1;
	}
	if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_SPACE && !uiWantsInput &&
	    !CabinetMenuOpen() && !single_step && pb::MainTable &&
	    (pb::game_mode == GameModes::GameOver || pb::MainTable->LightShowTimer))
	{
		new_game();
		return 1;
	}
	if (input.Type != InputTypes::None)
	{
		if (down && !CabinetMenuOpen() && uiWantsInput) return 1;
		const bool cheatDrag = pb::cheat_mode && mouse && event->button.button == SDL_BUTTON_LEFT;
		if (!cheatDrag) options::RouteInput(input, device, down, !uiWantsInput || CabinetMenuOpen());
	}
	switch (event->type)
	{
	case SDL_QUIT:
		options::ReleaseActiveInputs();
		if (pb::MainTable && pb::MainTable->EndGameTimeoutTimer)
		{
			timer::kill(pb::MainTable->EndGameTimeoutTimer);
			TPinballTable::EndGame_timeout(0, pb::MainTable);
		}
		bQuit = true;
		fullscrn::shutdown();
		return_value = 0;
		return 0;
	case SDL_KEYDOWN:
		if (!pb::cheat_mode || CabinetMenuOpen()) break;
		switch (event->key.keysym.sym)
		{
		case SDLK_g:
			DispGRhistory ^= true;
			break;
		case SDLK_o:
			{
				auto plt = new ColorRgba[4 * 256];
				auto pltPtr = &plt[10]; // first 10 entries are system colors hardcoded in display_palette()
				for (int i1 = 0, i2 = 0; i1 < 256 - 10; ++i1, i2 += 8)
				{
					unsigned char blue = i2, redGreen = i2;
					if (i2 > 255)
					{
						blue = 255;
						redGreen = i1;
					}

					*pltPtr++ = ColorRgba{blue, redGreen, redGreen, 0};
				}
				gdrv::display_palette(plt);
				delete[] plt;
			}
			break;
		case SDLK_y:
			SDL_SetWindowTitle(MainWindow, "Pinball");
			DispFrameRate ^= true;
			break;
		case SDLK_F10:
			pause();
			break;
		default:
			break;
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (event->button.button == SDL_BUTTON_LEFT && pb::cheat_mode && !CabinetMenuOpen())
		{
			mouse_down = 1;
			last_mouse_x = event->button.x;
			last_mouse_y = event->button.y;
			SDL_SetWindowGrab(MainWindow, SDL_TRUE);
		}
		break;
	case SDL_MOUSEBUTTONUP:
		if (event->button.button == SDL_BUTTON_LEFT && mouse_down)
		{
			mouse_down = 0;
			SDL_SetWindowGrab(MainWindow, SDL_FALSE);
		}
		break;
	case SDL_WINDOWEVENT:
		if (event->window.windowID && event->window.windowID != SDL_GetWindowID(MainWindow)) break;
		switch (event->window.event)
		{
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			activated = true;
			has_focus = true;
			no_time_loss = true;
			if (resumeOnFocus && !CabinetMenuOpen())
				RequestResume();
			else if (!single_step)
			{
				Sound::Activate();
				if (Options.Music) midi::music_play();
			}
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
		case SDL_WINDOWEVENT_HIDDEN:
		case SDL_WINDOWEVENT_MINIMIZED:
			if (!has_focus) break;
			resumeOnFocus = !CabinetMenuOpen() && (!single_step || ResumePending());
			resumeStage = -1;
			releaseForPause();
			pb::loose_focus();
			if (resumeOnFocus) single_step = true;
			activated = false;
			Sound::Deactivate();
			midi::music_stop();
			has_focus = false;
			break;
		case SDL_WINDOWEVENT_SIZE_CHANGED:
		case SDL_WINDOWEVENT_RESIZED:
			fullscrn::window_size_changed();
			break;
		case SDL_WINDOWEVENT_MAXIMIZED:
		{
			const auto flags = SDL_GetWindowFlags(MainWindow);
			if (!Options.FullScreen && (flags & SDL_WINDOW_MAXIMIZED) && !(flags & SDL_WINDOW_FULLSCREEN))
				options::toggle(Menu1::Full_Screen);
			break;
		}
		default: ;
		}
		break;
	default: break;
	}
	return 1;
}

int winmain::ProcessWindowMessages()
{
	static auto idleWait = 0;
	SDL_Event event;
	if (has_focus)
	{
		idleWait = static_cast<int>(TargetFrameTime.count());
		while (SDL_PollEvent(&event))
		{
			if (!event_handler(&event))
				return 0;
		}

		return 1;
	}

	// Progressively wait longer when transitioning to idle
	idleWait = std::min(idleWait + static_cast<int>(TargetFrameTime.count()), BackgroundArtVisible() ? 66 : 500);
	if (SDL_WaitEventTimeout(&event, idleWait))
	{
		idleWait = static_cast<int>(TargetFrameTime.count());
		return event_handler(&event);
	}
	return 1;
}

void winmain::memalloc_failure()
{
	midi::music_stop();
	Sound::Close();
	const char* caption = pb::get_rc_string(Msg::STRING270);
	const char* text = pb::get_rc_string(Msg::STRING279);
	pb::ShowMessageBox(SDL_MESSAGEBOX_ERROR, caption, text);
	std::exit(1);
}

void winmain::a_dialog()
{
	if (ShowAboutDialog == true)
	{
		ShowAboutDialog = false;
		ImGui::OpenPopup(pb::get_rc_string(Msg::STRING204));
	}

	bool unused_open = true;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{ 600, 300 });
	if (ImGui::BeginPopupModal(pb::get_rc_string(Msg::STRING204), &unused_open, ImGuiWindowFlags_None))
	{
		if (ImGui::BeginTabBar("AboutTabBar", ImGuiTabBarFlags_None))
		{
			if (ImGui::BeginTabItem("3DPB"))
			{
				ImGui::TextUnformatted(pb::get_rc_string(Msg::STRING139));
				ImGui::TextUnformatted("Original game by Cinematronics, Microsoft");
				ImGui::Separator();

				ImGui::TextUnformatted("Decompiled -> Ported to SDL");
				ImGui::Text("Version %s", Version);
				if (ImGui::SmallButton("Project home: https://github.com/k4zmu2a/SpaceCadetPinball"))
				{
#if SDL_VERSION_ATLEAST(2, 0, 14)
					// Relatively new feature, skip with older SDL
					SDL_OpenURL("https://github.com/k4zmu2a/SpaceCadetPinball");
#endif
				}
				ImGui::EndTabItem();
			}
			ImGui::PushStyleColor(ImGuiCol_Button, 0);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0);
			if (ImGui::BeginTabItem("Full Tilt!"))
			{
				const ImVec2 buttonCenter = { -1, 0 };
				ImGui::Button("Full Tilt! was created by Cinematronics for Maxis.", buttonCenter);
				ImGui::Button("Version 1.1", buttonCenter);

				auto tableRow = [](LPCSTR textA, LPCSTR textB)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(textA);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(textB);
				};
				if (ImGui::BeginTable("Full Tilt!", 2))
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::Button("Cinematronics", buttonCenter);
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("PROGRAMMING", "ART");
						tableRow("Michael Sandige", "John Frantz");
						tableRow("John Taylor", "Ryan Medeiros");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("DESIGN", "SOUND EFFECTS");
						tableRow("Kevin Gliner", "Matt Ridgeway");
						tableRow(nullptr, "Donald S. Griffin");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("DESIGN CONSULTANT", "MUSIC");
						tableRow("Mark Sprenger", "Matt Ridgeway");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("PRODUCER", "VOICES");
						tableRow("Kevin Gliner", "Mike McGeary");
						tableRow(nullptr, "William Rice");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("GRAND POOBAH", nullptr);
						tableRow("David Stafford", nullptr);
						ImGui::EndTable();
					}
					ImGui::Separator();
					ImGui::Button("SPECIAL THANKS", buttonCenter);
					if (ImGui::BeginTable("Cinematronics", 2))
					{
						tableRow("Paula Sandige", "Alex St. John");
						tableRow("Brad Silverberg", "Jeff Camp");
						tableRow("Danny Thorpe", "Greg Hospelhorn");
						tableRow("Keith Johnson", "Sean Grant");
						tableRow("Bob McAnn", "Michael Kelley");
						tableRow("Rob Rosenhouse", "Lisa Acton");
						ImGui::EndTable();
					}
					ImGui::TextUnformatted("Dan and Mitchell Roth");

					ImGui::TableNextColumn();
					ImGui::Button("Maxis", buttonCenter);
					ImGui::Separator();
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("PRODUCER", "PRODUCT MANAGER");
						tableRow("John Csicsery", "Larry Lee");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("LEAD TESTER", "QA MANAGER");
						tableRow("Scott Shicoff", "Scott Shicoff");
						ImGui::EndTable();
					}
					ImGui::Separator();
					ImGui::Button("ADDITIONAL TESTING", buttonCenter);
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("Cathy Castro", "Robin Hines");
						tableRow("John \"Jussi\" Ylinen", "Keith Meyer");
						tableRow("Marc Meyer", "Owen Nelson");
						tableRow("Joe Longworth", "Peter Saylor");
						tableRow("Michael Gilmartin", "Robin Hines");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("ADDITIONAL ART", "ART DIRECTOR");
						tableRow("Ocean Quigley", "Sharon Barr");
						tableRow("Rick Macaraeg", "INSTALL PROGRAM");
						tableRow("Charlie Aquilina", "Kevin O'Hare");
						ImGui::EndTable();
					}
					ImGui::Separator();
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("INTRO MUSIC", "DOCUMENTATION");
						tableRow("Brian Conrad", "David Caggiano");
						tableRow("John Csicsery", "Michael Bremer");
						tableRow(nullptr, "Bob Sombrio");
						ImGui::EndTable();
					}
					ImGui::Separator();
					ImGui::Button("SPECIAL THANKS", buttonCenter);
					if (ImGui::BeginTable("Maxis", 2))
					{
						tableRow("Sam Poole", "Joe Scirica");
						tableRow("Jeff Braun", "Bob Derber");
						tableRow("Ashley Csicsery", "Tom Forge");
						ImGui::EndTable();
					}
					ImGui::Button("Will \"Burr\" Wright", buttonCenter);
					ImGui::EndTable();
				}

				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
			ImGui::PopStyleColor(3);
		}

		ImGui::Separator();
		if (ImGui::Button("Ok"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();
}

bool winmain::CanAdvanceGame() { return has_focus && !single_step; }
int winmain::ResumeStage() { return resumeStage; }
bool winmain::ResumePending() { return resumeStage != -1; }

void winmain::ResumeFramePresented(Uint64 now)
{
	if (resumeStage == clearFieldStage && !clearFrameShown)
	{
		clearFrameTime = now;
		clearFrameShown = true;
	}
}

bool winmain::PauseForMenu()
{
	const bool shouldResume = !single_step || ResumePending() || resumeOnFocus;
	resumeStage = -1;
	resumeOnFocus = false;
	releaseForPause();
	pb::loose_focus();
	single_step = true;
	Sound::Deactivate();
	midi::music_stop();
	return shouldResume;
}

void winmain::FinishPause()
{
	if (nativePauseMessage)
	{
		pb::pause_continue();
		nativePauseMessage = false;
	}
	else
	{
		single_step = false;
		if (has_focus)
		{
			Sound::Activate();
			if (Options.Music) midi::music_play();
		}
	}
	no_time_loss = true;
}

void winmain::RequestResume()
{
	if (!single_step || CabinetMenuOpen()) return;
	if (!has_focus) { resumeOnFocus = true; return; }
	resumeOnFocus = false;
	if (pb::game_mode != GameModes::InGame || !Options.ResumeCountdown || !control::HasDeployedBall())
	{
		resumeStage = -1;
		FinishPause();
		return;
	}
	resumeStarted = SDL_GetTicks64();
	resumeStage = 3;
	stageProgress = 0.f;
	cueProgress = -1.f;
	clearFrameShown = false;
}

void winmain::UpdateResumeCountdown(Uint64 now)
{
	if (!ResumePending() || !has_focus || CabinetMenuOpen()) return;
	if (resumeStage == clearFieldStage)
	{
		if (clearFrameShown && now >= clearFrameTime && now - clearFrameTime >= clearFieldMs)
		{
			resumeStage = -1;
			FinishPause();
			no_time_loss = false;
			options::ResumeHeldFlippers();
		}
		return;
	}
	const Uint64 elapsed = now >= resumeStarted ? now - resumeStarted : 0;
	cueProgress = elapsed >= cueStart && elapsed < goEnd ? float(elapsed - cueStart) / float(goEnd - cueStart) : -1.f;
	if (elapsed < digitsEnd)
	{
		resumeStage = 3 - int(elapsed / digitMs);
		stageProgress = float(elapsed % digitMs) / float(digitMs);
	}
	else if (elapsed < goEnd)
	{
		resumeStage = 0;
		stageProgress = float(elapsed - digitsEnd) / float(goEnd - digitsEnd);
	}
	else resumeStage = clearFieldStage;
}

void winmain::new_game()
{
	options::ReleaseActiveInputs();
	resumeStage = -1;
	resumeOnFocus = false;
	if (single_step) FinishPause();
	if (pb::FairMode != Options.FairRules.V)
	{
		auto table = pb::MainTable;
		if (table->EndGameTimeoutTimer)
		{
			timer::kill(table->EndGameTimeoutTimer);
			TPinballTable::EndGame_timeout(0, table);
		}
		high_score::write();
		nudge::reset();
		table->Message(MessageCode::Reset, 0);
		pb::FairMode = Options.FairRules.V;
		high_score::read();
		UpdateWindowTitle();
	}
	pb::replay_level(false);
}

void winmain::pause(bool toggle)
{
	if (CabinetMenuOpen()) return;
	if (!single_step || ResumePending())
	{
		resumeStage = -1;
		resumeOnFocus = false;
		releaseForPause();
		if (!nativePauseMessage)
		{
			single_step = false;
			pb::pause_continue();
			nativePauseMessage = true;
		}
		no_time_loss = true;
	}
	else if (toggle) RequestResume();
}

void winmain::Restart()
{
	restart = true;
	SDL_Event event{SDL_QUIT};
	SDL_PushEvent(&event);
}

void winmain::UpdateFrameRate()
{
	// UPS >= FPS
	auto fps = Options.FramesPerSecond.V, ups = Options.UpdatesPerSecond.V;
	UpdateToFrameRatio = static_cast<double>(ups) / fps;
	TargetFrameTime = DurationMs(1000.0 / ups);
}

void winmain::HandleGameBinding(GameBindings binding, bool shortcut)
{
	switch (binding)
	{
	case GameBindings::TogglePause:
		StickInput::Cancel();
		pause();
		break;
	case GameBindings::NewGame:
		new_game();
		break;
	case GameBindings::ToggleFullScreen:
		options::toggle(Menu1::Full_Screen);
		break;
	case GameBindings::CycleWindowSize:
		CycleWindowSize();
		break;
	case GameBindings::ToggleSounds:
		options::toggle(Menu1::Sounds);
		break;
	case GameBindings::ToggleMusic:
		options::toggle(Menu1::Music);
		break;
	case GameBindings::ShowControlDialog:
		GameMenu::Controls();
		break;
	case GameBindings::ToggleMenuDisplay:
		ToggleCabinetMenu();
		break;
	case GameBindings::Exit:
		GameMenu::QuitConfirmation();
		break;
	default:
		break;
	}
}

void winmain::RenderFrameTimeDialog()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2{300, 70});
	if (ImGui::Begin("Frame Times", &DispGRhistory, ImGuiWindowFlags_NoScrollbar))
	{
		auto target = static_cast<float>(TargetFrameTime.count());
		auto yMax = target * 2;

		auto spin = Options.HybridSleep ? static_cast<float>(SpinThreshold.count()) : 0;
		ImGui::Text("YMin:0ms, Target frame time:%03.04fms, YMax:%03.04fms, SpinThreshold:%03.04fms",
		            target, yMax, spin);

		static bool scrollPlot = true;
		ImGui::Checkbox("Scroll Plot", &scrollPlot);

		ImGui::SameLine();
		ImGui::SliderFloat("Window Size", &gfrWindow, 0.1f, 15, "%.3fsec", ImGuiSliderFlags_AlwaysClamp);

		{
			float average = 0.0f, dev = 0.0f;
			for (auto n : gfrDisplay)
			{
				average += n;
				dev += std::abs(target - n);
			}
			average /= static_cast<float>(gfrDisplay.size());
			dev /= static_cast<float>(gfrDisplay.size());
			char overlay[64];
			sprintf(overlay, "avg %.3fms, dev %.3fms", average, dev);

			auto region = ImGui::GetContentRegionAvail();
			ImGui::PlotLines("Lines", gfrDisplay.data(), static_cast<int>(gfrDisplay.size()),
			                 scrollPlot ? gfrOffset : 0, overlay, 0, yMax, region);
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

void winmain::HybridSleep(DurationMs sleepTarget)
{
	static constexpr double StdDevFactor = 0.5;

	// This nice concept is from https://blat-blatnik.github.io/computerBear/making-accurate-sleep-function/
	// Sacrifices some CPU time for smaller frame time jitter
	while (sleepTarget > SpinThreshold)
	{
		auto start = Clock::now();
		std::this_thread::sleep_for(DurationMs(1));
		auto end = Clock::now();

		auto actualDuration = DurationMs(end - start);
		sleepTarget -= actualDuration;

		// Update expected sleep duration using Welford's online algorithm
		// With bad timer, this will run away to 100% spin
		SleepState.Advance(actualDuration.count());
		SpinThreshold = DurationMs(SleepState.mean + SleepState.GetStdDev() * StdDevFactor);
	}

	// spin lock
	for (auto start = Clock::now(); DurationMs(Clock::now() - start) < sleepTarget;);
}

void winmain::ImGuiMenuItemWShortcut(GameBindings binding, bool selected)
{
	const auto& keyDef = Options.Key[~binding];
	if (ImGui::MenuItem(pb::get_rc_string(keyDef.Description), keyDef.GetShortcutDescription().c_str(), selected))
	{
		HandleGameBinding(binding, false);
	}
}
