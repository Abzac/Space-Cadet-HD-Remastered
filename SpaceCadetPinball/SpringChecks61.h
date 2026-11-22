#pragma once

#include "InputChecks.h"
#include "VisualChecks.h"
#include <set>

namespace springChecks61
{
inline bool run(FILE* report, const std::string& prefix)
{
	auto plunger = pb::MainTable->Plunger;
	auto sprite = plunger->RenderSprite;
	if (!sprite || sprite->HdPlunger != plunger || !HdRenderer::HasPlungerMotion()) return false;
	const SDL_Rect crop{315, 369, 35, 47};
	const bool film = SDL_GetHintBoolean("SPACECADET_CAPTURE_PLUNGER61", SDL_FALSE);
	bool ok = true;
	std::set<uint32_t> poses;
	for (int phase = 0; phase <= 112; ++phase)
	{
		ok &= plunger->SetAnalogPull(phase / 112.f);
		const auto bitmap = sprite->Bmp;
		const auto boost = plunger->Boost;
		render::update();
		ok &= sprite->PlungerPhase == phase && sprite->Bmp == bitmap && plunger->Boost == boost;
		uint32_t hash = 0;
		const auto path = prefix + "plunger61-current.bmp";
		ok &= visualChecks::capture(path, crop, hash);
		poses.insert(hash);
		if (phase % 16 == 0)
		{
			// Redraw this same pose via the original HD bitmap path, at the
			// same simulation instant and with the ball still on the table.
			sprite->HdPlunger = nullptr;
			sprite->DirtyFlag = true;
			render::update();
			uint32_t original = 0;
			ok &= visualChecks::capture(path, crop, original) && hash == original;
			fprintf(report, "Spring key %d: HD %08x, original %08x.\n", phase / 16, hash, original);
			sprite->HdPlunger = plunger;
			sprite->DirtyFlag = true;
		}
	}
	plunger->CancelAnalog();
	render::update();
	ok &= poses.size() == 113 && sprite->PlungerPhase == 0;

	inputChecks::key(SDLK_SPACE, true);
	std::set<int> digital;
	int previous = 0;
	for (int tick = 0; tick < 390; ++tick)
	{
		pb::frame(1000.f / 120);
		const int phase = sprite->PlungerPhase;
		ok &= phase >= previous;
		digital.insert(phase);
		previous = phase;
		if (film && tick % 4 == 0)
		{
			uint32_t hash = 0;
			char name[40];
			snprintf(name, sizeof(name), "plunger61-film-%03d.bmp", tick / 4);
			ok &= visualChecks::capture(prefix + name, crop, hash);
		}
	}
	const auto charged = plunger->Boost;
	inputChecks::key(SDLK_SPACE, false);
	render::update();
	ok &= previous == 112 && digital.size() >= 90 && sprite->PlungerPhase == 0 &&
		plunger->Boost == charged && charged == plunger->MaxPullback && plunger->Threshold == 0.f;
	for (int tick = 0; tick < 36; ++tick)
	{
		pb::frame(1000.f / 120);
		if (film && tick % 4 == 0)
		{
			uint32_t hash = 0;
			char name[40];
			snprintf(name, sizeof(name), "plunger61-film-%03d.bmp", 98 + tick / 4);
			ok &= visualChecks::capture(prefix + name, crop, hash);
		}
	}
	ok &= plunger->Boost == 0;
	fprintf(report, "Spring rendered poses %zu/113; spacebar reached %zu distinct draw phases, final %d; release %.1f.\n",
		poses.size(), digital.size(), previous, charged);

	SDL_Event mouse{};
	mouse.button.button = SDL_BUTTON_MIDDLE;
	mouse.type = SDL_MOUSEBUTTONDOWN;
	mouse.button.state = SDL_PRESSED;
	winmain::event_handler(&mouse);
	for (int i = 0; i < 22; ++i) pb::frame(1000.f / 120);
	const int mousePhase = sprite->PlungerPhase;
	mouse.type = SDL_MOUSEBUTTONUP;
	mouse.button.state = SDL_RELEASED;
	winmain::event_handler(&mouse);
	render::update();
	ok &= mousePhase > 0 && mousePhase < 16 && sprite->PlungerPhase == 0;
	UsingSdlHint background(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	inputChecks::VirtualPad pad(0x054c, 0x0ce6, "Virtual DualSense spring");
	if (!pad.joystick) return false;
	pad.axis(SDL_CONTROLLER_AXIS_RIGHTY, 17000);
	render::update();
	const int stickPhase = sprite->PlungerPhase;
	ok &= plunger->AnalogPull && stickPhase > 16 && stickPhase < 112 && stickPhase % 16 != 0;
	pad.axis(SDL_CONTROLLER_AXIS_RIGHTY, 0);
	render::update();
	ok &= sprite->PlungerPhase == 0 && plunger->Threshold == 0.f;
	pad.axis(SDL_CONTROLLER_AXIS_RIGHTY, 17000);
	inputChecks::tap(SDLK_F3);
	render::update();
	ok &= winmain::single_step && !plunger->AnalogPull && sprite->PlungerPhase == 0;
	inputChecks::tap(SDLK_F3);
	inputChecks::finishResumeCountdown();
	pad.axis(SDL_CONTROLLER_AXIS_RIGHTY, 0);
	pad.axis(SDL_CONTROLLER_AXIS_RIGHTY, 21000);
	pb::replay_level(false);
	render::update();
	ok &= !plunger->PullbackStartedFlag && sprite->PlungerPhase == 0;
	fprintf(report, "Spring middle mouse phase %d; right stick phase %d; rest after new game %d.\n",
		mousePhase, stickPhase, sprite->PlungerPhase);
	return ok;
}
}
