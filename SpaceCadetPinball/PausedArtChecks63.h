#pragma once

#include "InputChecks.h"
#include "VisualChecks.h"

namespace pausedArtChecks63
{
inline bool run(FILE* report, const std::string& prefix)
{
	inputChecks::tap(SDLK_F3);
	const auto ball = pb::MainTable->BallList.front();
	const auto position = ball->Position;
	const float speed = ball->Speed, clock = pb::time_now;
	const int ticks = pb::time_ticks, score = pb::MainTable->CurScore;
	bool good = winmain::single_step && !winmain::CanAdvanceGame() && ball->ActiveFlag && speed > .1f;
	struct Sample { const char* name; SDL_Rect area; float first, second; };
	const Sample samples[] = {
		{"right galaxy", {279,168,15,12}, 0.f, 9.f},
		{"top galaxies", {250,16,32,22}, 0.f, 9.f},
		{"poster planet", {398,140,28,22}, 0.f, 15.f},
		{"poster exhaust", {566,78,13,21}, 6.f, 7.69f},
		{"poster lettering", {455,32,64,38}, 62.f, 63.92f},
		{"central diffuser", {171,254,24,28}, 0.f, 5.4f},
	};
	for (const auto& sample : samples)
	{
		uint32_t first{}, next{}, repeated{};
		auto frame = [&](float seconds, uint32_t& hash) {
			return visualChecks::capture(prefix + "paused63-" + sample.name + ".bmp", sample.area,
				hash, nullptr, 0.f, seconds);
		};
		const bool moved = frame(sample.first, first) && frame(sample.second, next) &&
			frame(sample.second, repeated) && first != next && next == repeated;
		fprintf(report, "%s  %s animates during F3 pause at a fixed game tick\n", moved ? "PASS" : "FAIL", sample.name);
		good &= moved;
	}
	good &= !winmain::CanAdvanceGame() && pb::time_ticks == ticks && pb::time_now == clock &&
		pb::MainTable->CurScore == score && ball->Speed == speed &&
		ball->Position.X == position.X && ball->Position.Y == position.Y && ball->Position.Z == position.Z;
	inputChecks::tap(SDLK_SPACE);
	inputChecks::finishResumeCountdown();
	return good && winmain::CanAdvanceGame();
}
}
