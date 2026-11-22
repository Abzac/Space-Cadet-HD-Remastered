#include "pch.h"
#include "TBall.h"


#include "fullscrn.h"
#include "HdRenderer.h"
#include "loader.h"
#include "maths.h"
#include "pb.h"
#include "proj.h"
#include "render.h"
#include "TPinballTable.h"
#include "TTableLayer.h"

TBall::TBall(TPinballTable* table, int groupIndex) : TCollisionComponent(table, groupIndex, false),
                                                     TEdgeSegment(this, &ActiveFlag, 0)
{
	visualStruct visual{};
	char ballGroupName[10]{"ball"};

	RayMaxDistance = 0.0;
	ActiveFlag = 1;
	CollisionComp = nullptr;
	EdgeCollisionCount = 0;
	TimeDelta = 0.0;
	CollisionFlag = 0;
	Speed = 0.0;
	Direction.Y = 0.0;
	Direction.X = 0.0;
	ListBitmap = new std::vector<SpriteData>();

	if (groupIndex == -1)
	{
		HasGroupFlag = false;
		Position = {0, 0, 0};
		CollisionMask = 1;
	}
	else
	{
		HasGroupFlag = true;
		loader::query_visual(groupIndex, 0, &visual);
		CollisionMask = visual.CollisionGroup;
		auto floatArr = loader::query_float_attribute(groupIndex, 0, 408);
		Position = {floatArr[0], floatArr[1], floatArr[3]};
	}

	/*Full tilt: ball is ballN, where N[0,2] resolution*/
	groupIndex = loader::query_handle(ballGroupName);
	if (groupIndex < 0)
	{
		ballGroupName[4] = '0' + fullscrn::GetResolution();
		groupIndex = loader::query_handle(ballGroupName);
	}

	Radius = *loader::query_float_attribute(groupIndex, 0, 500);

	auto visualCount = loader::query_visual_states(groupIndex);
	for (auto index = 0; index < visualCount; ++index)
	{
		loader::query_visual(groupIndex, index, &visual);
		ListBitmap->push_back(visual.Bitmap);
		auto visVec = reinterpret_cast<vector3*>(loader::query_float_attribute(groupIndex, index, 501));
		auto zDepth = proj::z_distance(*visVec);
		VisualZArray[index] = zDepth;
	}
	RenderSprite = new render_sprite(VisualTypes::Ball, nullptr, nullptr, 0, 0, nullptr);
	PinballTable->CollisionCompOffset = Radius;
	Position.Z = Radius;
	GroupIndex = groupIndex;
}

void TBall::Repaint()
{
	if (CollisionFlag)
	{
		Position.Z =
			CollisionOffset.X * Position.X +
			CollisionOffset.Y * Position.Y +
			Radius + CollisionOffset.Z;
	}

	auto pos2D = proj::xform_to_2d(Position);
	auto zDepth = proj::z_distance(Position);

	auto index = 0u;
	for (; index < ListBitmap->size() - 1; ++index)
	{
		if (VisualZArray[index] <= zDepth) break;
	}

	SpriteSetBall(index, pos2D, zDepth);
	HdRenderer::TrackBall(RenderSprite, Position, Radius, Orientation);
}

// I = 2/5 mR² for a solid sphere. Contact impulses below are per unit mass.
namespace
{
constexpr float inverseInertiaFactor = 2.5f;
constexpr float contactMass = 1.f / (1.f + inverseInertiaFactor);

void setPlanarVelocity(TBall& ball, float x, float y)
{
	ball.Direction.X = x;
	ball.Direction.Y = y;
	ball.Speed = maths::normalize_2d(ball.Direction);
}
}

void TBall::ApplyPlayfieldFriction(float seconds)
{
	if (seconds <= 0.f || !ActiveFlag || CollisionComp || Radius <= 0.f) return;
	const float vx = Direction.X * Speed, vy = Direction.Y * Speed;
	const float slopeX = CollisionFlag ? CollisionOffset.X : 0.f;
	const float slopeY = CollisionFlag ? CollisionOffset.Y : 0.f;
	const float nz = 1.f / sqrtf(1.f + slopeX*slopeX + slopeY*slopeY);
	const vector3 normal{-slopeX*nz, -slopeY*nz, nz};
	vector3 surfaceSpin;
	maths::cross(AngularVelocity, normal, surfaceSpin);
	const vector3 slip{vx - Radius*surfaceSpin.X, vy - Radius*surfaceSpin.Y,
	                   slopeX*vx + slopeY*vy - Radius*surfaceSpin.Z};
	const float slipSpeed = maths::magnitude(slip);
	if (slipSpeed > .000001f)
	{
		// Oppose sliding at the bottom contact. At rolling equilibrium the
		// impulse is zero; the stock table already supplies rolling resistance.
		const float maxImpulse = .16f * PinballTable->GravityDirVectMult * nz * seconds;
		const float scale = -std::min(contactMass, maxImpulse / slipSpeed);
		const vector3 impulse{slip.X*scale, slip.Y*scale, slip.Z*scale};
		setPlanarVelocity(*this, vx + impulse.X, vy + impulse.Y);
		vector3 torque;
		maths::cross(impulse, normal, torque);
		const float invRadius = inverseInertiaFactor / Radius;
		AngularVelocity.X += torque.X*invRadius;
		AngularVelocity.Y += torque.Y*invRadius;
		AngularVelocity.Z += torque.Z*invRadius;
	}
	// A finite contact patch slowly damps spin about the surface normal.
	const float axial = AngularVelocity.X*normal.X + AngularVelocity.Y*normal.Y + AngularVelocity.Z*normal.Z;
	const float decay = axial * -expm1f(-.85f * seconds);
	AngularVelocity.X -= decay*normal.X;
	AngularVelocity.Y -= decay*normal.Y;
	AngularVelocity.Z -= decay*normal.Z;
}

void TBall::AdvanceOrientation(float seconds)
{
	if (seconds <= 0.f || !ActiveFlag || CollisionComp) return;
	const float speed = maths::magnitude(AngularVelocity);
	if (speed < .000001f) return;
	const float halfAngle = .5f * speed * seconds;
	const float s = sinf(halfAngle) / speed, w = cosf(halfAngle);
	const float x = AngularVelocity.X*s, y = AngularVelocity.Y*s, z = AngularVelocity.Z*s;
	const auto q = Orientation;
	// Left-multiply because angular velocity is expressed in world coordinates.
	Orientation = {w*q.x + x*q.w + y*q.z - z*q.y, w*q.y - x*q.z + y*q.w + z*q.x,
	               w*q.z + x*q.y - y*q.x + z*q.w, w*q.w - x*q.x - y*q.y - z*q.z};
	auto& a = Orientation;
	const float inv = 1.f / sqrtf(a.x*a.x + a.y*a.y + a.z*a.z + a.w*a.w);
	a.x *= inv; a.y *= inv; a.z *= inv; a.w *= inv;
}

void TBall::ApplyRailFriction(const vector2& normal, float surfaceTangentSpeed, float normalImpulse, float grip)
{
	if (Radius <= 0.f || normalImpulse <= 0.f) return;
	const vector2 tangent{-normal.Y, normal.X};
	const float vx = Direction.X * Speed, vy = Direction.Y * Speed;
	const float slip = vx*tangent.X + vy*tangent.Y - Radius*AngularVelocity.Z - surfaceTangentSpeed;
	const float limit = grip*normalImpulse;
	const float impulse = Clamp(-slip*contactMass, -limit, limit);
	setPlanarVelocity(*this, vx + impulse*tangent.X, vy + impulse*tangent.Y);
	AngularVelocity.Z -= impulse*inverseInertiaFactor/Radius;
}

void TBall::not_again(TEdgeSegment* edge)
{
	if (EdgeCollisionCount < 16)
	{
		Collisions[EdgeCollisionCount] = edge;
		++EdgeCollisionCount;
	}
	else
	{
		for (int i = 0; i < 8; i++)
			Collisions[i] = Collisions[i + 8];
		Collisions[8] = edge;
		EdgeCollisionCount = 9;
	}
	EdgeCollisionResetFlag = true;
}

bool TBall::already_hit(const TEdgeSegment& edge) const
{
	for (int i = 0; i < EdgeCollisionCount; i++)
	{
		if (Collisions[i] == &edge)
			return true;
	}

	return false;
}

int TBall::Message(MessageCode code, float value)
{
	if (code == MessageCode::Reset)
	{
		AwaitingPlungerStrike = false;
		SpriteSetBall(-1, {0, 0}, 0.0f);
		Position.X = 0.0;
		CollisionComp = nullptr;
		Position.Y = 0.0;
		ActiveFlag = 0;
		CollisionFlag = 0;
		CollisionMask = 1;
		Direction.Y = 0.0;
		Position.Z = Radius;
		Direction.X = 0.0;
		Speed = 0.0;
		AngularVelocity = {};
		Orientation = {};
		RayMaxDistance = 0.0;
	}
	return 0;
}

void TBall::throw_ball(vector3* direction, float angleMult, float speedMult1, float speedMult2)
{
	CollisionComp = nullptr;
	Direction = *direction;
	float rnd = RandFloat();
	float angle = (1.0f - (rnd + rnd)) * angleMult;
	maths::RotateVector(Direction, angle);
	rnd = RandFloat();
	Speed = (1.0f - (rnd + rnd)) * (speedMult1 * speedMult2) + speedMult1;
}

void TBall::EdgeCollision(TBall* ball, float distance)
{
	ball->Position.X += ball->Direction.X * distance;
	ball->Position.Y += ball->Direction.Y * distance;
	ball->Direction.X *= ball->Speed;
	ball->Direction.Y *= ball->Speed;
	Direction.X *= Speed;
	Direction.Y *= Speed;

	// AB - vector from ball to this, BA - from this to ball; collision direction
	vector2 AB{ball->Position.X - Position.X, ball->Position.Y - Position.Y};
	maths::normalize_2d(AB);
	vector2 BA{-AB.X, -AB.Y};

	// Projection = difference between ball directions and collision direction
	auto projAB = -maths::DotProduct(ball->Direction, AB);
	auto projBA = -maths::DotProduct(Direction, BA);
	vector2 delta{AB.X * projAB - BA.X * projBA, AB.Y * projAB - BA.Y * projBA};

	ball->Direction.X += delta.X;
	ball->Direction.Y += delta.Y;
	Direction.X -= delta.X;
	Direction.Y -= delta.Y;
	// Equal-mass spheres meet at their equators. A single bounded friction
	// impulse transfers tangent momentum and torque to both surfaces.
	const vector2 tangent{-AB.Y, AB.X};
	const float slip = (ball->Direction.X - Direction.X)*tangent.X +
	                   (ball->Direction.Y - Direction.Y)*tangent.Y -
	                   ball->Radius*ball->AngularVelocity.Z - Radius*AngularVelocity.Z;
	const float limit = .045f * std::max(0.f, projAB + projBA);
	const float impulse = Clamp(-.5f*contactMass*slip, -limit, limit);
	ball->Direction.X += tangent.X*impulse;
	ball->Direction.Y += tangent.Y*impulse;
	Direction.X -= tangent.X*impulse;
	Direction.Y -= tangent.Y*impulse;
	ball->AngularVelocity.Z -= inverseInertiaFactor*impulse/ball->Radius;
	AngularVelocity.Z -= inverseInertiaFactor*impulse/Radius;
	ball->Speed = maths::normalize_2d(ball->Direction);
	Speed = maths::normalize_2d(Direction);
}

float TBall::FindCollisionDistance(const ray_type& ray)
{
	// Original inherits TCircle and aliases position.
	const circle_type ballCircle{{Position.X, Position.Y}, Radius * Radius * 4.0f};
	return maths::ray_intersect_circle(ray, ballCircle);
}

vector2 TBall::get_coordinates()
{
	return TTableLayer::edge_manager->NormalizeBox(Position);
}

void TBall::Disable()
{
	AngularVelocity = {};
	Orientation = {};
	AwaitingPlungerStrike = false;
	ActiveFlag = false;
	CollisionDisabledFlag = true;
	SpriteSet(-1);
}
