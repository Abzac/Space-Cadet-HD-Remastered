#pragma once

#include <algorithm>
#include <cstdint>
#include "CabinetCyrillic.h"
#include "imgui.h"

namespace ResumeOverlay
{
// Windows counter digits and PB_MSGFT capitals, with the mission-panel
// Cyrillic face for the Russian signal.
inline const cabinetCyrillic::Letter& LensMask(char32_t code)
{
	static constexpr cabinetCyrillic::Letter mask[] = {
		{U'1', 6, {0x0c,0x0e,0x0f,0x0c,0x0c,0x0c,0x0c,0x3e,0x1f}},
		{U'2', 7, {0x1c,0x3e,0x33,0x30,0x30,0x18,0x0c,0x7e,0x3f}},
		{U'3', 7, {0x7c,0x7e,0x30,0x18,0x1c,0x38,0x30,0x3e,0x1f}},
		{U'G', 9, {0x38,0xfe,0x46,0x1f3,0x1f3,0x1c3,0xe6,0xfe,0x38}},
		{U'O', 8, {0x18,0x7e,0xe7,0xc3,0xc3,0xc3,0xe7,0x7e,0x18}},
		{U'!', 2, {3,3,3,3,3,3,0,3,3}}
	};
	for (const auto& letter : mask)
		if (letter.code == code) return letter;
	if (const auto* letter = cabinetCyrillic::find(code)) return *letter;
	return mask[5];
}

inline void Draw(int stage, float progress, bool russian)
{
	if (stage < 0 || stage > 3) return;
	const char32_t digit[] = {U'0' + static_cast<char32_t>(stage), 0};
	const char32_t* label = stage ? digit : (russian ? U"ВПЕРЁД!" : U"GO!");
	int columns = -1, rows = 0;
	for (auto ch = label; *ch; ++ch)
	{
		const auto& letter = LensMask(*ch);
		columns += letter.width + 1;
		for (int row = 0; row < 11; ++row)
			if (letter.rows[row]) rows = std::max(rows, row + 1);
	}
	const auto view = ImGui::GetMainViewport();
	const auto center = view->GetCenter();
	const float t = std::max(0.f, std::min(progress, 1.f));
	const float fade = stage == 0 ? std::max(0.f, t * 2.f - 1.f) : 0.f;
	const float opacity = 1.f - fade * fade * (3.f - 2.f * fade);
	if (opacity <= 0.f) return;
	const float pitch = std::min(view->Size.y * .025f, view->Size.x * .75f / columns) *
		(.90f + .10f * t * (2.f - t));
	if (pitch < 1.f) return;
	auto list = ImGui::GetForegroundDrawList();
	const float firstX = center.x - (columns - 1) * pitch * .5f;
	const float firstY = center.y - (rows - 1) * pitch * .5f;
	struct Layer { float radius, dx, dy; ImU32 color; };
	constexpr Layer layers[] = {
		{.74f, .05f, .17f, IM_COL32(7, 7, 24, 179)},
		{.59f, 0, 0, IM_COL32(92, 79, 230, 110)},
		{.46f, 0, 0, IM_COL32(141, 133, 244, 255)},
		{.32f, -.025f, -.055f, IM_COL32(208, 204, 255, 255)}
	};
	for (const auto& layer : layers)
	{
		const ImU32 alpha = static_cast<ImU32>((layer.color >> IM_COL32_A_SHIFT) * opacity + .5f);
		const ImU32 color = (layer.color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
		int column = 0;
		for (auto ch = label; *ch; ++ch)
		{
			const auto& letter = LensMask(*ch);
			for (int y = 0; y < rows; ++y)
				for (int x = 0; x < letter.width; ++x)
					if (letter.rows[y] & (1u << x))
						list->AddCircleFilled({firstX + (column + x + layer.dx) * pitch,
							firstY + (y + layer.dy) * pitch}, layer.radius * pitch, color, 12);
			column += letter.width + 1;
		}
	}
}
}
