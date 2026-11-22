#include "pch.h"
#include "TFlipper.h"


#include "control.h"
#include "loader.h"
#include "pb.h"
#include "render.h"
#include "TBall.h"
#include "TFlipperEdge.h"

#include "timer.h"
#include "TPinballTable.h"

TFlipper::TFlipper(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false)
{
	visualStruct visual{};

	loader::query_visual(groupIndex, 0, &visual);
	HardHitSoundId = visual.SoundIndex4;
	SoftHitSoundId = visual.SoundIndex3;
	Elasticity = visual.Elasticity;
	Smoothness = visual.Smoothness;

	auto collMult = *loader::query_float_attribute(groupIndex, 0, 803);
	auto retractTime = *loader::query_float_attribute(groupIndex, 0, 805);
	auto extendTime = *loader::query_float_attribute(groupIndex, 0, 804);
	auto vecT2 = *reinterpret_cast<vector3*>(loader::query_float_attribute(groupIndex, 0, 802));
	auto vecT1 = *reinterpret_cast<vector3*>(loader::query_float_attribute(groupIndex, 0, 801));
	auto origin = reinterpret_cast<vector3*>(loader::query_float_attribute(groupIndex, 0, 800));
	auto flipperEdge = new TFlipperEdge(
		this,
		&ActiveFlag,
		visual.CollisionGroup,
		table,
		origin,
		&vecT1,
		&vecT2,
		extendTime,
		retractTime,
		collMult,
		Elasticity,
		Smoothness);
	flipperEdge->place_in_grid(&AABB);

	FlipperEdge = flipperEdge;
	BmpIndex = 0;
	if (table)
		table->FlipperList.push_back(this);
}

TFlipper::~TFlipper()
{
	delete FlipperEdge;
	if (PinballTable)
	{
		auto& flippers = PinballTable->FlipperList;
		auto position = std::find(flippers.begin(), flippers.end(), this);
		if (position != flippers.end())
			flippers.erase(position);
	}
}

int TFlipper::Message(MessageCode code, float value)
{
	switch (code)
	{
	case MessageCode::TFlipperExtend:
	case MessageCode::TFlipperRetract:
	case MessageCode::Resume:
	case MessageCode::LooseFocus:
	case MessageCode::SetTiltLock:
	case MessageCode::GameOver:
		if (code == MessageCode::TFlipperExtend)
		{
			control::handler(MessageCode::TFlipperExtend, this);
			loader::play_sound(HardHitSoundId, this, "TFlipper1");
		}
		else if (code == MessageCode::TFlipperRetract)
		{
			loader::play_sound(SoftHitSoundId, this, "TFlipper2");
		}
		else
		{
			// Retract for all non-input messages
			code = MessageCode::TFlipperRetract;
		}

		MessageField = FlipperEdge->SetMotion(code);
		break;
	case MessageCode::PlayerChanged:
	case MessageCode::Reset:
		if (MessageField)
		{
			FlipperEdge->CurrentAngle = 0;
			FlipperEdge->set_control_points(0);
			MessageField = 0;
			FlipperEdge->SetMotion(MessageCode::Reset);
			UpdateSprite();
		}
		break;
	default: break;
	}

	return 0;
}

void TFlipper::port_draw()
{
	FlipperEdge->port_draw();
}

void TFlipper::Collision(TBall*, vector2*, vector2*, float, TEdgeSegment*)
{
	// TFlipperEdge handles contacts for the moving and stationary bat.
}

void TFlipper::UpdateSprite()
{
	auto bmpCountSub1 = static_cast<int>(ListBitmap->size()) - 1;
	auto newBmpIndex = static_cast<int>(floor(FlipperEdge->CurrentAngle / FlipperEdge->AngleMax * bmpCountSub1 + 0.5f));
	newBmpIndex = Clamp(newBmpIndex, 0, bmpCountSub1);
	if (BmpIndex == newBmpIndex)
		return;

	BmpIndex = newBmpIndex;
	SpriteSet(BmpIndex);
}

namespace
{
float distanceToSegmentSq(vector2 p, vector2 from, vector2 to)
{
	const vector2 line{to.X - from.X, to.Y - from.Y};
	const float lengthSq = maths::magnitudeSq(line);
	const vector2 offset{p.X - from.X, p.Y - from.Y};
	const float t = lengthSq > 0 ? Clamp(maths::DotProduct(offset, line) / lengthSq, 0.f, 1.f) : 0.f;
	return maths::Distance_Squared(p, {from.X + t * line.X, from.Y + t * line.Y});
}

bool crossesNativeReach(vector2 from, vector2 to, const TFlipperEdge& edge, float ballRadius)
{
	// A tangent graze on the very outside of the swept rubber can still send
	// a ball down the middle. Count a real opportunity once there is enough
	// overlap to strike; any actual native contact always latches separately.
	const float usableReach = edge.DistanceDiv - .2f * ballRadius;
	return distanceToSegmentSq(edge.RotOrigin, from, to) <= usableReach * usableReach;
}
}

void TFlipper::TrackDrainApproach(TBall& ball, const TPinballTable& table)
{
	const auto& left = *table.FlipperL->FlipperEdge;
	const auto& right = *table.FlipperR->FlipperEdge;
	if (!ball.ActiveFlag || ball.CollisionComp || !(ball.CollisionMask & left.CollisionGroup) ||
		ball.Position.Y < std::min(left.T2Src.Y, right.T2Src.Y) - 2 * ball.Radius)
	{
		ball.HadFlipperOpportunity = false;
		return;
	}
	const vector2 here = ball.Position;
	if (crossesNativeReach(here, here, left, ball.Radius) || crossesNativeReach(here, here, right, ball.Radius))
		ball.HadFlipperOpportunity = true;
}

bool TFlipper::TryTipSave(TBall& ball, float deltaAngle)
{
	auto& bat = *FlipperEdge;
	if (ball.HadFlipperOpportunity || !ball.ActiveFlag || ball.CollisionComp ||
		!(ball.CollisionMask & bat.CollisionGroup) || ball.Direction.Y * ball.Speed < 1.f)
		return false;

	const auto& other = *(PinballTable->FlipperL == this ? PinballTable->FlipperR : PinballTable->FlipperL)->FlipperEdge;
	const float centerX = .5f * (bat.RotOrigin.X + other.RotOrigin.X);
	const float towardBat = bat.RotOrigin.X > centerX ? 1.f : -1.f;
	const float vx = ball.Direction.X * ball.Speed;
	const float vy = ball.Direction.Y * ball.Speed;
	// For an oblique approach only the bat toward which the ball travels gets
	// the extra reach. On a vertical drop the nearer bat is eligible.
	const float lineY = .5f * (bat.RotOrigin.Y + other.RotOrigin.Y);
	const float crossingX = ball.Position.X + vx * (lineY - ball.Position.Y) / vy;
	const bool movingAcrossGap = std::abs(vx) > .09f * vy;
	const bool wrongSide = movingAcrossGap ? vx * towardBat < 0.f :
		(crossingX - centerX) * towardBat < -.35f * ball.Radius;
	if (wrongSide || ball.Position.Y < bat.T2Src.Y + .8f * ball.Radius ||
	    ball.Position.Y > bat.T1Src.Y + .5f * ball.Radius)
		return false;

	if (bat.ControlPointDirtyFlag) bat.set_control_points(bat.CurrentAngle);
	const float length = bat.DistanceDiv - bat.CircleT1Radius;
	const vector2 radialToBall{ball.Position.X - bat.RotOrigin.X, ball.Position.Y - bat.RotOrigin.Y};
	const float restGap = std::abs(bat.T1Src.X - other.T1Src.X) - bat.CircleT1Radius - other.CircleT1Radius;
	const float extra = std::min(1.8f * ball.Radius, .5f * (std::max(0.f, restGap) + ball.Radius));
	const float sweptLength = Clamp(std::sqrt(maths::magnitudeSq(radialToBall)), length, length + extra);
	const vector2 tip{bat.RotOrigin.X + (bat.T1.X - bat.RotOrigin.X) * sweptLength / length,
	                  bat.RotOrigin.Y + (bat.T1.Y - bat.RotOrigin.Y) * sweptLength / length};
	vector2 nextTip = tip;
	float sn, cs;
	maths::SinCos(deltaAngle, sn, cs);
	maths::RotatePt(nextTip, sn, cs, bat.RotOrigin);
	// Allow a glancing contact within 0.16 ball diameters of the moving tip.
	const float catchRadius = bat.CircleT1Radius + .32f * ball.Radius;
	if (distanceToSegmentSq(ball.Position, tip, nextTip) > catchRadius * catchRadius)
		return false;

	// Follow the falling ball to the bottom of the playable flipper zone. A
	// native hit available on either bat takes precedence over assistance.
	const float exitY = std::max(bat.T1Src.Y + bat.CircleT1Radius, other.T1Src.Y + other.CircleT1Radius);
	const auto& table = *PinballTable;
	const float gravity = std::sin(table.GravityAngleX) * table.GravityDirVectMult;
	const float ax = std::cos(table.GravityAnglY) * gravity;
	const float ay = std::sin(table.GravityAnglY) * gravity;
	vector2 from = ball.Position;
	for (int i = 1; i <= 60; ++i)
	{
		const float t = i / 120.f;
		vector2 to{ball.Position.X + vx * t + .5f * ax * t * t,
			ball.Position.Y + vy * t + .5f * ay * t * t};
		if (crossesNativeReach(from, to, bat, ball.Radius) || crossesNativeReach(from, to, other, ball.Radius))
			return false;
		if (to.Y > exitY) break;
		from = to;
	}

	// Apply the ordinary flipper-tip impulse at the ball's actual position.
	// A little radial normal resolves grazing contacts toward the field. There
	// is no stationary collider under the ball and no position snap.
	vector2 normal{nextTip.X - tip.X, nextTip.Y - tip.Y};
	maths::normalize_2d(normal);
	if (normal.Y >= 0.f) return false;
	vector2 radial{ball.Position.X - nextTip.X, ball.Position.Y - nextTip.Y};
	if (maths::DotProduct(radial, normal) < -ball.Radius * .6f) return false;
	maths::normalize_2d(radial);
	normal.X += .28f * radial.X;
	normal.Y += .28f * radial.Y;
	maths::normalize_2d(normal);
	const float boost = std::abs(bat.ExtendSpeed) * bat.CollisionMult;
	vector2 position = ball.Position;
	const float surfaceSpeed = bat.MoveSpeed * ((position.X - bat.RotOrigin.X) * normal.X +
		(position.Y - bat.RotOrigin.Y) * normal.Y - ball.Radius);
	maths::basic_collision(&ball, &position, &normal, bat.Elasticity, bat.Smoothness, -1.f, boost, surfaceSpeed, .22f);
	ball.HadFlipperOpportunity = true;
	return true;
}

int TFlipper::GetFlipperStepAngle(float dt, float* dst) const
{
	if (!MessageField)
		return 0;

	auto deltaAngle = FlipperEdge->flipper_angle_delta(dt);
	auto step = std::ceil(FlipperEdge->DistanceDiv * std::fabs(deltaAngle) * FlipperEdge->InvT1Radius);
	if (step > 3.0f)
		step = 3.0f;
	if (step >= 2.0f)
	{
		*dst = deltaAngle / step;
		return static_cast<int>(step);
	}

	*dst = deltaAngle;
	return 1;
}

void TFlipper::FlipperCollision(float deltaAngle)
{
	if (!MessageField)
		return;

	ray_type ray{}, rayDst{};
	ray.MinDistance = 0.002f;
	bool collisionFlag = false;
	for (auto ball : pb::MainTable->BallList)
	{
		if (!ball->ActiveFlag || ball->CollisionComp || ball->CollisionDisabledFlag)
			continue;
		if ((FlipperEdge->CollisionGroup & ball->CollisionMask) != 0 &&
			FlipperEdge->YMax >= ball->Position.Y && FlipperEdge->YMin <= ball->Position.Y &&
			FlipperEdge->XMax >= ball->Position.X && FlipperEdge->XMin <= ball->Position.X)
		{
			if (FlipperEdge->ControlPointDirtyFlag)
				FlipperEdge->set_control_points(FlipperEdge->CurrentAngle);
			ray.CollisionMask = ball->CollisionMask;
			ray.Origin = ball->Position;

			float sin, cos;
			auto ballPosRot = ray.Origin;
			maths::SinCos(-deltaAngle, sin, cos);
			maths::RotatePt(ballPosRot, sin, cos, FlipperEdge->RotOrigin);
			ray.Direction.X = ballPosRot.X - ray.Origin.X;
			ray.Direction.Y = ballPosRot.Y - ray.Origin.Y;
			ray.MaxDistance = maths::normalize_2d(ray.Direction);
			auto distance = maths::distance_to_flipper(FlipperEdge, ray, rayDst);
			if (distance < 1e9f)
			{
				FlipperEdge->NextBallPosition = ball->Position;
				FlipperEdge->CollisionDirection = rayDst.Direction;
				FlipperEdge->EdgeCollision(ball, distance);
				collisionFlag = true;
			}
		}
		if (pb::FairMode && FlipperEdge->FlipperFlag == MessageCode::TFlipperExtend &&
			TryTipSave(*ball, deltaAngle))
			collisionFlag = true;
	}

	if (collisionFlag)
	{
		auto angleAdvance = deltaAngle / (std::fabs(FlipperEdge->MoveSpeed) * 5.0f);
		FlipperEdge->CurrentAngle -= angleAdvance;
		FlipperEdge->AngleRemainder += std::fabs(angleAdvance);
	}
	else
	{
		FlipperEdge->CurrentAngle += deltaAngle;
		FlipperEdge->AngleRemainder -= std::fabs(deltaAngle);
	}

	if (FlipperEdge->AngleRemainder <= 0.0001f)
	{
		FlipperEdge->CurrentAngle = FlipperEdge->AngleDst;
		FlipperEdge->FlipperFlag = MessageCode::TFlipperNull;
		MessageField = 0;
	}
	FlipperEdge->ControlPointDirtyFlag = true;
}
