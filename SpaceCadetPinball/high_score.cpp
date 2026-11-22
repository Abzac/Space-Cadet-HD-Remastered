#include "pch.h"
#include "high_score.h"

#include "options.h"
#include "pb.h"
#include "GameMenu.h"
std::vector<high_score_entry> high_score::ScoreQueue;
high_score_struct high_score::highscore_table[5];
static bool fairScores;

static uint32_t checksumPart(const high_score_struct& entry)
{
	uint32_t sum = static_cast<uint32_t>(entry.Score);
	for (const char* p = entry.Name; *p; ++p)
		sum += static_cast<int8_t>(*p); // Retain the Windows format for UTF-8 names.
	return sum;
}

void high_score::view(bool meditative, high_score_struct (&entries)[5])
{
	char Buffer[20];
	uint32_t checkSum = 0;
	for (auto position = 0; position < 5; ++position)
	{
		auto& tablePtr = entries[position];

		snprintf(Buffer, sizeof Buffer, "%s%d.Name", meditative ? "Fair." : "", position);
		auto name = options::GetSetting(Buffer, "");
		SDL_utf8strlcpy(tablePtr.Name, name.c_str(), sizeof tablePtr.Name);

		snprintf(Buffer, sizeof Buffer, "%s%d.Score", meditative ? "Fair." : "", position);
		tablePtr.Score = options::get_int(Buffer, -999);

		checkSum += checksumPart(tablePtr);
	}

	auto verification = options::get_int(meditative ? "Fair.Verification" : "Verification", 7);
	if (checkSum != static_cast<uint32_t>(verification))
		for (auto& row : entries) row = {{0}, -999};
}

int high_score::read()
{
	fairScores = pb::FairMode;
	ScoreQueue.clear();
	view(fairScores, highscore_table);
	return 0;
}

int high_score::write()
{
	char Buffer[20];

	uint32_t checkSum = 0;
	for (auto position = 0; position < 5; ++position)
	{
		auto& tablePtr = highscore_table[position];

		snprintf(Buffer, sizeof Buffer, "%s%d.Name", fairScores ? "Fair." : "", position);
		options::SetSetting(Buffer, tablePtr.Name);

		snprintf(Buffer, sizeof Buffer, "%s%d.Score", fairScores ? "Fair." : "", position);
		options::set_int(Buffer, tablePtr.Score);

		checkSum += checksumPart(tablePtr);
	}

	options::set_int(fairScores ? "Fair.Verification" : "Verification", static_cast<int32_t>(checkSum));
	return 0;
}

void high_score::clear_table()
{
	for (auto& table : highscore_table)
	{
		table.Score = -999;
		table.Name[0] = 0;
	}
}

int high_score::get_score_position(int score)
{
	if (score <= 0)
		return -1;

	for (int position = 0; position < 5; position++)
	{
		if (highscore_table[position].Score < score)
			return position;
	}
	return -1;
}

void high_score::show_high_score_dialog()
{
	GameMenu::Scores();
}

void high_score::persist()
{
	write();
	if (const char* path = ImGui::GetIO().IniFilename)
		ImGui::SaveIniSettingsToDisk(path);
}

void high_score::show_and_set_high_score_dialog(high_score_entry score)
{
	if (score.Position < 0 || score.Position >= 5)
		score.Position = get_score_position(score.Entry.Score);
	if (score.Position < 0) return;

	for (auto& pending : ScoreQueue)
		if (pending.Position >= score.Position) ++pending.Position;
	ScoreQueue.erase(std::remove_if(ScoreQueue.begin(), ScoreQueue.end(),
		[](const high_score_entry& entry) { return entry.Position >= 5; }), ScoreQueue.end());
	for (int i = 4; i > score.Position; --i)
		highscore_table[i] = highscore_table[i - 1];
	score.Entry.Name[sizeof(score.Entry.Name) - 1] = 0;
	highscore_table[score.Position] = score.Entry;
	ScoreQueue.push_back(score);
	persist();
}

high_score_entry* high_score::pending()
{
	return ScoreQueue.empty() ? nullptr : &ScoreQueue.front();
}

void high_score::finish_name(bool save)
{
	auto record = pending();
	if (!record) return;
	if (save && record->Entry.Name[0])
	{
		highscore_table[record->Position] = record->Entry;
		persist();
	}
	ScoreQueue.erase(ScoreQueue.begin());
}

void high_score::erase_records(bool meditative)
{
	if (meditative == fairScores)
	{
		ScoreQueue.clear();
		clear_table();
		persist();
		return;
	}
	// Clearing the other board keeps the active table and any names in progress.
	const char* prefix = meditative ? "Fair." : "";
	for (int row = 0; row < 5; ++row)
	{
		const auto key = prefix + std::to_string(row);
		options::SetSetting(key + ".Name", "");
		options::SetSetting(key + ".Score", "-999");
	}
	options::set_int(meditative ? "Fair.Verification" : "Verification", -999 * 5);
	if (const char* path = ImGui::GetIO().IniFilename) ImGui::SaveIniSettingsToDisk(path);
}
