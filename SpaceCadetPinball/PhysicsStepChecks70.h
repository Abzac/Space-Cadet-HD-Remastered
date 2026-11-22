#pragma once
#include "pb.h"
#include "TBall.h"
#include "TFlipper.h"
#include "TFlipperEdge.h"
#include "TLine.h"
#include "TPinballTable.h"
#include "TTableLayer.h"

namespace physicsStepChecks70
{
// Known analytical paths through the production grid, ray intersections,
// time integrator and rebound function. The fixture supplies flat geometry
// without the shipped table's random lateral acceleration.
struct Surface : TCollisionComponent
{
	enum class Kind { Wall, LaneSwitch, Catch, Jam, Refeed };
	Kind kind;
	int hits{};
	explicit Surface(Kind k) : TCollisionComponent(nullptr,-1,false), kind(k) {}
	void Collision(TBall* ball, vector2* position, vector2* normal, float, TEdgeSegment* edge) override
	{
		++hits;
		if (kind == Kind::Jam) return;
		if (kind == Kind::Refeed)
		{
			ball->Disable();
			auto refed=pb::MainTable->AddBall({1.f,2.f});
			refed->Direction={1.f,0.f,0.f};
			refed->Speed=12.f;
			return;
		}
		if (kind == Kind::Wall)
		{
			maths::basic_collision(ball,position,normal,Elasticity,1.f,0.f,Boost,0.f,0.f);
			return;
		}
		ball->Position.X = position->X;
		ball->Position.Y = position->Y;
		if (kind == Kind::LaneSwitch)
		{
			ball->CollisionMask = 2;
			ball->not_again(edge);
		}
		else
		{
			ball->CollisionComp = this;
			ball->CollisionDisabledFlag = true;
		}
	}
};

inline bool run(FILE* report)
{
	auto table = pb::MainTable;
	auto original = TTableLayer::edge_manager;
	TEdgeManager grid(-4.f,-4.f,8.f,8.f);
	TTableLayer::edge_manager = &grid;
	const float originalGravity = table->GravityDirVectMult;
	table->GravityDirVectMult = 0.f;
	constexpr float dt = .005f;
	const float radius = table->BallList.front()->Radius;
	const float travel = .4f*radius, speed = travel/dt;
	Surface nearWall(Surface::Kind::Wall), farWall(Surface::Kind::Wall);
	Surface sensor(Surface::Kind::LaneSwitch), cup(Surface::Kind::Catch);
	TLine right(&nearWall,&nearWall.ActiveFlag,1,.25f*travel,1.f,.25f*travel,-1.f);
	TLine left(&farWall,&farWall.ActiveFlag,1,-.15f*travel,-1.f,-.15f*travel,1.f);
	TLine gate(&sensor,&sensor.ActiveFlag,1,.2f*travel,1.f,.2f*travel,-1.f);
	TLine catchLine(&cup,&cup.ActiveFlag,2,.6f*travel,1.f,.6f*travel,-1.f);
	for (auto line : {&right,&left,&gate,&catchLine}) line->place_in_grid(nullptr);
	auto reset = [&]() {
		for (auto ball : table->BallList) ball->Disable();
		for (auto surface : {&nearWall,&farWall,&sensor,&cup})
		{
			surface->hits=0; surface->ActiveFlag=0;
			surface->Elasticity=1.f; surface->Boost=0.f;
		}
	};
	auto launch = [&](float x, float vx) {
		auto ball = table->AddBall({x,0.f});
		ball->Direction = {vx < 0.f ? -1.f : 1.f,0.f,0.f};
		ball->Speed = std::fabs(vx);
		ball->AngularVelocity = {0.f,vx/radius,0.f};
		return ball;
	};
	bool ok=true;
	auto check = [&](bool pass,const char* label) {
		fprintf(report,"%s  %s\n",pass?"PASS":"FAIL",label);
		ok &= pass;
	};
	auto close = [](float x,float expected){ return std::fabs(x-expected)<.0001f; };
	reset();
	auto ball=launch(0.f,speed);
	pb::timed_frame(dt);
	check(close(ball->Position.X,travel),"unobstructed ball consumes its exact tick");

	reset(); nearWall.ActiveFlag=1; nearWall.Elasticity=.5f;
	ball=launch(0.f,speed);
	pb::timed_frame(dt);
	check(nearWall.hits==1 && close(ball->Speed,.5f*speed) && close(ball->Position.X,-.125f*travel-.0005f),
	      "inelastic bounce travels only the remaining time at half speed");

	reset(); nearWall.ActiveFlag=1; nearWall.Boost=speed;
	ball=launch(0.f,speed);
	pb::timed_frame(dt);
	check(nearWall.hits==1 && close(ball->Speed,2.f*speed) && close(ball->Position.X,-1.25f*travel-.0005f),
	      "powered bumper travels the remaining time at its boosted speed");

	reset(); nearWall.ActiveFlag=1; farWall.ActiveFlag=1;
	ball=launch(0.f,speed);
	pb::timed_frame(dt);
	check(nearWall.hits==1 && farWall.hits==1 && close(ball->Position.X,.2f*travel+.001f),
	      "two successive reflections share one time budget");

	reset(); sensor.ActiveFlag=1;
	ball=launch(0.f,speed);
	pb::timed_frame(dt);
	check(sensor.hits==1 && ball->CollisionMask==2 && close(ball->Position.X,travel),
	      "a pass-through switch does not add travel or double-trigger");

	reset(); sensor.ActiveFlag=1; cup.ActiveFlag=1;
	ball=launch(0.f,speed);
	pb::timed_frame(dt);
	check(sensor.hits==1 && cup.hits==1 && ball->CollisionComp==&cup && close(ball->Position.X,.6f*travel),
	      "the new collision layer captures the ball within the same time slice");

	reset(); ball=launch(0.f,speed);
	const float start=2.f*radius+.6f*travel;
	auto other=launch(start,-speed);
	pb::timed_frame(dt);
	check(close(ball->Position.X,-.4f*travel) && close(other->Position.X,start+.4f*travel) &&
	      ball->Direction.X<0.f && other->Direction.X>0.f,
	      "both moving balls advance to contact and use the same remaining time");

	for (int hz : {60,120,240})
	{
		reset(); nearWall.ActiveFlag=1; nearWall.Elasticity=.5f;
		ball=launch(0.f,travel*60.f);
		for (int i=0;i<hz/60;++i) pb::timed_frame(1.f/hz);
		const float reflected=ball->Position.X;
		const bool wallOk=nearWall.hits==1 && close(reflected,-.125f*travel-.0005f);
		reset(); ball=launch(0.f,travel*60.f); other=launch(start,-travel*60.f);
		for (int i=0;i<hz/60;++i) pb::timed_frame(1.f/hz);
		const bool pairOk=close(ball->Position.X,-.4f*travel) && close(other->Position.X,start+.4f*travel);
		fprintf(report,"At %d Hz: isolated bank x=%.7f, meeting spheres x=%.7f / %.7f.\n",
		        hz,reflected,ball->Position.X,other->Position.X);
		check(wallOk && pairOk,"wall and ball-pair time of impact match analytic positions at this update rate");

		reset();
		ball = launch(0.f, travel * 60.f);
		const float initialTargetX = 2.f * radius + .25f * travel;
		other = launch(initialTargetX, 0.f);
		for (int tick = 0; tick < hz / 60; ++tick) pb::timed_frame(1.f / hz);
		fprintf(report, "At %d Hz: initial resting target x=%.7f, travel after strike %.7f.\n",
		        hz, initialTargetX, other->Position.X - initialTargetX);
		check(close(ball->Position.X, .25f * travel) && close(other->Position.X, initialTargetX + .75f * travel),
		      "an initially stationary target uses the full remaining clock after a strike");
	}
	reset(); nearWall.ActiveFlag=1; nearWall.kind=Surface::Kind::Jam;
	ball=launch(.25f*travel,speed); other=launch(0.f,speed); other->Position.Y=2.f;
	pb::timed_frame(dt);
	check(nearWall.hits>0 && nearWall.hits<=32 && close(other->Position.X,travel),
	      "overlapping zero-distance contacts cannot stall the other ball");

	reset(); nearWall.ActiveFlag=1; nearWall.kind=Surface::Kind::Refeed;
	ball=launch(0.f,speed); other=launch(0.f,speed); other->Position.Y=2.f;
	pb::timed_frame(dt);
	check(nearWall.hits==1 && ball->ActiveFlag && close(ball->Position.X,1.f) && close(other->Position.X,travel),
	      "slot reused by a contact starts a new ball on the next tick");
	nearWall.kind=Surface::Kind::Wall;

	reset();
	for (auto flipper : table->FlipperList) flipper->Message(MessageCode::Reset,0.f);
	for (float sweeps : {.6f,1.25f,2.25f,3.8f})
	{
		int counts[2]{};
		float angles[2]{};
		int index=0;
		for (auto bat : {table->FlipperL,table->FlipperR})
		{
			bat->Message(MessageCode::TFlipperExtend,0.f);
			auto edge=bat->FlipperEdge;
			const float duration=sweeps/std::fabs(edge->MoveSpeed*edge->DistanceDiv*edge->InvT1Radius);
			counts[index]=bat->GetFlipperStepAngle(duration,&angles[index]);
			bat->Message(MessageCode::Reset,0.f);
			++index;
		}
		check(counts[0]==std::min(3,int(std::ceil(sweeps))) && counts[0]==counts[1] && angles[0]*angles[1]<0.f,
		      "mirrored signed flipper arcs use equal angular subdivisions");
	}
	TTableLayer::edge_manager=original;
	table->GravityDirVectMult=originalGravity;
	return ok;
}
}
