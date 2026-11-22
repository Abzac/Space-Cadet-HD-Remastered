#include "pch.h"
#include "TPlunger.h"
#include "HdRenderer.h"


#include "control.h"
#include "loader.h"
#include "maths.h"
#include "pb.h"
#include "render.h"
#include "TBall.h"
#include "timer.h"
#include "TPinballTable.h"

TPlunger::TPlunger(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, true)
{
	visualStruct visual{};

	loader::query_visual(groupIndex, 0, &visual);
	Boost = 0.0;
	PullbackTimer_ = 0;
	SoundIndexP1 = visual.SoundIndex4;
	SoundIndexP2 = visual.SoundIndex3;
	HardHitSoundId = visual.Kicker.HardHitSoundId;
	Threshold = 1000000000.0;

    // FT:MaxPullback = 50; 3DPB: MaxPullback = 100, PullbackIncrement is floored.
    if (pb::FullTiltMode)
    {
        MaxPullback = 50;
        PullbackIncrement = MaxPullback / (ListBitmap->size() * 8.0f);
    }
    else
    {
        MaxPullback = 100;
        PullbackIncrement = std::floor(MaxPullback / (ListBitmap->size() * 8.0f));
    }

	Elasticity = 0.5f;
	Smoothness = 0.5f;
	
	PullbackDelay = 0.025f;
	float* floatArr = loader::query_float_attribute(groupIndex, 0, 601);
	table->PlungerPosition = {floatArr[0], floatArr[1]};
	if (RenderSprite && groupIndex == 473 && HdRenderer::HasPlungerMotion())
		RenderSprite->HdPlunger = this;
}

void TPlunger::Collision(TBall* ball, vector2* nextPosition, vector2* direction, float distance, TEdgeSegment* edge)
{
	if (PinballTable->TiltLockFlag || SomeCounter > 0) 
	{
		auto boost = RandFloat() * MaxPullback * 0.1f + MaxPullback;
		maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, 0, boost);
		ball->AwaitingPlungerStrike = false;
		if (SomeCounter)
			SomeCounter--;
		Message(MessageCode::PlungerInputReleased, 0.0);
	}
	else 
	{
		auto boost = RandFloat() * Boost * 0.1f + Boost;
		const float rebound = maths::basic_collision(ball, nextPosition, direction, Elasticity, Smoothness, Threshold, boost);
		// A missed release still leaves a ball to shoot. A weak shot can
		// return and physically land on the resting plunger for another try.
		ball->AwaitingPlungerStrike = !(rebound >= Threshold && boost > 0.f);
	}
}

bool TPlunger::SetAnalogPull(float amount)
{
	if (SomeCounter || (!AnalogPull && PullbackStartedFlag)) return false;
	if (!AnalogPull)
	{
		AnalogPull=true;
		AnalogPeak=0;
		PullbackStartedFlag=true;
		Threshold=1000000000.f;
		timer::kill(ReleasedTimer);
		loader::play_sound(HardHitSoundId,this,"TPlunger1");
	}
	amount=std::max(0.f,std::min(1.f,amount));
	AnalogPeak=std::max(AnalogPeak,amount);
	Boost=MaxPullback*amount;
	SpriteSet(int(std::round(amount*(ListBitmap->size()-1))));
	return true;
}
void TPlunger::ReleaseAnalog()
{
	if (!AnalogPull) return;
	AnalogPull=false;
	Boost=MaxPullback*AnalogPeak;
	Message(MessageCode::PlungerInputReleased,0);
}
void TPlunger::CancelAnalog()
{
	if (!AnalogPull) return;
	CancelUserPull();
}
void TPlunger::CancelUserPull()
{
	if (!PullbackStartedFlag || SomeCounter) return;
	if (PullbackTimer_) timer::kill(PullbackTimer_);
	PullbackTimer_=0;
	AnalogPull=PullbackStartedFlag=false;
	AnalogPeak=Boost=0;
	Threshold=1000000000.f;
	SpriteSet(0);
}
int TPlunger::Message(MessageCode code, float value)
{
	if (AnalogPull)
	{
		if (code==MessageCode::PlungerInputPressed || code==MessageCode::PlungerInputReleased) return 0;
		if (code==MessageCode::Reset || code==MessageCode::LooseFocus || code==MessageCode::Pause ||
			code==MessageCode::SetTiltLock || code==MessageCode::PlayerChanged || code==MessageCode::PlungerLaunchBall ||
			code==MessageCode::PlungerRelaunchBall) CancelAnalog();
	}
	switch (code)
	{
	case MessageCode::PlungerInputPressed:
		if (!PullbackStartedFlag && (!pb::FullTiltMode || PinballTable->MultiballCount > 0 && !PinballTable->
			TiltLockFlag))
		{
			PullbackStartedFlag = true;
			Boost = 0.0;
			Threshold = 1000000000.0;
			loader::play_sound(HardHitSoundId, this, "TPlunger1");
			PullbackTimer(0, this);
		}
		break;
	case MessageCode::PlungerFeedBall:
		{
			if (PinballTable->BallCountInRect(PinballTable->PlungerPosition, PinballTable->CollisionCompOffset * 1.2f))
			{
				timer::set(1.0f, this, BallFeedTimer);
			}
			else
			{
				auto ball = PinballTable->AddBall(PinballTable->PlungerPosition);
				assertm(ball, "Failure to create ball in plunger");
				PinballTable->MultiballCount++;
				ManualFeedPending = false;
				if (ball) ball->AwaitingPlungerStrike = true;
				PinballTable->BallInDrainFlag = 0;
				pb::tilt_no_more();
			}
			break;
		}
	case MessageCode::PlungerStartFeedTimer:
		ManualFeedPending = timer::set(0.95999998f, this, BallFeedTimer) != 0;
		loader::play_sound(SoundIndexP1, this, "TPlunger2");
		break;
	case MessageCode::PlungerLaunchBall:
		PullbackStartedFlag = true;
		Boost = MaxPullback;
		Message(MessageCode::PlungerInputReleased, 0.0f);
		break;
	case MessageCode::PlungerRelaunchBall:
		SomeCounter++;
		timer::set(value, this, BallFeedTimer);
		loader::play_sound(SoundIndexP1, this, "TPlunger2_1");
		PullbackStartedFlag = true;
		PullbackTimer(0, this);
		break;
	case MessageCode::PlayerChanged:
		ManualFeedPending = false;
		PullbackStartedFlag = false;
		Boost = 0.0f;
		Threshold = 1000000000.0f;
		SomeCounter = 0;
		timer::kill(BallFeedTimer);
		timer::kill(PullbackTimer);
		timer::kill(ReleasedTimer);
		break;
	case MessageCode::SetTiltLock:
		ManualFeedPending = false;
		SomeCounter = 0;
		timer::kill(BallFeedTimer);
		break;
	case MessageCode::PlungerInputReleased:
	case MessageCode::Resume:
	case MessageCode::LooseFocus:
		if (PullbackStartedFlag && !SomeCounter)
		{
			PullbackStartedFlag = false;
			Threshold = 0.0;
			if (PullbackTimer_)
				timer::kill(PullbackTimer_);
			PullbackTimer_ = 0;
			loader::play_sound(SoundIndexP2, this, "TPlunger3");
			SpriteSet(0);
			timer::set(PullbackDelay, this, ReleasedTimer);
		}
		break;
	case MessageCode::Reset:
	{
		ManualFeedPending = false;
		PullbackStartedFlag = false;
		Boost = 0.0f;
		Threshold = 1000000000.0f;
		SomeCounter = 0;

		timer::kill(BallFeedTimer);
		timer::kill(PullbackTimer);
		timer::kill(ReleasedTimer);

		SpriteSet(0);
		break;
	}
	default:
		break;
	}

	control::handler(code, this);
	return 0;
}

void TPlunger::BallFeedTimer(int timerId, void* caller)
{
	auto plunger = static_cast<TPlunger*>(caller);
	plunger->Message(MessageCode::PlungerFeedBall, 0.0);
}

void TPlunger::PullbackTimer(int timerId, void* caller)
{
	auto plunger = static_cast<TPlunger*>(caller);
	plunger->Boost += plunger->PullbackIncrement;
	if (plunger->Boost <= plunger->MaxPullback)
	{
		if (plunger->SomeCounter)
		{
			plunger->PullbackTimer_ = timer::set(plunger->PullbackDelay / 4.0f, plunger, PullbackTimer);
		}
		else
		{
			plunger->PullbackTimer_ = timer::set(plunger->PullbackDelay, plunger, PullbackTimer);
		}
	}
	else
	{
		plunger->PullbackTimer_ = 0;
		plunger->Boost = plunger->MaxPullback;
	}

	int index = static_cast<int>(floor(
		static_cast<float>(plunger->ListBitmap->size() - 1) *
		(plunger->Boost / plunger->MaxPullback)));
	plunger->SpriteSet(index);
}

void TPlunger::ReleasedTimer(int timerId, void* caller)
{
	auto plunger = static_cast<TPlunger*>(caller);
	plunger->Threshold = 1000000000.0;
	plunger->Boost = 0.0;
}
