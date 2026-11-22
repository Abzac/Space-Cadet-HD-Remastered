#pragma once
class nudge
{
public:
	static void reset();
	static void un_nudge_right(int timerId, void* caller);
	static void un_nudge_left(int timerId, void* caller);
	static void un_nudge_up(int timerId, void* caller);
	static void nudge_right();
	static void nudge_left();
	static void nudge_up();

	static int nudged_left;
	static int nudged_right;
	static int nudged_up;
	static float nudge_count;
private:
	static bool allow_nudge();
	static int nextFairNudgeTick;
	static void _nudge(float x, float y);
	static int timer;
};
