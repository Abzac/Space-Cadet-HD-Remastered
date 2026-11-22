#pragma once

#include "TLight.h"
#include "VisualChecks.h"

namespace fieldArtChecks5
{
inline bool run(FILE* report, const std::string& prefix)
{
	// Freeze gameplay so that the palette and HD captures contain the same
	// lamps at the same positions, including any occluding hardware.
	const bool savedGraphics = render::EnhancedGraphics;
	const int score = pb::MainTable->CurScore;
	std::vector<TLight*> lights;
	for (int i = 300; i <= 322; ++i)
	{
		const auto name = "lite" + std::to_string(i);
		auto lamp = dynamic_cast<TLight*>(pb::MainTable->find_component(name.c_str()));
		if (!lamp) return false;
		lights.push_back(lamp);
	}
	auto ramp = dynamic_cast<TLight*>(pb::MainTable->find_component("lite198"));
	if (!ramp) return false;
	lights.push_back(ramp);
	for (const auto lamp : lights) lamp->Message(MessageCode::Reset, 0);
	bool ok = true;
	for (int lit = 0; lit <= 1; ++lit)
	{
		for (auto lamp : lights)
		{
			lamp->Message(lit ? MessageCode::TLightTurnOn : MessageCode::TLightTurnOff, 0);
			ok &= (lamp->RenderSprite->Bmp != nullptr) == bool(lit);
		}
		render::update();
		for (const bool enhanced : {false, true})
		{
			render::EnhancedGraphics = enhanced;
			const auto name = prefix + (enhanced ? "detail5-hd-" : "detail5-dat-") + (lit ? "lit" : "off");
			uint32_t hash{};
			ok &= visualChecks::capture(name + ".bmp", {0, 0, 365, 334}, hash);
		}
	}
	render::EnhancedGraphics = savedGraphics;
	fprintf(report, "Switched all 23 stock mission arrows (lite300..322) and the red ramp insert (lite198) in both renderers.\n");
	return ok && pb::MainTable->CurScore == score;
}
}
