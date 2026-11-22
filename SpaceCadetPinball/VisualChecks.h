#pragma once

#include "HdRenderer.h"
#include "CabinetEffects.h"
#include "TableRelief.h"
#include "RailReflections.h"
#include "TBumper.h"
#include "TCircle.h"
#include "TLine.h"
#include "TWall.h"
#include "proj.h"

namespace visualChecks
{
inline void measurePlay(FILE* report, const std::string& prefix)
{
	int oldWidth, oldHeight;
	SDL_GetWindowSize(winmain::MainWindow, &oldWidth, &oldHeight);
	auto renderer = winmain::Renderer;
	for (int requested : {900, 1200})
	{
		winmain::SetWindowSize(requested);
		fullscrn::window_size_changed();
		int width, height;
		SDL_GetRendererOutputSize(renderer, &width, &height);
		std::vector<double> compose, presented;
		int movingFrames = 0;
		for (int i = 0; i < 144; ++i)
		{
			const auto begin = SDL_GetPerformanceCounter();
			pb::frame(1000.f / 120.f);
			SDL_RenderClear(renderer);
			render::PresentVScreen(pb::time_now);
			SDL_RenderFlush(renderer);
			const auto submitted = SDL_GetPerformanceCounter();
			if (i == 12)
			{
				const auto ball = pb::MainTable->BallList.front();
				const auto p = proj::xform_to_2d(ball->Position);
				fprintf(report, "Play capture %dx%d: native ball center (%d, %d).\n", width, height, p.X, p.Y);
				auto image = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
				if (image &&
					SDL_RenderReadPixels(renderer, nullptr, image->format->format, image->pixels, image->pitch) == 0)
					SDL_SaveBMP(image, (prefix + "play-" + std::to_string(requested) + ".bmp").c_str());
				SDL_FreeSurface(image);
			}
			SDL_RenderPresent(renderer);
			if (i >= 16)
			{
				auto ball = pb::MainTable->BallList.front();
				if (ball->ActiveFlag && ball->Speed > 0.1f && ball->RenderSprite->Bmp) ++movingFrames;
				const double ms = 1000.0 / SDL_GetPerformanceFrequency();
				compose.push_back((submitted - begin) * ms);
				presented.push_back((SDL_GetPerformanceCounter() - begin) * ms);
			}
		}
		std::sort(compose.begin(), compose.end());
		std::sort(presented.begin(), presented.end());
		fprintf(report,
				"HD play %dx%d, %d/128 ticks with moving ball (physics, lights, reflections, SDL draw, SDR). "
				"CPU submit p50 %.3f / p95 %.3f ms; with driver presentation p50 %.3f / p95 %.3f ms.\n",
				width, height, movingFrames, compose[64], compose[121], presented[64], presented[121]);
	}
	SDL_SetWindowSize(winmain::MainWindow, oldWidth, oldHeight);
	fullscrn::window_size_changed();
}

// Render a crop of the real playing table at its original HD asset resolution.
// No sprite or lamp is painted by this test.
inline bool capture(const std::string& path, SDL_Rect crop, uint32_t& hash, const vector2* disk = nullptr,
					float radius = 0.f, float decorTime = pb::time_now)
{
	constexpr int scale = HdRenderer::Scale;
	auto renderer = winmain::Renderer;
	auto previous = SDL_GetRenderTarget(renderer);
	auto destination = render::DestinationRect;
	auto target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, crop.w * scale,
									crop.h * scale);
	if (!target) return false;
	SDL_Surface* image = nullptr;
	bool saved = false;
	if (SDL_SetRenderTarget(renderer, target) == 0)
	{
		render::DestinationRect = {-crop.x * scale, -crop.y * scale, render::vscreen->Width * scale,
								   render::vscreen->Height * scale};
		SDL_RenderClear(renderer);
		render::PresentVScreen(decorTime);
		image = SDL_CreateRGBSurfaceWithFormat(0, crop.w * scale, crop.h * scale, 32, SDL_PIXELFORMAT_ARGB8888);
		if (image &&
			SDL_RenderReadPixels(renderer, nullptr, image->format->format, image->pixels, image->pitch) == 0)
		{
			hash = 2166136261u;
			auto data = static_cast<const uint8_t*>(image->pixels);
			for (int y = 0; y < image->h; ++y)
				for (int x = 0; x < image->w; ++x)
				{
					const float dx = crop.x + (x + .5f) / scale - (disk ? disk->X : 0.f),
								dy = crop.y + (y + .5f) / scale - (disk ? disk->Y : 0.f);
					if (disk && dx * dx + dy * dy > radius * radius) continue;
					for (int i = 0; i < 4; ++i)
						hash = (hash ^ data[y * image->pitch + 4 * x + i]) * 16777619u;
				}
			saved = SDL_SaveBMP(image, path.c_str()) == 0;
		}
		render::DestinationRect = destination;
		SDL_SetRenderTarget(renderer, previous);
	}
	SDL_FreeSurface(image);
	SDL_DestroyTexture(target);
	return saved;
}

inline bool run(FILE* report, const std::string& prefix)
{
	auto table = pb::MainTable;
	auto ball = table->BallList.front();
	auto wall = dynamic_cast<TWall*>(table->find_component("v_rebo1"));
	auto lamp = dynamic_cast<TLight*>(table->find_component("lite84"));
	auto bumper = dynamic_cast<TBumper*>(table->find_component("a_bump1"));
	if (!wall || !lamp || !bumper || wall->EdgeList.empty() || bumper->EdgeList.empty()) return false;
	TLine* line = nullptr;
	float longest = 0;
	for (auto edge : wall->EdgeList)
		if (auto segment = dynamic_cast<TLine*>(edge))
		{
			const auto length = hypotf(segment->X1 - segment->X0, segment->Y1 - segment->Y0);
			if (length > longest)
			{
				line = segment;
				longest = length;
			}
		}
	auto circle = dynamic_cast<TCircle*>(bumper->EdgeList.front());
	if (!line || !circle) return false;
	auto aim = [&](vector2 point, vector2 normal, int mask)
	{
		ball->ActiveFlag = 1;
		ball->CollisionDisabledFlag = false;
		ball->CollisionComp = nullptr;
		ball->CollisionFlag = 0;
		ball->EdgeCollisionCount = 0;
		ball->CollisionMask = mask;
		ball->Position = {point.X + normal.X * ball->Radius, point.Y + normal.Y * ball->Radius, ball->Radius};
		ball->Direction = {-normal.X, -normal.Y, 0};
		ball->Speed = 20.f;
	};
	auto step = [](int ticks)
	{
		for (int i = 0; i < ticks; ++i)
			pb::frame(1000.f / 120);
	};
	// Only set up an incoming trajectory. The normal collision sweep, scoring
	// callback and native timers drive the entire contact and its three frames.
	const auto normal = line->Line.PerpendicularC;
	aim({(line->X0 + line->X1) * .5f, (line->Y0 + line->Y1) * .5f}, normal, line->CollisionGroup);
	const int score = table->CurScore;
	for (int i = 0; i < 20 && !wall->Timer; ++i)
		step(1);
	if (!wall->Timer || !lamp->RenderSprite->Bmp || table->CurScore <= score) return false;
	const auto box = lamp->RenderSprite->BmpRect;
	const SDL_Rect detail{box.XPosition - 15, box.YPosition - 15, box.Width + 52, box.Height + 30};
	const SDL_Rect arc{box.XPosition + 4, box.YPosition + 4, box.Width - 8, box.Height - 8};
	bool good = true;
	uint32_t hashes[3]{}, ignored{};
	for (int i = 0; i < 3; ++i)
	{
		if (i) step(4);
		good &= lamp->RenderSprite->Bmp != nullptr;
		good &= capture(prefix + "sling-" + std::to_string(i) + ".bmp", detail, ignored);
		good &= capture(prefix + "arc-" + std::to_string(i) + ".bmp", arc, hashes[i]);
		const auto p = proj::xform_to_2d(ball->Position);
		good &= capture(prefix + "steel-" + std::to_string(i) + ".bmp", {p.X - 20, p.Y - 20, 40, 40}, ignored);
		float radius;
		const auto center = proj::BallImage(ball->Position, ball->Radius, radius);
		fprintf(report, "Sling frame %d: ball center (%.2f, %.2f), radius %.2f, arc pixels %08x.\n", i, center.X,
				center.Y, radius, hashes[i]);
	}
	// Without advancing physics, change a real lamp beside the passing ball.
	// Compare only a crop inside the sphere, away from the source itself.
	if (auto nearLamp = dynamic_cast<TLight*>(table->find_component("lite16")))
	{
		const bool prior = nearLamp->LightOnFlag;
		float radius;
		const auto p = proj::BallImage(ball->Position, ball->Radius, radius);
		const SDL_Rect inner{int(p.X) - 8, int(p.Y) - 8, 17, 17};
		uint32_t off, on;
		nearLamp->Message(MessageCode::TLightTurnOff, 0);
		render::update();
		good &= capture(prefix + "steel-lamp-off.bmp", inner, off, &p, radius - .38f);
		nearLamp->Message(MessageCode::TLightTurnOn, 0);
		render::update();
		good &= capture(prefix + "steel-lamp-on.bmp", inner, on, &p, radius - .38f);
		nearLamp->Message(prior ? MessageCode::TLightTurnOn : MessageCode::TLightTurnOff, 0);
		render::update();
		fprintf(report, "Direct light on steel, same ball pose, inner sphere region %08x / %08x.\n", off, on);
		good &= off != on;
	}
	step(14);
	good &= !wall->Timer && !lamp->RenderSprite->Bmp;
	float tail = CabinetEffects::FlashStrength(0, pb::time_now);
	good &= tail > .10f && table->CurScore == score + 500;
	good &= capture(prefix + "sling-tail-180ms.bmp", detail, ignored);
	step(56);
	good &= CabinetEffects::FlashStrength(0, pb::time_now) == 0.f;
	fprintf(report, "Native 500-point switch open, visual arc at +183 ms %.3f, +650 ms %.3f.\n", tail,
			CabinetEffects::FlashStrength(0, pb::time_now));
	good &= capture(prefix + "sling-off.bmp", detail, ignored);
	const float r = sqrtf(circle->Circle.RadiusSq);
	aim({circle->Circle.Center.X, circle->Circle.Center.Y - r}, {0, -1}, circle->CollisionGroup);
	const int bumperScore = table->CurScore;
	for (int i = 0; i < 20 && !bumper->Timer; ++i)
		step(1);
	const auto b = bumper->RenderSprite->BmpRect;
	good &= bumper->Timer && bumper->RenderSprite->Emissive && table->CurScore > bumperScore;
	const SDL_Rect bumperArea{b.XPosition - 38, b.YPosition - 15, b.Width + 80, b.Height + 48};
	uint32_t receiverOn, receiverOff;
	good &= capture(prefix + "star-hit.bmp", bumperArea, receiverOn);
	HdRenderer::SetReceiverDiagnosticBypass(true);
	good &= capture(prefix + "star-hit-no-spill.bmp", bumperArea, receiverOff);
	HdRenderer::SetReceiverDiagnosticBypass(false);
	good &= receiverOn != receiverOff;
	fprintf(report, "Same bumper hit, local receiver pass on/off %08x / %08x, %zu visible relief triangles.\n",
			receiverOn, receiverOff, TableRelief::FaceCount());
	measurePlay(report, prefix);
	if (const char* sequence = SDL_getenv("SPACECADET_CAPTURE_SEQUENCE"))
		if (sequence[0] == '1')
		{
			aim({(line->X0 + line->X1) * .5f, (line->Y0 + line->Y1) * .5f}, normal, line->CollisionGroup);
			for (int i = 0; i < 20 && !wall->Timer; ++i)
				step(1);
			good &= wall->Timer && lamp->RenderSprite->Bmp;
			for (int i = 0; i < 48; ++i)
			{
				char name[32];
				SDL_snprintf(name, sizeof(name), "play-seq-%02d.bmp", i);
				good &= capture(prefix + name, {52, 255, 256, 160}, ignored);
				step(2);
			}
			fprintf(report, "Captured 48 live lower-table frames, 60 fps, starting at the scored rebound.\n");
		}
	// Start a ball in the actual shooter lane, then let the normal field and
	// collision loop carry it alongside the registered stainless sidewall.
	const auto from = proj::ReverseXForm({318, 309});
	const auto toward = proj::ReverseXForm({315, 280});
	const float dx = toward.X - from.X, dy = toward.Y - from.Y, len = hypotf(dx, dy);
	ball->ActiveFlag = 1;
	ball->CollisionDisabledFlag = false;
	ball->CollisionComp = nullptr;
	ball->CollisionFlag = 0;
	ball->EdgeCollisionCount = 0;
	ball->CollisionMask = 1;
	ball->Position = {from.X, from.Y, ball->Radius};
	ball->Direction = {dx / len, dy / len, 0};
	ball->Speed = 13.f;
	int railFrames = 0;
	float lastY = 10000.f;
	for (int i = 0; i < 45 && railFrames < 2; ++i)
	{
		step(1);
		render::PresentVScreen(pb::time_now);
		const auto railSamples = RailReflections::LastSampleCount();
		float radius;
		const auto pos = proj::BallImage(ball->Position, ball->Radius, radius);
		if (railSamples < 8 || fabsf(pos.Y - lastY) < 9.f) continue;
		lastY = pos.Y;
		const SDL_Rect railArea{290, 254, 56, 70};
		uint32_t litRail = 0;
		good &= capture(prefix + "shooter-rail-" + std::to_string(railFrames) + ".bmp", railArea, litRail);
		if (!railFrames)
		{
			uint32_t reference = 0;
			RailReflections::SetDiagnosticBypass(true);
			good &= capture(prefix + "shooter-rail-reference.bmp", railArea, reference);
			RailReflections::SetDiagnosticBypass(false);
			good &= litRail != reference;
		}
		fprintf(report,
				"Shooter guide frame %d: physical ball (%.2f, %.2f), radius %.2f, reflecting material samples %d.\n",
				railFrames, pos.X, pos.Y, radius, railSamples);
		++railFrames;
	}
	ball->Disable();
	uint32_t galaxy[2][2]{};
	const SDL_Rect nebulae[]{{273, 162, 25, 22}, {253, 17, 17, 11}};
	for (int frame = 0; frame < 2; ++frame)
	{
		for (int field = 0; field < 2; ++field)
			good &= capture(prefix + "galaxy-" + std::to_string(field) + "-" + std::to_string(frame) + ".bmp",
							nebulae[field], galaxy[field][frame]);
		if (!frame) step(145);
	}
	fprintf(report, "Right galaxy core +1.2 s %08x / %08x, upper galaxy core %08x / %08x.\n", galaxy[0][0],
			galaxy[0][1], galaxy[1][0], galaxy[1][1]);
	good &= galaxy[0][0] != galaxy[0][1] && galaxy[1][0] != galaxy[1][1];
	return good && railFrames == 2 && hashes[0] != hashes[1] && hashes[1] != hashes[2] && hashes[0] != hashes[2];
}
} // namespace visualChecks
