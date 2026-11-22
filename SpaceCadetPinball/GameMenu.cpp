#include "pch.h"
#include "GameMenu.h"
#include "HdrOutput.h"
#include "winmain.h"
#include "options.h"
#include "fullscrn.h"
#include "render.h"
#include "pb.h"
#include "Sound.h"
#include "midi.h"
#include "translations.h"
#include "TTextBox.h"
#include "CabinetDisplay.h"
#include "CabinetMenuArt.h"

namespace
{
	enum class Page { Home, Settings, Controls, Scores, Record, ClearScores, Quit, Help };
	Page page = Page::Home, beforeHelp=Page::Home;
	ImFont* titleFont=nullptr, *bodyFont=nullptr;
	bool helpFromGame=false, resetScroll=false;
	using Icon=cabinetMenuArt::Icon;
	bool open = false, focusNext = false, menuPausedGame = false, scoreMode = false;
	bool focusScoreTab = false;
	high_score_struct displayedScores[5]{};
	const ImVec4 gold{1.0f, .74f, .36f, 1};
	const ImVec4 muted{.67f,.72f,.8f,1};
	const char* tr(const char* ru,const char* en)
	{
		return translations::GetCurrentLanguage()->Language == Lang::Russian ? ru : en;
	}
	void go(Page next)
	{
		options::CancelBinding();
		page=next; focusNext=true; resetScroll=true;
		if (next==Page::Scores) focusScoreTab=true;
	}
	void resume(bool keepPriorPause=false)
	{
		options::CancelBinding();
		open=false;
		if (menuPausedGame || !keepPriorPause) winmain::RequestResume();
		ImGui::SetWindowFocus(nullptr);
	}
	void openHome()
	{
		// Freeze game time without replacing the player's current mission text.
		menuPausedGame=winmain::PauseForMenu();
		open=true; go(Page::Home);
	}
	void primaryFocus()
	{
		if (focusNext) { ImGui::SetKeyboardFocusHere(); focusNext=false; }
	}
	float keyHeight()
	{
		return Clamp(ImGui::GetIO().DisplaySize.y*.067f,31.f,53.f);
	}
	bool button(const char* label, bool primary=false, float width=-1, Icon icon=Icon::None)
	{
		const float height=keyHeight();
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha,0.f);
		const bool clicked=ImGui::Button(label,ImVec2(width,height));
		ImGui::PopStyleVar();
		const bool focused=ImGui::IsItemFocused() && !ImGui::GetCurrentContext()->NavDisableHighlight;
		const bool hot=focused || ImGui::IsItemHovered();
		const auto p=ImGui::GetItemRectMin(), q=ImGui::GetItemRectMax();
		auto dl=ImGui::GetWindowDrawList();
		const ImU32 upper=primary ? (hot?IM_COL32(255,216,155,255):IM_COL32(250,201,128,255))
			: (hot?IM_COL32(62,82,98,255):IM_COL32(42,54,70,255));
		const ImU32 lower=primary ? IM_COL32(218,146,77,255)
			: (hot?IM_COL32(36,58,73,255):IM_COL32(28,38,52,255));
		const float cut=6.f, cy=(q.y+p.y)*.5f;
		const ImVec2 outline[]={{p.x+cut,p.y},{q.x,p.y},{q.x,q.y-cut},{q.x-cut,q.y},{p.x,q.y},{p.x,p.y+cut}};
		const int first=dl->VtxBuffer.Size;
		dl->AddConvexPolyFilled(outline,6,IM_COL32_WHITE);
		ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl,first,dl->VtxBuffer.Size,p,{p.x,q.y},upper,lower);
		dl->AddPolyline(outline,6,hot?cabinetMenuArt::amber:IM_COL32(107,119,131,190),ImDrawFlags_Closed,hot?1.7f:1.f);
		dl->AddLine({p.x+7,p.y+2},{q.x-2,p.y+2},primary?IM_COL32(255,230,184,165):IM_COL32(153,163,177,52));
		const auto fg=primary ? IM_COL32(43,31,34,255):IM_COL32(228,234,239,255);
		const float inset=icon!=Icon::None ? 37.f : 13.f;
		if (icon!=Icon::None) cabinetMenuArt::symbol(dl,icon,{p.x+19,cy},Clamp(height/40.f,.86f,1.13f),primary?fg:cabinetMenuArt::ice);
		auto font=ImGui::GetFont();
		float size=std::min(ImGui::GetFontSize(),(q.x-p.x-inset-6)/std::max(.01f,font->CalcTextSizeA(1.f,FLT_MAX,0.f,label).x));
		dl->AddText(font,size,{p.x+inset,cy-size*.5f},fg,label);
		return clicked;
	}
	void hint(const char* text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text,muted);
		ImGui::PushTextWrapPos(0.f); ImGui::TextUnformatted(text); ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
	}
	void play()
	{
		bool newGame=pb::FairMode!=options::Options.FairRules.V || pb::game_mode==GameModes::GameOver;
		resume();
		if (newGame) winmain::new_game();
	}
	void displayAndSound()
	{
		auto& opt=options::Options;
		const auto language=translations::GetCurrentLanguage()->Language;
		ImGui::TextUnformatted(tr("Язык","Language"));
		if (ImGui::RadioButton("Русский",language==Lang::Russian) && language!=Lang::Russian) GameMenu::SetLanguage("ru");
		ImGui::SameLine();
		if (ImGui::RadioButton("English",language==Lang::English) && language!=Lang::English) GameMenu::SetLanguage("en");
		ImGui::Spacing();
		if (ImGui::Checkbox(tr("Таймер после паузы","Resume countdown"),&opt.ResumeCountdown.V))
		{
			opt.ResumeCountdown.Save();
			if (ImGui::GetIO().IniFilename) ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
		}
		ImGui::Spacing();
		int currentWidth, currentHeight;
		SDL_GetWindowSize(winmain::MainWindow,&currentWidth,&currentHeight);
		const auto widths=winmain::WindowSizes();
		char current[32]; SDL_snprintf(current,sizeof(current),"%d × %d",currentWidth,currentHeight);
		ImGui::TextUnformatted(tr("Размер окна","Window size"));
		ImGui::SetNextItemWidth(-1);
		if (ImGui::BeginCombo("##size",current))
		{
			for (int width : widths)
			{
				const auto& t=fullscrn::resolution_array[fullscrn::GetResolution()];
				char label[58]; SDL_snprintf(label,sizeof(label),"%d × %d   (%.1f×)",width,width*t.TableHeight/t.TableWidth,float(width)/t.TableWidth);
				if (ImGui::Selectable(label,!opt.FullScreen && currentWidth==width))
				{
					if (opt.FullScreen) options::toggle(Menu1::Full_Screen);
					SDL_RestoreWindow(winmain::MainWindow); winmain::SetWindowSize(width);
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::Button(tr("Следующий размер окна","Next window size"))) winmain::CycleWindowSize();
		bool fullscreen=opt.FullScreen;
		if (ImGui::Checkbox(tr("Полный экран","Fullscreen"),&fullscreen)) options::toggle(Menu1::Full_Screen);
		if (ImGui::Checkbox(tr("HD-графика и освещение","HD graphics and lighting"),&opt.EnhancedArt.V))
			render::EnhancedGraphics=opt.EnhancedArt;
		ImGui::BeginDisabled(!opt.EnhancedArt);
		ImGui::Checkbox(tr("Сглаживание контуров","Smooth object edges"),&opt.SmoothSilhouettes.V);
		ImGui::EndDisabled();
#if defined(_WIN32) && defined(_MSC_VER)
		ImGui::Checkbox(tr("HDR при поддержке экрана","HDR on compatible displays"),&opt.AutoHDR.V);
		hint(HdrOutput::Active() ? tr("Сейчас: HDR · scRGB","Output: HDR · scRGB") : tr("Сейчас: SDR","Output: SDR"));
#endif
		ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
		bool sounds=opt.Sounds, music=opt.Music;
		if (ImGui::Checkbox(tr("Звуки стола","Table sounds"),&sounds)) options::toggle(Menu1::Sounds);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderInt("##sound",&opt.SoundVolume.V,0,MIX_MAX_VOLUME)) Sound::SetVolume(opt.SoundVolume);
		if (ImGui::Checkbox(tr("Музыка","Music"),&music)) options::toggle(Menu1::Music);
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderInt("##music",&opt.MusicVolume.V,0,MIX_MAX_VOLUME)) midi::SetVolume(opt.MusicVolume);
		if (music && midi::HasInstrumentChoice())
		{
			const char* banks[] = {tr("Исходный MIDI", "Original MIDI"), "GeneralUser GS"};
			int bank = opt.GeneralUserMusic ? 1 : 0;
			ImGui::TextUnformatted(tr("Банк инструментов", "Instrument bank"));
			ImGui::SetNextItemWidth(-1);
			if (ImGui::Combo("##musicBank", &bank, banks, 2))
				midi::SelectGeneralUser(bank == 1);
		}
	}
	void selectScores(bool meditative)
	{
		scoreMode = meditative;
		high_score::view(meditative, displayedScores);
	}
	void scoreRows()
	{
		const float width = ImGui::GetContentRegionAvail().x;
		const float height = Clamp(ImGui::GetIO().DisplaySize.y * .064f, 31.f, 58.f);
		const float pitch = Clamp(width / 166.f, 1.5f, 2.7f);
		auto list = ImGui::GetWindowDrawList();
		for (int i = 0; i < 5; ++i)
		{
			const auto& row = displayedScores[i];
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float end = p.x + width, middle = p.y + height * .5f;
			const bool winner = i == 0 && row.Score > 0;
			list->AddRectFilled(p, {end, p.y + height}, winner ? IM_COL32(41,35,31,255) : IM_COL32(16,23,35,255), 4.f);
			if (winner) list->AddRectFilled(p, {p.x+3, p.y+height}, IM_COL32(250,187,94,255));
			char rank[4]; SDL_snprintf(rank, sizeof(rank), "%02d", i+1);
			const float textY = middle - ImGui::GetFontSize() * .5f;
			list->AddText({p.x+10, textY}, winner ? IM_COL32(250,192,115,255) : IM_COL32(133,151,176,255), rank);
			if (row.Score > 0)
			{
				const auto digits = std::to_string(row.Score);
				float nameX = p.x+std::max(42.f, ImGui::CalcTextSize("00").x+20);
				list->PushClipRect({nameX,p.y},{end-19-digits.size()*6*pitch,p.y+height},true);
				list->AddText({nameX,textY},IM_COL32(230,235,245,255),row.Name);
				list->PopClipRect();
				CabinetDisplay::DrawNumber(digits,end-10,middle,pitch,
					winner ? IM_COL32(255,204,127,255) : IM_COL32(166,192,248,255));
			}
			else
				list->AddLine({end-25,middle},{end-11,middle}, IM_COL32(77,92,111,255), 1.f);
			ImGui::Dummy({width,height});
		}
	}
	void scores()
	{
		const float tabWidth = (ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*.5f;
		for (int i=0; i<2; ++i)
		{
			if (i) ImGui::SameLine();
			const auto label = i ? tr("Медитативный","Meditative") : tr("Классика","Classic");
			ImGui::PushStyleColor(ImGuiCol_Button, scoreMode==bool(i) ? ImVec4(.20f,.24f,.31f,1) : ImVec4(.10f,.13f,.18f,1));
			if (focusScoreTab && scoreMode==bool(i)) { ImGui::SetKeyboardFocusHere(); focusScoreTab=false; }
			if (ImGui::Button(label,{tabWidth,0}))
				selectScores(bool(i));
			ImGui::PopStyleColor();
			if (scoreMode==bool(i))
			{
				auto end = ImGui::GetItemRectMax();
				ImGui::GetWindowDrawList()->AddLine({end.x-tabWidth+1,end.y-1},{end.x-1,end.y-1},IM_COL32(251,190,109,255),2);
			}
		}
		ImGui::Spacing();
		scoreRows();
		if (auto pad = CabinetNavigationController())
		{
			const auto type = SDL_GameControllerGetType(pad);
			const bool playStation = type==SDL_CONTROLLER_TYPE_PS4 || type==SDL_CONTROLLER_TYPE_PS5 || type==SDL_CONTROLLER_TYPE_PS3;
			ImGui::TextDisabled("%s   ·   %s", playStation ? "L1 / R1" : "LB / RB", tr("Сменить вкладку","Switch tab"));
		}
		if (displayedScores[0].Score < 0) hint(tr("Завершите партию, чтобы записать первый рекорд.","Finish a game to set the first high score."));
		else if (ImGui::SmallButton(tr("Очистить таблицу","Clear this board"))) go(Page::ClearScores);
	}
	void endRecord(bool save)
	{
		high_score::finish_name(save);
		selectScores(pb::FairMode);
		go(high_score::pending() ? Page::Record : Page::Scores);
	}
	std::string binding(GameBindings action, int device)
	{
		const auto& input=options::Options.Key[~action].Inputs[device];
		if (input.Type==InputTypes::None) return "—";
		if (input==GameInput{InputTypes::Keyboard, SDLK_SPACE}) return tr("Пробел","Space");
		if (input==GameInput{InputTypes::Keyboard, SDLK_UP}) return tr("вверх","Up");
		if (input.Type==InputTypes::GameController && input.Value>=SDL_CONTROLLER_BUTTON_A && input.Value<=SDL_CONTROLLER_BUTTON_Y)
		{
			const char* names[]={tr("A / крест","A / Cross"),tr("B / круг","B / Circle"),
				tr("X / квадрат","X / Square"),tr("Y / треуг.","Y / Triangle")};
			return names[input.Value];
		}
		return input.GetShortInputDescription();
	}
	std::string nudgeInputs(int device)
	{
		const auto& keys=options::Options.Key;
		if (device==2 &&
			keys[~GameBindings::LeftTableBump].Inputs[2]==GameInput{InputTypes::GameController, SDL_CONTROLLER_BUTTON_DPAD_LEFT} &&
			keys[~GameBindings::RightTableBump].Inputs[2]==GameInput{InputTypes::GameController, SDL_CONTROLLER_BUTTON_DPAD_RIGHT} &&
			keys[~GameBindings::BottomTableBump].Inputs[2]==GameInput{InputTypes::GameController, SDL_CONTROLLER_BUTTON_DPAD_UP})
			return tr("Крестовина","D-pad");
		std::string buttons;
		for(auto action : {GameBindings::LeftTableBump,GameBindings::RightTableBump,GameBindings::BottomTableBump})
		{
			if (keys[~action].Inputs[device].Type==InputTypes::None) continue;
			if (!buttons.empty()) buttons+=" / ";
			buttons+=binding(action,device);
		}
		return buttons.empty() ? "—" : buttons;
	}
	void helpSheet()
	{
		ImGui::TextWrapped("%s",tr("Удерживайте кнопку пуска и отпустите. Отбивайте шар флипперами.",
			"Hold the launch button and release. Keep the ball in play with the flippers."));
		ImGui::Spacing();
		const auto label=tr("Действие","Action");
		if (ImGui::BeginTable("##quick-reference",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_PadOuterX|ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn(label,ImGuiTableColumnFlags_WidthStretch,1.48f);
			ImGui::TableSetupColumn(tr("Клавиши","Keyboard"),ImGuiTableColumnFlags_WidthStretch,1.05f);
			ImGui::TableSetupColumn(tr("Мышь","Mouse"),ImGuiTableColumnFlags_WidthStretch,.83f);
			ImGui::TableSetupColumn(tr("Геймпад","Gamepad"),ImGuiTableColumnFlags_WidthStretch,1.17f);
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
			for(int col=0;col<4;++col)
			{
				ImGui::TableSetColumnIndex(col);
				ImGui::TextColored(gold,"%s",ImGui::TableGetColumnName(col));
			}
			for(auto action : {GameBindings::LeftFlipper,GameBindings::RightFlipper,GameBindings::Plunger})
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextWrapped("%s",action==GameBindings::Plunger ? tr("Пуск шара","Launch ball") :
					action==GameBindings::LeftFlipper ? tr("Левый флиппер","Left flipper") : tr("Правый флиппер","Right flipper"));
				for(int device=0;device<3;++device)
				{
					ImGui::TableSetColumnIndex(device+1);
					ImGui::TextWrapped("%s",binding(action,device).c_str());
				}
			}
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(tr("Толчки стола","Nudges"));
			for(int device=0;device<3;++device)
			{
				ImGui::TableSetColumnIndex(device+1);
				ImGui::TextWrapped("%s",nudgeInputs(device).c_str());
			}
			ImGui::EndTable();
		}
		std::string sticks;
		if(options::Options.StickPlunger) sticks=tr("Правый стик вниз и отпустить — пуск.","Pull down and release the right stick to launch.");
		if(options::Options.StickNudge)
		{
			if (!sticks.empty()) sticks+=" ";
			sticks+=tr("Левый стик — толчок.","Flick the left stick to nudge.");
		}
		if (!sticks.empty()) hint(sticks.c_str());
		ImGui::Spacing();
		ImGui::TextColored(gold,"%s",tr("Миссии","Missions"));
		ImGui::TextWrapped("%s",tr("Попадите в мишень выбора миссии, затем на стартовую рампу. Следуйте стрелкам и табло, чтобы набирать очки и повышать звание.",
			"Hit a mission target, then shoot the launch ramp. Follow the arrows and display to score points and earn promotions."));
		ImGui::Spacing();
		const auto pause=binding(GameBindings::TogglePause,0), full=binding(GameBindings::ToggleFullScreen,0), cycle=binding(GameBindings::CycleWindowSize,0);
		ImGui::TextWrapped("%s", (pause+tr(" — пауза, пробел — продолжить.\n"," pauses, Space resumes.\n")+full+tr(" полный экран     "," fullscreen     ")+cycle+
			tr(" размер окна"," window size")).c_str());
	}
	void record()
	{
		auto entry = high_score::pending();
		if (!entry) { GameMenu::Scores(); return; }
		ImGui::TextColored(gold, "%s   ·   %s %d",
			pb::FairMode ? tr("Медитативный","Meditative") : tr("Классика","Classic"),
			tr("Место","Rank"), entry->Position+1);
		auto p = ImGui::GetCursorScreenPos();
		auto w = ImGui::GetContentRegionAvail().x;
		ImGui::GetWindowDrawList()->AddRectFilled(p,{p.x+w,p.y+52},IM_COL32(19,25,36,255),4.f);
		CabinetDisplay::DrawNumber(std::to_string(entry->Entry.Score),p.x+w-14,p.y+26,3.7f,IM_COL32(255,203,118,255));
		ImGui::Dummy({w,52});
		ImGui::Spacing();
		ImGui::TextUnformatted(tr("Имя пилота","Pilot name"));
		primaryFocus();
		ImGui::SetNextItemWidth(-1);
		const bool submitted = ImGui::InputText("##pilot",entry->Entry.Name,sizeof(entry->Entry.Name),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (button(tr("Сохранить имя","Save name"),true) || submitted) endRecord(true);
	}
}
void GameMenu::SetLanguage(const char* code)
{
	if (strcmp(code,"ru") && strcmp(code,"en")) return;
	translations::SetCurrentLanguage(code);
	options::Options.Language.V=code;
	options::Options.Language.Save();
	if (ImGui::GetIO().IniFilename) ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
	for (auto box : {pb::InfoTextBox,pb::MissTextBox})
		if (box)
			for (auto message=box->CurrentMessage;message;message=message->NextMessage) message->Translate();
	winmain::UpdateWindowTitle();
}
void GameMenu::Style()
{
	auto& s=ImGui::GetStyle();
	s.WindowRounding=0; s.ChildRounding=0; s.FrameRounding=2; s.PopupRounding=3;
	s.WindowBorderSize=0; s.PopupBorderSize=1; s.FrameBorderSize=1; s.FramePadding=ImVec2(9,4);
	s.ItemSpacing=ImVec2(8,6); s.CellPadding=ImVec2(4,3); s.ButtonTextAlign=ImVec2(.07f,.5f);
	s.ScrollbarSize=8; s.GrabRounding=1;
	auto* c=s.Colors;
	c[ImGuiCol_Text]=ImVec4(.94f,.95f,.97f,1);
	c[ImGuiCol_WindowBg]=ImVec4(.055f,.068f,.10f,.99f);
	c[ImGuiCol_PopupBg]=ImVec4(.08f,.10f,.14f,1);
	c[ImGuiCol_Button]=ImVec4(.13f,.16f,.22f,1);
	c[ImGuiCol_ButtonHovered]=c[ImGuiCol_HeaderHovered]=ImVec4(.20f,.25f,.33f,1);
	c[ImGuiCol_ButtonActive]=c[ImGuiCol_HeaderActive]=ImVec4(.25f,.30f,.39f,1);
	c[ImGuiCol_FrameBg]=ImVec4(.073f,.106f,.143f,1); c[ImGuiCol_FrameBgHovered]=c[ImGuiCol_ButtonHovered];
	c[ImGuiCol_Header]=ImVec4(.18f,.28f,.35f,1);
	c[ImGuiCol_Border]=ImVec4(.34f,.39f,.44f,.8f);
	c[ImGuiCol_TableHeaderBg]=ImVec4(.12f,.17f,.22f,1);
	c[ImGuiCol_TableRowBg]=ImVec4(.043f,.071f,.106f,.92f);
	c[ImGuiCol_TableRowBgAlt]=ImVec4(.10f,.14f,.19f,.92f);
	c[ImGuiCol_Separator]=ImVec4(.39f,.48f,.54f,.65f);
	c[ImGuiCol_TextDisabled]=ImVec4(.67f,.75f,.81f,1);
	c[ImGuiCol_CheckMark]=c[ImGuiCol_SliderGrab]=c[ImGuiCol_NavHighlight]=gold;
	c[ImGuiCol_SliderGrabActive]=ImVec4(1,.86f,.55f,1);
}
void GameMenu::Scores() { if (!open) openHome(); selectScores(pb::FairMode); go(Page::Scores); }
bool GameMenu::ScoreShoulder(Uint8 button, bool pressed)
{
	if (!open || page != Page::Scores ||
		(button != SDL_CONTROLLER_BUTTON_LEFTSHOULDER && button != SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) return false;
	if (pressed) { selectScores(!scoreMode); focusScoreTab=true; }
	return true;
}
bool GameMenu::ViewingMeditativeScores() { return open && page==Page::Scores && scoreMode; }
void GameMenu::SetTitleFont(ImFont* font) { titleFont=font; }
void GameMenu::SetBodyFont(ImFont* font) { bodyFont=font; }
void GameMenu::ReleaseArt() { cabinetMenuArt::release(); }
void GameMenu::Help()
{
	if (open && page==Page::Help)
	{
		if (helpFromGame) resume(true);
		else go(beforeHelp);
		return;
	}
	helpFromGame=!open;
	if (!open) openHome();
	beforeHelp=page;
	go(Page::Help);
}
bool GameMenu::IsHelpOpen() { return open && page==Page::Help; }
void GameMenu::QuitConfirmation() { if (!open) openHome(); go(Page::Quit); }
void GameMenu::Toggle()
{
	if (!open) openHome();
	else if (page==Page::Help) Help();
	else if (page==Page::Record) endRecord(false);
	else if (page==Page::ClearScores) go(Page::Scores);
	else if (page!=Page::Home) go(Page::Home);
	else resume(true);
}
void GameMenu::Controls() { if (!open) openHome(); go(Page::Controls); }
bool GameMenu::IsOpen() { return open; }

void GameMenu::Draw()
{
	if (high_score::pending() && (!open || (page!=Page::Record && !(page==Page::Help && beforeHelp==Page::Record))))
	{
		if (!open) openHome();
		go(Page::Record);
	}
	if (!open) return;
	// ImGui 1.89 sends Enter to editors and Space to buttons. Also activate
	// the highlighted menu item on either Enter, once per physical press.
	auto& nav=*ImGui::GetCurrentContext();
	if (nav.NavId && !nav.ActiveId && !nav.NavDisableHighlight && !options::WaitingForInput() &&
	    (ImGui::IsKeyPressed(ImGuiKey_Enter,false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter,false)))
	{
		nav.NavInputSource=ImGuiInputSource_Keyboard;
		nav.NavActivateId=nav.NavActivateDownId=nav.NavActivatePressedId=nav.NavId;
		nav.NavActivateInputId=nav.NavId;
	}
	const ImVec2 size=ImGui::GetIO().DisplaySize;
	if (size.x<=0 || size.y<=0) return;
	const bool controls=page==Page::Controls, help=page==Page::Help;
	const float minWidth=controls ? 580.f : help ? 446.f : 330.f;
	const float panelWidth=std::min(size.x,Clamp(size.x*(controls?.64f:help?.50f:.405f),
		minWidth,controls?830.f:help?650.f:565.f));
	float edge=size.x-panelWidth;
	cabinetMenuArt::bezel(edge,panelWidth,size.y);
	ImGui::SetNextWindowBgAlpha(0.f);
	ImGui::SetNextWindowPos(ImVec2(edge,0)); ImGui::SetNextWindowSize(ImVec2(panelWidth,size.y));
	const float padding=Clamp(panelWidth*.054f,23.f,34.f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(padding, Clamp(size.y*.042f,16.f,52.f)));
	ImGui::Begin("##FlightMenu",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|
		ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoCollapse);
	if (resetScroll) { ImGui::SetScrollY(0); resetScroll=false; }
	if(bodyFont) ImGui::PushFont(bodyFont);
	ImGui::SetWindowFontScale(Clamp(size.y/640.f,.90f,1.20f));
	if (page==Page::Home)
	{
		cabinetMenuArt::masthead("SPACE CADET",titleFont,true);
		const bool change=pb::FairMode!=options::Options.FairRules.V;
		ImGui::Dummy(ImVec2(0,Clamp(size.y*.015f,4.f,18.f)));
		primaryFocus();
		const char* mode=tr("Медитативный","Meditative");
		float modeWidth=ImGui::CalcTextSize(mode).x+ImGui::GetFrameHeight()+ImGui::GetStyle().ItemInnerSpacing.x;
		float buttonWidth=std::max(76.f,ImGui::GetContentRegionAvail().x-modeWidth-ImGui::GetStyle().ItemSpacing.x-2);
		const bool newGame=change || pb::game_mode==GameModes::GameOver;
		if (button(newGame ? tr("Играть","Play") : tr("Продолжить","Resume"),true,buttonWidth,Icon::Play)) play();
		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY()+std::max(0.f,(keyHeight()-ImGui::GetFrameHeight())*.5f));
		if (ImGui::Checkbox(mode,&options::Options.FairRules.V))
		{
			options::Options.FairRules.Save();
			if (ImGui::GetIO().IniFilename) ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s",tr("Боковые пружины, полный бак и последний шанс у кончика флиппера.",
				"Reusable kickbacks, a full fuel tank and a last-chance flipper shot."));
		if (button(tr("Настройки","Settings"),false,-1,Icon::Settings)) go(Page::Settings);
		if (button(tr("Управление","Controls"),false,-1,Icon::Controls)) go(Page::Controls);
		if (button(tr("Рекорды","High scores"),false,-1,Icon::Scores)) Scores();
		if (button(tr("Помощь","Help"),false,-1,Icon::Help)) Help();
		ImGui::Spacing();
		if (button(tr("Выйти из игры","Quit game"),false,-1,Icon::Power)) go(Page::Quit);
		if (change) hint(tr("Смена режима начнёт новую партию.","Changing mode will start a new game."));
		else if (pb::game_mode==GameModes::GameOver) hint(tr("Пробел — новая игра","Space — new game"));
		cabinetMenuArt::starChart();
	}
	else
	{
		const char* header = page==Page::Settings ? tr("Настройки","Settings") : page==Page::Controls ? tr("Управление","Controls") :
			page==Page::Scores ? tr("Рекорды","High scores") : page==Page::Record ? tr("Новый рекорд","New high score") :
			page==Page::ClearScores ? tr("Очистить таблицу?","Clear this board?") :
			page==Page::Help ? tr("Как играть","How to play") : tr("Выход","Quit");
		cabinetMenuArt::masthead(header,titleFont,false);
		if (page!=Page::Record && page!=Page::Scores) primaryFocus();
		if (page==Page::Settings) displayAndSound();
		else if (page==Page::Controls) options::DrawControlBindings();
		else if (page==Page::Help) helpSheet();
		else if (page==Page::Scores) scores();
		else if (page==Page::Record) record();
		else if (page==Page::ClearScores)
		{
			hint(scoreMode ? tr("Удалить пять результатов медитативного режима?","Erase the five meditative scores?") :
				tr("Удалить пять результатов классического режима?","Erase the five classic scores?"));
			if (button(tr("Удалить рекорды","Erase scores")))
			{
				high_score::erase_records(scoreMode);
				selectScores(scoreMode);
				go(Page::Scores);
			}
		}
		else
		{
			hint(tr("Закончить текущую партию и закрыть игру?","End the current game and quit?"));
			if (button(tr("Выйти","Quit game"))) { SDL_Event quit{SDL_QUIT}; SDL_PushEvent(&quit); }
		}
		ImGui::Spacing();
		if (page!=Page::Record && button(tr("Назад","Back"),false,-1,Icon::Back)) Toggle();
	}
	if(bodyFont) ImGui::PopFont();
	ImGui::End(); ImGui::PopStyleVar();
}
