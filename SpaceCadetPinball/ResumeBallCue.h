#pragma once

#include "TBall.h"
#include "TPinballTable.h"
#include "pb.h"
#include "render.h"
#include "imgui.h"

namespace ResumeBallCue
{
inline void Draw(float progress)
{
	if (!(progress > 0.f && progress < 1.f) || !pb::MainTable) return;
	const auto scale = ImGui::GetIO().DisplayFramebufferScale;
	if (scale.x <= 0.f || scale.y <= 0.f) return;
	auto list = ImGui::GetForegroundDrawList();
	const auto origin = ImGui::GetMainViewport()->Pos;
	const auto smooth = [](float t) {
		t = Clamp(t, 0.f, 1.f);
		return t*t*(3.f-2.f*t);
	};
	const float opacity = smooth(progress/.18f) * smooth((1.f-progress)/.52f);
	const float gap = 3.25f - 1.6f*smooth(progress);
	const auto line = IM_COL32(174, 221, 248, int(215.f*opacity));
	const auto shadow = IM_COL32(15, 29, 57, int(143.f*opacity));
	for (const auto ball : pb::MainTable->BallList)
	{
		SDL_FRect image{}, clip{};
		if (!render::GetVisibleBallImage(*ball, image, clip)) continue;
		const ImVec2 center{origin.x + (image.x + image.w*.5f)/scale.x, origin.y + (image.y + image.h*.5f)/scale.y};
		const float rx = image.w / (2.f*scale.x), ry = image.h / (2.f*scale.y);
		const float width = Clamp(.18f*std::min(rx, ry), .9f, 3.6f);
		list->PushClipRect({origin.x + clip.x/scale.x, origin.y + clip.y/scale.y},
			{origin.x + (clip.x+clip.w)/scale.x, origin.y + (clip.y+clip.h)/scale.y}, true);
		// Three open arcs converge once, leaving the steel and its highlights exposed.
		for (int arc = 0; arc < 3; ++arc)
		{
			ImVec2 points[13];
			const float middle = (-90.f + 120.f*arc) * (Pi/180.f);
			for (int i = 0; i < IM_ARRAYSIZE(points); ++i)
			{
				const float a = middle + (i/12.f-.5f)*(.29f*Pi);
				points[i] = {center.x + rx*gap*cosf(a), center.y + ry*gap*sinf(a)};
			}
			list->AddPolyline(points, IM_ARRAYSIZE(points), shadow, ImDrawFlags_None, width*2.65f);
			list->AddPolyline(points, IM_ARRAYSIZE(points), line, ImDrawFlags_None, width);
		}
		list->PopClipRect();
	}
}
}
