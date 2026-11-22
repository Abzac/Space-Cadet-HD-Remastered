#pragma once

#include "control.h"
#include "nudge.h"
#include "options.h"
#include "pb.h"
#include "TBall.h"
#include "TDrain.h"
#include "TFlipper.h"
#include "TFlipperEdge.h"
#include "TGate.h"
#include "TKickback.h"
#include "TPinballTable.h"

// Integration probe for a disposable initialized Windows table.
inline bool verifyFairMechanics(FILE* report)
{
	auto table = pb::MainTable;
	bool ok = true;
	const auto check = [&](bool passed, const char* name) {
		fprintf(report, "%s %s\n", passed ? "PASS" : "FAIL", name);
		ok &= passed;
	};

	for (auto active : table->BallList)
		active->Disable();
	auto ball = table->AddBall({0.0f, 10.15f});
	const auto& left = *table->FlipperL->FlipperEdge;
	const auto& right = *table->FlipperR->FlipperEdge;
	const float gap = std::fabs(left.T1Src.X - right.T1Src.X) - left.CircleT1Radius - right.CircleT1Radius;
	fprintf(report, "Flipper tip clearance for ball center %.5f\n", gap);
	check(std::fabs(gap - 0.936f) < 0.01f, "Both modes retain the original gap at rest");

	// Drop balls under all four held-flipper combinations. Use the engine's
	// normal field integration, broad phase, substeps, and drain collisions.
	int survivors = 0;
	int centerDrains = 0;
	for (int pose = 0; pose < 4; ++pose)
	{
		ball->Disable();
		table->FlipperL->Message((pose & 1) ? MessageCode::TFlipperExtend : MessageCode::TFlipperRetract, 0);
		table->FlipperR->Message((pose & 2) ? MessageCode::TFlipperExtend : MessageCode::TFlipperRetract, 0);
		for (int i = 0; i < 24; ++i)
			pb::timed_frame(1.0f / 120.0f);
		for (int offset = -2; offset <= 2; ++offset)
		{
			ball->Disable();
			ball = table->AddBall({0.25f * offset, 10.15f});
			ball->Direction = {0.0f, 1.0f, 0.0f};
			ball->Speed = 14.0f;
			table->MultiballCount = 2;
			for (int i = 0; i < 108 && ball->ActiveFlag; ++i)
				pb::timed_frame(1.0f / 120.0f);
			if (ball->ActiveFlag && ball->Position.Y < 14.14f)
				++survivors;
			if (!offset && !ball->ActiveFlag)
				++centerDrains;
		}
	}
	fprintf(report, "Ball trajectories retained %d / 20\n", survivors);
	check(centerDrains == 4 && survivors == 2,
	      "Center drains with all four held poses; off-center drops retain native trajectories");

	// Compare a true near miss with a naturally playable shot. Use the real
	// input->flipper->ball path, including the ordinary substep integrator.
	auto launch = [&](vector2 start, vector2 direction, float speed) {
		for (auto b : table->BallList) b->Disable();
		for (auto f : table->FlipperList) f->Message(MessageCode::TFlipperRetract, 0);
		for (int i = 0; i < 24; ++i) pb::timed_frame(1.f / 120);
		ball = table->AddBall(start);
		ball->Direction = {direction.X, direction.Y, 0};
		ball->Speed = speed; table->MultiballCount = 2;
		std::srand(718);
	};
	for (int side : {-1, 1})
	{
		auto bat = table->FlipperL->FlipperEdge->RotOrigin.X * side > 0 ? table->FlipperL : table->FlipperR;
		auto other = bat == table->FlipperL ? table->FlipperR : table->FlipperL;
		auto strokeResult = [&](int press, TFlipper* pressed) {
			for (int i = 0; i < 120 && ball->ActiveFlag; ++i)
			{
				if (i == press) pressed->Message(MessageCode::TFlipperExtend, 0);
				pb::timed_frame(1.f / 120);
				if (ball->Position.Y < 10.f && ball->Direction.Y < 0) return 1;
			}
			return ball->ActiveFlag ? 0 : -1;
		};
		auto shot = [&](float x, float dx, int press, TFlipper* pressed) {
			launch({x * side, 10.4f}, {dx * side, std::sqrt(1.f - dx * dx)}, 14.f);
			return strokeResult(press, pressed);
		};
		check(shot(-.2f, .12f, 12, bat) == (pb::FairMode ? 1 : -1),
			"Timely matching flipper saves otherwise unreachable diagonal drop only in meditative mode");
		check(shot(-.2f, .12f, 12, other) == -1, "Opposite flipper cannot rescue diagonal drop");
		check(shot(-.2f, .12f, 0, bat) == -1, "Early held stroke cannot rescue diagonal drop");
		check(shot(-.2f, .12f, 30, bat) == -1, "Late stroke below the tips cannot rescue the ball");
		check(shot(.4f, 0.f, 12, bat) == 1, "Playable shot uses the original striking surface");
		check(shot(.4f, 0.f, -1, bat) == -1, "Missing a playable shot drains the ball");
		auto grazingShot = [&](float crossingX, float slope, float speed, int press, TFlipper* pressed) {
			const float inv = 1.f / std::sqrt(1.f + slope * slope);
			launch({(crossingX - (12.06f - 10.15f) * slope) * side, 10.15f}, {slope * side * inv, inv}, speed);
			return strokeResult(press, pressed);
		};
		check(grazingShot(0.f, .16f, 16.f, 10, bat) == (pb::FairMode ? 1 : -1),
		      "Glancing diagonal center shot can be caught with a well-timed stroke");
		check(grazingShot(0.f, .16f, 16.f, 10, other) == -1,
		      "Wrong bat still misses the same glancing diagonal");
		check(grazingShot(.08f, 0.f, 35.f, 7, bat) == (pb::FairMode ? 1 : -1),
		      "Fast vertical graze can be caught at the tip");
		check(grazingShot(.08f, 0.f, 35.f, 11, bat) == -1,
		      "Fast vertical graze still drains when the player presses four ticks later");
		for (int rate : {60, 120, 240, 360})
		{
			int saved = 0;
			for (float slope : {0.f, .025f, .30f})
			{
				const float crossingX = slope > .1f ? .08f : 0.f;
				const float initialX = crossingX - (12.06f - 10.15f) * slope;
				const float invLength = 1.f / std::sqrt(1.f + slope * slope);
				launch({initialX * side, 10.15f}, {slope * side * invLength, invLength}, 16.f);
				bool pressed = false;
				for (int step = 0; step < rate && ball->ActiveFlag; ++step)
				{
					if (!pressed && ball->Position.Y >= 11.85f)
					{
						bat->Message(MessageCode::TFlipperExtend, 0);
						pressed = true;
					}
					pb::timed_frame(1.f / rate);
					if (ball->Position.Y < 10.f && ball->Direction.Y < 0.f) { ++saved; break; }
				}
			}
			fprintf(report, "Center returns at %d UPS, side %+d: %d / 3\n", rate, side, saved);
			check(saved == (pb::FairMode ? 3 : 0), "Timed strokes across vertical, near-vertical and diagonal center shots");
		}
	}

	// This ball actually contacts the left rubber while travelling along it.
	// A late press after the ball crosses its tip must not pull it back.
	launch({-1.8f, 12.f}, {1.f, 0.f}, 3.f);
	std::srand(1242);
	bool rebound = false, latePress = false;
	int strokes = 0;
	for (int step = 0; step < 360 && ball->ActiveFlag; ++step)
	{
		if (ball->Position.X >= -.42f)
		{
			latePress = true;
			if (strokes % 4 == 0) table->FlipperL->Message(MessageCode::TFlipperExtend, 0);
			if (strokes % 4 == 2) table->FlipperL->Message(MessageCode::TFlipperRetract, 0);
			++strokes;
		}
		const float downSpeed = ball->Speed * ball->Direction.Y;
		pb::timed_frame(1.f / 120);
		if (!latePress && ball->ActiveFlag && ball->Speed * ball->Direction.Y < downSpeed - .1f)
			rebound = true;
	}
	fprintf(report, "Late roll-off: rubber %d, substeps below tip %d, active %d\n", rebound, strokes, ball->ActiveFlag);
	check(rebound && strokes > 4 && !ball->ActiveFlag, "Ball rolls off native rubber and drains despite repeated late strokes");

	// Drop repeatedly from above each actual outlane switch. The complete
	// physics and timer loop must carry the ball back above the switch.
	const char* gateNames[] = {"v_gate1", "v_gate2"};
	for (int lane = 0; lane < 2; ++lane)
	{
		table->Message(MessageCode::Reset, 0);
		pb::replay_level(false);
		pb::replay_level(false);
		for (int i = 0; i < 160; ++i)
			pb::frame(1000.0f / 120.0f);
		auto gate = static_cast<TGate*>(table->find_component(gateNames[lane]));
		auto kicker = static_cast<TKickback*>(table->find_component(lane ? "a_kick2" : "a_kick1"));
		const int passes = pb::FairMode ? 3 : 2;
		for (int pass = 0; pass < passes; ++pass)
		{
			for (auto active : table->BallList)
				active->Disable();
			ball = table->AddBall({lane ? -5.85f : 7.0f, 7.0f});
			ball->Direction = {0, 1, 0};
			ball->Speed = 8;
			table->MultiballCount = 1;
			auto scoreBefore = table->CurScore;
			int crossingScore = -1;
			float deepest = 0;
			bool returned = false;
			bool springFired = false;
			for (int step = 0; step < 420 && ball->ActiveFlag; ++step)
			{
				const bool springArmed = kicker->KickActiveFlag != 0;
				pb::frame(1000.0f / 120.0f);
				if (springArmed && !kicker->KickActiveFlag)
					springFired = true;
				deepest = std::max(deepest, ball->Position.Y);
				if (crossingScore < 0 && ball->Position.Y >= 8.65f)
					crossingScore = table->CurScore - scoreBefore;
				if (springFired && deepest > 10.5f && ball->Position.Y < 7.3f &&
					ball->Direction.Y < 0 && (lane ? ball->Position.X < 0 : ball->Position.X > 0))
				{
					returned = true;
					break;
				}
			}
			fprintf(report, "Outlane %d pass %d returned %d, depth %.4f, points %d\n",
			        lane + 1, pass + 1, returned, deepest, crossingScore);
			check(crossingScore == 20000, "Outlane rollover awards 20000");
			check((pb::FairMode || !pass) ? returned : !ball->ActiveFlag, "Outlane spring return or original drain");
			ball->Disable();
			for (int i = 0; i < 24; ++i)
				pb::frame(1000.0f / 120.0f);
			check((gate->ActiveFlag == 0) == pb::FairMode, gateNames[lane]);
		}
	}

	ball = table->AddBall({0.0f, 10.15f});
	ball->Direction = {};
	ball->Speed = 0;
	ball->CollisionComp = nullptr;
	nudge::reset();
	const int baselineTick = pb::time_ticks;
	nudge::nudge_right();
	const float firstSpeed = ball->Speed;
	nudge::nudge_left();
	check((nudge::nudged_left == 0) == pb::FairMode, "Cross-direction bump cooldown");
	fprintf(report, "Single bump delta speed %.5f\n", firstSpeed);
	check(std::fabs(firstSpeed - (pb::FairMode ? 0.55902f : 1.11803f)) < 0.001f, "Bump impulse strength");
	nudge::un_nudge_left(0, nullptr);
	nudge::un_nudge_right(0, nullptr);
	if (pb::FairMode)
	{
		pb::time_ticks = baselineTick + 1249;
		nudge::nudge_up();
		check(!nudge::nudged_up, "Bump blocked before 1250 ms");
		pb::time_ticks = baselineTick + 1250;
		nudge::nudge_up();
		check(nudge::nudged_up != 0, "Bump available at 1250 ms");
		nudge::un_nudge_up(0, nullptr);
	}
	pb::time_ticks = baselineTick;
	table->BallInDrainFlag = 0;
	table->tilt(pb::time_now);
	check((table->TiltLockFlag == 0) == pb::FairMode, "Tilt penalty is mode-specific");

	// Verify persistence with the current mode's real settings and checksum.
	auto& record = high_score::highscore_table[0];
	const auto saved = record;
	const char* ownKey = pb::FairMode ? "Fair.0.Score" : "0.Score";
	const char* otherKey = pb::FairMode ? "0.Score" : "Fair.0.Score";
	auto otherValue = options::get_int(otherKey, -999);
	record = {"QA", 1234567};
	high_score::write();
	check(options::get_int(ownKey, 0) == 1234567 && options::get_int(otherKey, -999) == otherValue,
	      "Saving a high score touches only the active mode");
	record = {};
	high_score::read();
	check(record.Score == 1234567 && !strcmp(record.Name, "QA"), "Active high score survives read and verification");
	record = saved;
	high_score::write();
	return ok;
}
