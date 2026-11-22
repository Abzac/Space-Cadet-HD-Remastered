#pragma once

namespace GameMenu
{
	void Style();
	void SetTitleFont(ImFont* font);
	void SetBodyFont(ImFont* font);
	void ReleaseArt();
	void Help();
	bool IsHelpOpen();
	void QuitConfirmation();
	void Scores();
	bool ScoreShoulder(Uint8 button, bool pressed);
	bool ViewingMeditativeScores();
	void Toggle();
	bool IsOpen();
	void Controls();
	void SetLanguage(const char* code);
	void Draw();
}
