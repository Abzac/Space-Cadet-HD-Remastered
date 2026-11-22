#pragma once
#include "DomeVeins.h"
#include "EffectsChecks4.h"
#include "HdrOutput.h"

namespace domeVeinChecks632
{
inline bool run(FILE* report, const std::string& prefix)
{
	constexpr SDL_Rect crop{145, 233, 76, 74};
	constexpr int s = HdRenderer::Scale;
	bool ok = true;
	uint32_t hash{};
	const float times[] = {.24f, .54f, .85f, 1.68f};
	effectsChecks4::Difference samples[4];
	for (int i = 0; i < 4; ++i)
	{
		const auto file = prefix + "dome632-" + std::to_string(i);
		DomeVeins::SetDiagnosticBypass(true);
		ok &= visualChecks::capture(file + "-glass.bmp", crop, hash, nullptr, 0, times[i]);
		DomeVeins::SetDiagnosticBypass(false);
		ok &= visualChecks::capture(file + ".bmp", crop, hash, nullptr, 0, times[i]);
		samples[i] =
		    effectsChecks4::difference(file + ".bmp", file + "-glass.bmp", {0, 0, crop.w * s, crop.h * s});
		// The center insert is opaque, including the rim closest to this particular route.
		ok &= effectsChecks4::difference(file + ".bmp", file + "-glass.bmp",
		                                 {(179 - crop.x) * s, (264 - crop.y) * s, 8 * s, 8 * s})
		          .maximum == 0;
	}
	const float dx = (samples[1].cx - samples[0].cx) / s, dy = (samples[1].cy - samples[0].cy) / s;
	fprintf(report,
	        "Dome discharge .24/.54 s: %d/%d HD texels, peak +%u/+%u SDR levels, travel %.2f native units; "
	        "afterglow %d, quiet at 1.68 s: %d changed.\n",
	        samples[0].pixels, samples[1].pixels, samples[0].maximum, samples[1].maximum, hypotf(dx, dy),
	        samples[2].pixels,samples[3].pixels);
	// At peak the conducting line spans most of this 24-unit branch.
	ok &= samples[0].pixels > 60 && samples[1].pixels > 2*samples[0].pixels &&
	      samples[1].pixels > 600 && samples[1].pixels < 30*5*s*s && samples[2].pixels > 60 &&
	      samples[0].maximum >= 30 && samples[2].maximum < samples[1].maximum &&
	      hypotf(dx, dy) > 4.f && samples[3].maximum == 0;
	if (SDL_GetHintBoolean("SPACECADET_DOME_FRAMES", SDL_FALSE))
		for (int i = 0; i < 216; ++i)
			ok &= visualChecks::capture(prefix + "dome-frame-" + std::to_string(i) + ".bmp", crop, hash, nullptr,
			                            0, i * .05f);
	return ok;
}

inline bool occluded(FILE* report, float phase, HdrOutput::LuminanceSite site, float x, float y)
{
	auto ball = pb::MainTable->BallList.front();
	const auto previous = ball->Position;
	const auto wasActive = ball->ActiveFlag;
	const auto collision = ball->CollisionFlag;
	ball->ActiveFlag = 1;
	ball->CollisionFlag = 0;
	vector2i ground{int(std::round(x)), int(std::round(y))};
	// ReverseXForm is in the table plane. Account for the height of the
	// sphere's center before placing the real ball over the brightest site.
	for (int i = 0; i < 2; ++i)
	{
		ball->Position = proj::ReverseXForm(ground);
		ball->Position.Z = ball->Radius;
		float radius;
		const auto center = proj::BallImage(ball->Position, ball->Radius, radius);
		ground.X += int(std::round(x - center.X));
		ground.Y += int(std::round(y - center.Y));
	}
	ball->Repaint();
	render::update();
	bool ok = true, covered = false;
	size_t baselineRegions = 0;
	auto diffuse = site, lit = site;
	for (int i = 0; i < 2; ++i)
	{
		DomeVeins::SetDiagnosticHdrBypass(i == 0);
		SDL_RenderClear(winmain::Renderer);
		render::PresentVScreen(phase);
		if (!i)
			baselineRegions = render::EmissiveRegions.size();
		else if (render::EmissiveRegions.size() == baselineRegions + 1)
		{
			const auto& region = render::EmissiveRegions.back();
			covered =
			    std::all_of(region.covered, region.covered + 8, [](uint32_t word) { return word == 0xffffffffu; });
		}
		auto& reading = i ? lit : diffuse;
		ok &= HdrOutput::Present(winmain::MainWindow, winmain::Renderer, true, true, false, &reading, 1);
	}
	SDL_FRect image{}, clip{};
	ok &= render::GetVisibleBallImage(*ball, image, clip);
	DomeVeins::SetDiagnosticHdrBypass(false);
	ball->Position = previous;
	ball->ActiveFlag = wasActive;
	ball->CollisionFlag = collision;
	if (wasActive)
		ball->Repaint();
	else
		ball->SpriteSet(-1);
	render::update();
	fprintf(report, "Dome emitter fully masked by real ball: %s; FP16 at same covered site %.2f / %.2f nits.\n",
	        covered ? "yes" : "no", diffuse.nits, lit.nits);
	return ok && covered && diffuse.nits >= 0 && lit.nits >= 0 && fabsf(lit.nits - diffuse.nits) < .5f;
}

inline bool hdr(FILE* report)
{
	auto renderer = winmain::Renderer;
	int w{}, h{};
	if (SDL_GetRenderTarget(renderer) || SDL_GetRendererOutputSize(renderer, &w, &h) || w < 1 || h < 1)
		return false;
	fullscrn::window_size_changed();
	const auto dest = render::DestinationRect;
	const float sx = float(dest.w) / render::vscreen->Width, sy = float(dest.h) / render::vscreen->Height;
	if (sx <= 0 || sy <= 0)
		return false;
	// Survey physical output pixels over the whole southwest branch, plus
	// the center glass insert, at the exact current display scale.
	const int left = std::max(0, int(dest.x + 160 * sx)), right = std::min(w, int(dest.x + 188 * sx));
	const int top = std::max(0, int(dest.y + 260 * sy)), bottom = std::min(h, int(dest.y + 295 * sy));
	std::vector<HdrOutput::LuminanceSite> sites;
	for (int y = top; y < bottom; ++y)
		for (int x = left; x < right; ++x)
			sites.push_back({(x + .5f) / w, (y + .5f) / h});
	if (sites.empty())
		return false;
	bool ok = true;
	for (float phase : {.24f, .54f})
	{
		auto diffuse = sites, emitted = sites;
		for (int i = 0; i < 2; ++i)
		{
			DomeVeins::SetDiagnosticHdrBypass(i == 0);
			SDL_RenderClear(renderer);
			render::PresentVScreen(phase);
			auto& values = i ? emitted : diffuse;
			ok &= HdrOutput::Present(winmain::MainWindow, renderer, true, true, false, values.data(),
			                         unsigned(values.size()));
		}
		DomeVeins::SetDiagnosticHdrBypass(false);
		int lit = 0;
		size_t hottest = 0;
		float rise = 0, peak = 0, base = 0, centerDelta = 0;
		for (size_t i = 0; i < sites.size(); ++i)
		{
			const float x = (sites[i].u * w - dest.x) / sx, y = (sites[i].v * h - dest.y) / sy;
			const float delta = emitted[i].nits - diffuse[i].nits;
			if (emitted[i].nits < 0 || diffuse[i].nits < 0)
				ok = false;
			if (delta > .5f)
			{
				++lit;
				peak = std::max(peak, emitted[i].nits);
			}
			if (delta > rise)
			{
				rise = delta;
				base = diffuse[i].nits;
				hottest = i;
			}
			if (fabsf(x - 183.f) < 3 && fabsf(y - 268.f) < 3)
				centerDelta = std::max(centerDelta, fabsf(delta));
		}
		fprintf(report,
		        "Dome FP16 t=%.2f, %dx%d, %d pixels gained >0.5 nit, diffuse at strongest rise %.2f, +%.2f nits, "
		        "highest vein %.2f nits, center insert delta %.3f. %s\n",
		        phase, w, h, lit, base, rise, peak, centerDelta, HdrOutput::Status());
		ok &= HdrOutput::Active() && lit > 0 && lit < 40 * sx * sy && rise > 3.f && centerDelta < .5f;
		if (phase == .24f && rise > 3.f)
			ok &= occluded(report, phase, sites[hottest], (sites[hottest].u * w - dest.x) / sx,
			               (sites[hottest].v * h - dest.y) / sy);
	}
	return ok;
}
} // namespace domeVeinChecks632
