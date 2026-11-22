#pragma once

#include "control.h"
#include "pb.h"
#include "TBall.h"
#include "TBumper.h"
#include "TFlipper.h"
#include "TLight.h"
#include "TLightBargraph.h"
#include "TLine.h"
#include "TOneway.h"
#include "TPinballTable.h"
#include "TPopupTarget.h"
#include "TRamp.h"
#include "TRollover.h"
#include "TSink.h"
#include "timer.h"

// Check the Windows rule set using the actual actors, collisions and timers.
// The integration harness runs this on a disposable table with the supplied DAT.
inline bool verifyTableRules(FILE* report)
{
	auto table = pb::MainTable;
	bool ok = true;
	auto check = [&](bool result, const char* name) {
		fprintf(report, "%s %s\n", result ? "PASS" : "FAIL", name);
		ok &= result;
	};
	auto checkScore = [&](int before, int expected, const char* name) {
		fprintf(report, "%s score delta %d (expected %d)\n", name, table->CurScore - before, expected);
		check(table->CurScore - before == expected, name);
	};
	// Progress native timer callbacks but hold ball motion between contacts.
	auto advance = [](float seconds) {
		const int steps = static_cast<int>(std::ceil(seconds * 100.0f));
		for (int i = 0; i < steps; ++i)
		{
			pb::time_ticks += 10;
			timer::check();
		}
	};
	auto newGame = [&]() {
		table->Message(MessageCode::Reset, 0);
		pb::replay_level(false);
		pb::replay_level(false); // Skip the intro light show via normal F2 behavior.
		advance(1.1f);
	};
	auto component = [table](const char* name) { return table->find_component(name); };
	auto light = [&](const char* name) { return static_cast<TLight*>(component(name)); };
	newGame();
	auto ball = table->BallList[0];
	check(ball->ActiveFlag != 0, "Game feeds a ball into the launcher");
	auto contact = [&](TCollisionComponent* actor, TEdgeSegment* edge) {
		vector2 point{(actor->AABB.XMin + actor->AABB.XMax) * 0.5f,
		              (actor->AABB.YMin + actor->AABB.YMax) * 0.5f};
		vector2 normal{0.0f, -1.0f};
		ball->Direction = {0.0f, 1.0f, 0.0f};
		ball->Speed = 20.0f;
		actor->Collision(ball, &point, &normal, 0, edge);
	};
	auto hit = [&](const char* name) {
		auto actor = static_cast<TCollisionComponent*>(component(name));
		if (actor->ActiveFlag)
			contact(actor, actor->EdgeList.front());
	};

	// Deploy, select Launch Training at cadet rank, train three times and
	// return to mission selection. Each ramp crossing is its real scoring edge.
	auto chute = static_cast<TOneway*>(component("s_onewy10"));
	contact(chute, chute->Line);
	check(light("lite198")->MessageField == 1, "Deployment opens mission selection");
	if (pb::FairMode)
		check(component("fuel_bargraph")->Message(MessageCode::TLightGroupGetOnCount, 0) == 11,
		      "Meditative ball already has a full tank before its first fuel rollover");
	int before = table->CurScore;
	hit("a_roll184");
	checkScore(before, 500, "Fuel lane supplies mission fuel");
	check(component("fuel_bargraph")->Message(MessageCode::TLightGroupGetOnCount, 0) == 11, "Full fuel bar");
	before = table->CurScore;
	hit("a_targ13");
	checkScore(before, 1000, "Cadet mission target");
	check(light("lite56")->light_on() && light("lite56")->MessageField == 3, "Launch Training selected");
	auto ramp = static_cast<TRamp*>(component("ramp"));
	before = table->CurScore;
	contact(ramp, ramp->Line1);
	checkScore(before, 10000, "Launch Training acceptance");
	check(light("lite198")->MessageField == 3 && light("lite56")->MessageField == 3 &&
	      light("lite317")->light_on(), "Mission ramp indicator and three remaining shots");
	auto fuel = static_cast<TLightBargraph*>(component("fuel_bargraph"));
	float tankLifetime = 0.f;
	for (size_t i = 0; i < 2 * fuel->List.size(); ++i) tankLifetime += fuel->TimerTimeArray[i];
	const auto scoreAtStart = table->CurScore;
	const auto ballsAtStart = table->BallCount;
	advance(tankLifetime + 2.f);
	fprintf(report, "Fuel test %.2f simulated seconds, level %d, mission %d, balls %d\n",
		tankLifetime+2.f, fuel->TimeIndex, light("lite198")->MessageField, table->BallCount);
	check(table->BallCount == ballsAtStart && table->CurScore == scoreAtStart,
		"Fuel expiry costs no ball and grants no points");
	check(pb::FairMode ? fuel->TimeIndex == 11 && light("lite198")->MessageField == 3 :
		fuel->TimeIndex == 0 && light("lite198")->MessageField == 1,
		"Classic fuel expires; meditative fuel and active mission remain");
	if (!pb::FairMode)
	{
		newGame();
		contact(chute, chute->Line);
		hit("a_roll184");
		hit("a_targ13");
		contact(ramp, ramp->Line1);
	}
	for (int remaining = 2; remaining >= 0; --remaining)
	{
		before = table->CurScore;
		contact(ramp, ramp->Line1);
		checkScore(before, remaining ? 5000 : 505000, "Training ramp and completion award");
		check(light("lite56")->MessageField == remaining, "Launch Training countdown");
	}
	check(light("lite198")->MessageField == 1 && !light("lite317")->light_on(), "Completed mission returns to selection");

	newGame();
	auto bumper = static_cast<TBumper*>(component("a_bump1"));
	before = table->CurScore;
	hit("a_bump1");
	checkScore(before, 500, "Base attack bumper");
	check(bumper->Timer != 0, "Bumper entered hit state");
	advance(bumper->TimerTime + 0.1f);
	check(bumper->Timer == 0 && bumper->Threshold == bumper->OriginalThreshold, "Bumper released and rearmed");
	auto crossLane = [&](const char* name) {
		auto lane = static_cast<TRollover*>(component(name));
		int entryScore = table->CurScore;
		contact(lane, lane->EdgeList.front());
		check(lane->RolloverFlag != 0 && table->CurScore - entryScore == 2000, "Reentry lane press awards 2000");
		contact(lane, lane->EdgeList.back());
		check(!lane->RolloverFlag && !lane->ActiveFlag && table->CurScore - entryScore == 2000,
		      "Lane release does not score twice");
		advance(0.14f);
		check(lane->ActiveFlag != 0, "Lane active after release timer");
	};
	crossLane("a_roll1");
	check(light("lite10")->light_on(), "Reentry lamp lit on first crossing");
	table->FlipperL->Message(MessageCode::TFlipperExtend, 0);
	check(!light("lite10")->light_on() && (light("lite9")->light_on() || light("lite8")->light_on()),
	      "Left flipper cycles the active reentry lamp");
	table->FlipperR->Message(MessageCode::TFlipperExtend, 0);
	check(light("lite10")->light_on(), "Right flipper cycles the lamp back");
	crossLane("a_roll1");
	check(!light("lite10")->light_on(), "Repeated lane crossing cancels its lamp");
	crossLane("a_roll1");
	crossLane("a_roll2");
	crossLane("a_roll3");
	check(bumper->BmpIndex == 1 && light("lite8")->FlasherOnFlag, "Three lanes upgrade the attack bumpers and flash");
	advance(bumper->TimerTime + 0.15f);
	before = table->CurScore;
	hit("a_bump1");
	checkScore(before, 1000, "Upgraded attack bumper");

	// Resolve each branch of the wormhole scoring/routing decision. Confirm
	// actual ejection coordinates and speed after the destination timer fires.
	const int entrances[] = {2, 1, 0}, destinations[] = {2, 0, 0}, points[] = {2500, 7500, 5000};
	for (int route = 0; route < 3; ++route)
	{
		if (route)
		{
			before = table->CurScore;
			hit("a_targ22");
			checkScore(before, 750, "Wormhole destination target");
			check(light("lite4")->MessageField == 1 && light("lite110")->light_on(), "Wormhole destination arrow selected");
		}
		auto entry = control::WormholeSinkArray[entrances[route]].get();
		auto exit = control::WormholeSinkArray[destinations[route]].get();
		before = table->CurScore;
		contact(entry, entry->EdgeList.front());
		checkScore(before, points[route], "Wormhole capture");
		check(!ball->ActiveFlag && control::WormholeLightArray1[destinations[route]].get()->FlasherOnFlag,
		      "Ball captured and destination lamp flashing");
		advance(entry->TimerTime + 0.2f);
		check(ball->ActiveFlag && maths::Distance(ball->Position, exit->BallPosition) < 0.005f && ball->Speed > 0,
		      "Wormhole ejects at the selected physical exit");
	}
	check(light("lite199")->light_on(), "Matching wormhole grants replay");

	auto target7 = static_cast<TPopupTarget*>(component("a_targ7"));
	auto target8 = static_cast<TPopupTarget*>(component("a_targ8"));
	auto target9 = static_cast<TPopupTarget*>(component("a_targ9"));
	before = table->CurScore;
	hit("a_targ7");
	checkScore(before, 500, "First drop target");
	check(!target7->ActiveFlag && target7->MessageField == 1 && !target7->Timer, "First target stays down");
	before = table->CurScore;
	hit("a_targ8");
	checkScore(before, 500, "Second drop target");
	check(!target8->ActiveFlag && target8->MessageField == 1, "Second target stays down");
	before = table->CurScore;
	hit("a_targ9");
	checkScore(before, 1500, "Full multiplier bank");
	check(table->ScoreMultiplier == 1 && target7->Timer && target8->Timer && target9->Timer,
	      "Three targets activate 2x and schedule bank reset");
	advance(std::max(target7->TimerTime, std::max(target8->TimerTime, target9->TimerTime)) + 0.2f);
	check(target7->ActiveFlag && target8->ActiveFlag && target9->ActiveFlag, "All three targets pop back up");
	before = table->CurScore;
	hit("a_bump1");
	checkScore(before, 2000, "2x applies to upgraded attack bumper");
	return ok;
}
