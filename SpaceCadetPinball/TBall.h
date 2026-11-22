#pragma once
#include "maths.h"
#include "TCollisionComponent.h"
#include "TEdgeSegment.h"

struct BallOrientation
{
	float x{}, y{}, z{}, w{1.f};
};

class TBall : public TCollisionComponent, public TEdgeSegment
{
public :
	TBall(TPinballTable* table, int groupIndex);
	void Repaint();
	void ApplyPlayfieldFriction(float seconds);
	void AdvanceOrientation(float seconds);
	void ApplyRailFriction(const vector2& normal, float surfaceTangentSpeed, float normalImpulse, float grip);
	void not_again(TEdgeSegment* edge);
	bool already_hit(const TEdgeSegment& edge) const;
	int Message(MessageCode code, float value) override;
	vector2 get_coordinates() override;
	void Disable();
	void throw_ball(vector3* direction, float angleMult, float speedMult1, float speedMult2);
	void place_in_grid(RectF* aabb) override {}
	void EdgeCollision(TBall* ball, float distance) override;
	float FindCollisionDistance(const ray_type& ray) override;

	vector3 Position{};
	vector3 PrevPosition{};
	vector3 Direction{};
	vector3 AngularVelocity{}; // Radians per second, in table coordinates.
	BallOrientation Orientation{};
	float Speed;
	float RayMaxDistance;
	float TimeDelta;
	vector2 RampFieldForce{};
	TCollisionComponent* CollisionComp;
	int CollisionMask;
	TEdgeSegment* Collisions[16]{};
	int EdgeCollisionCount;
	bool EdgeCollisionResetFlag{};
	vector3 CollisionOffset{};
	int CollisionFlag;
	float Radius;
	bool HasGroupFlag;
	int StuckCounter = 0;
	int LastActiveTime{};
	float VisualZArray[50]{};
	bool CollisionDisabledFlag{};
	// Presentation state for this particular ball, including multiball.
	bool AwaitingPlungerStrike{};
	// Latched for this approach to the drain. A ball which rolled off a bat,
	// or already crossed ordinary striking range, gets no late virtual hit.
	bool HadFlipperOpportunity{};
};
