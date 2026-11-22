#include "pch.h"
#include "TFlagSpinner.h"
#include "HdRenderer.h"
#include "pb.h"


#include "control.h"
#include "loader.h"
#include "render.h"
#include "TBall.h"
#include "timer.h"
#include "TLine.h"
#include "TPinballTable.h"

TFlagSpinner::TFlagSpinner(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};
	vector2 end{}, start{};

	if (!pb::FullTiltMode && (groupIndex == 481 || groupIndex == 489) && RenderSprite && ListBitmap &&
		ListBitmap->size() == 8 && HdRenderer::HasSpinnerFrames(ListBitmap->at(3).Bmp))
		RenderSprite->Spinner = this;
	loader::query_visual(groupIndex, 0, &visual);
	end.X = visual.FloatArr[0];
	end.Y = visual.FloatArr[1];
	start.X = visual.FloatArr[2];
	start.Y = visual.FloatArr[3];
	auto line = new TLine(this, &ActiveFlag, visual.CollisionGroup, start, end);
	if (line)
	{
		line->place_in_grid(&AABB);
		EdgeList.push_back(line);
	}

	line = new TLine(this, &ActiveFlag, visual.CollisionGroup, end, start);
	PrevCollider = line;
	if (line)
	{
		line->place_in_grid(&AABB);
		EdgeList.push_back(line);
	}

	SpeedDecrement = 0.64999998f;
	MaxSpeed = 50000.0f;
	MinSpeed = 5.0f;
	auto speedDec = loader::query_float_attribute(groupIndex, 0, 1202);
	if (speedDec)
		SpeedDecrement = *speedDec;
	auto maxSpeed = loader::query_float_attribute(groupIndex, 0, 1200);
	if (maxSpeed)
		MaxSpeed = *maxSpeed;
	auto minSpeed = loader::query_float_attribute(groupIndex, 0, 1201);
	if (minSpeed)
		MinSpeed = *minSpeed;
}

TFlagSpinner::~TFlagSpinner()
{
	CancelTurns();
	if (RenderSprite) RenderSprite->Spinner = nullptr;
}

void TFlagSpinner::CancelTurns()
{
	for (const auto& turn : pending) timer::kill(turn.timer);
	pending.clear();
}

int TFlagSpinner::Message(MessageCode code, float value)
{
	if (code == MessageCode::Reset)
	{
		CancelTurns();
		visualStart = pb::time_ticks;
		restartPhase = -1;
		BmpIndex = 0;
		SpriteSet(BmpIndex);
	}
	return 0;
}

void TFlagSpinner::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance,
                             TEdgeSegment* edge)
{
	ball->Position.X = nextPosition->X;
	ball->Position.Y = nextPosition->Y;
	ball->RayMaxDistance = ball->RayMaxDistance - distance;
	ball->not_again(edge);

	// A stopped HD paddle may have settled a few visual phases beyond the
	// switch. Pick it up where it is, including a new strike from the back.
	int parked = -1;
	if (RenderSprite && RenderSprite->Spinner && pending.empty())
	{
		RefreshVisualPhase();
		if (visualPhase != BmpIndex * 4) parked = visualPhase;
	}
	SpinDirection = 2 * (PrevCollider != edge) - 1;
	if (ball->Speed == 0.0f)
		Speed = MinSpeed;
	else
		Speed = ball->Speed * 20.0f;
	if (Speed < MinSpeed)
		Speed = MinSpeed;
	if (Speed > MaxSpeed)
		Speed = MaxSpeed;
	NextFrame();
	restartPhase = parked;
}

void TFlagSpinner::NextFrame()
{
	restartPhase = -1;
	BmpIndex += SpinDirection;
	int bmpIndex = BmpIndex;
	int bmpCount = static_cast<int>(ListBitmap->size());
	if (bmpIndex >= bmpCount)
		BmpIndex = 0;
	else if (bmpIndex < 0)
		BmpIndex = bmpCount - 1;

	if (!PinballTable->TiltLockFlag)
	{
		control::handler(MessageCode::ControlCollision, this);
		if (SoftHitSoundId)
			loader::play_sound(SoftHitSoundId, this, "TFlagSpinner");
		if (!BmpIndex)
			control::handler(MessageCode::ControlSpinnerLoopReset, this);
	}

	SpriteSet(BmpIndex);
	visualStart = pb::time_ticks;
	Speed *= SpeedDecrement;
	if (Speed >= MinSpeed)
	{
		const float delay = 1.0f / Speed;
		const int id = timer::set(delay, this, SpinTimer);
		if (id) pending.push_back({id, visualStart + static_cast<int>(delay * 1000.0f)});
	}
}

void TFlagSpinner::SpinTimer(int timerId, void* caller)
{
	auto spinner = static_cast<TFlagSpinner*>(caller);
	auto& pending = spinner->pending;
	pending.erase(std::remove_if(pending.begin(), pending.end(),
		[timerId](const Turn& turn) { return turn.timer == timerId; }), pending.end());
	spinner->NextFrame();
}

void TFlagSpinner::RefreshVisualPhase()
{
	int phase = BmpIndex * 4;
	const int elapsed = std::max(0, pb::time_ticks - visualStart);
	if (!pending.empty())
	{
		// A second contact may have posted a turn ahead of the most recent
		// one. Interpolate only up to the next real scoring callback.
		const auto next = std::min_element(pending.begin(), pending.end(),
			[](const Turn& a, const Turn& b) { return a.due < b.due; });
		const int duration = std::max(1, next->due - visualStart);
		const int from = restartPhase < 0 ? phase : restartPhase;
		const int distance = (SpinDirection * (phase-from) + 4 + 32) % 32;
		const int substep = std::min(elapsed,duration-1) * distance / duration;
		phase = (from + SpinDirection * substep + 32) % 32;
	}
	else if (restartPhase >= 0 && elapsed < 120)
	{
		// A very light strike can end after a single native step.
		const int distance = (phase - restartPhase + 48) % 32 - 16;
		phase = (restartPhase + distance * elapsed / 120 + 32) % 32;
	}
	else if ((BmpIndex & 3) == 1)
	{
		// DAT positions 1 and 5 show the blade edge-on. Once the last scoring
		// turn has finished, let its face tip into view on the fixed axle.
		const int settling = elapsed - (restartPhase >= 0 ? 120 : 0);
		const int coast = (settling>=32) + (settling>=76) + (settling>=136) + (settling>=220);
		phase = (phase + SpinDirection * coast + 32) % 32;
	}
	if (phase != visualPhase)
	{
		visualPhase = phase;
		RenderSprite->DirtyFlag = true;
	}
}

void TFlagSpinner::PaintHd(const rectangle_type& dirty) const
{
	rectangle_type clip{};
	const auto& bounds = RenderSprite->BoundingRect;
	if (!maths::rectangle_clip(bounds, dirty, &clip)) return;
	const auto& carrier = ListBitmap->at(3);
	const int sx = clip.XPosition - bounds.XPosition, sy = clip.YPosition - bounds.YPosition;
	HdRenderer::Paint(render::vscreen, clip.Width, clip.Height, clip.XPosition, clip.YPosition,
		carrier.Bmp, sx, sy, carrier.ZMap->ZPtr1 + sy * carrier.ZMap->Stride + sx, carrier.ZMap->Stride, visualPhase);
}
