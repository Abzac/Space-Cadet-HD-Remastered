#pragma once
#include "EffectsChecks4.h"
#include "TLight.h"

namespace ballSpinChecks70
{
inline bool reflectionCenter(const std::string& stem, const SDL_Rect& crop, vector2 center,
                             float radius, vector2& result)
{
	auto off=SDL_LoadBMP((stem+"-0.bmp").c_str()), on=SDL_LoadBMP((stem+"-1.bmp").c_str());
	float weight=0.f;
	if (off && on && off->w==on->w && off->h==on->h && off->format->format==on->format->format)
	{
		const int bytes=on->format->BytesPerPixel;
		for (int y=0; y<on->h; ++y)
			for (int x=0; x<on->w; ++x)
			{
				const float u=crop.x+(x+.5f)/HdRenderer::Scale-center.X;
				const float v=crop.y+(y+.5f)/HdRenderer::Scale-center.Y;
				if (u*u+v*v > radius*radius) continue;
				const auto a=static_cast<const Uint8*>(off->pixels)+y*off->pitch+x*bytes;
				const auto b=static_cast<const Uint8*>(on->pixels)+y*on->pitch+x*bytes;
				const float rise=float(std::max({0,int(b[0])-a[0],int(b[1])-a[1],int(b[2])-a[2]}));
				weight += rise;
				result.X += rise*u;
				result.Y += rise*v;
			}
	}
	SDL_FreeSurface(off);
	SDL_FreeSurface(on);
	if (weight<1.f) return false;
	result.X/=weight; result.Y/=weight;
	return true;
}

inline bool run(FILE* report, const std::string& prefix)
{
	auto table = pb::MainTable;
	table->Message(MessageCode::Reset, 0);
	auto ball = table->AddBall({0, 0});
	if (!ball) return false;
	bool good = true;
	auto check = [&](const char* message, bool result) {
		fprintf(report, "%s  %s\n", result ? "PASS" : "FAIL", message);
		good &= result;
	};
	const float radius = ball->Radius;
	auto energy = [&]() {
		const auto& w = ball->AngularVelocity;
		return .5f*ball->Speed*ball->Speed + .2f*radius*radius*(w.X*w.X+w.Y*w.Y+w.Z*w.Z);
	};
	ball->Direction = {1, 0, 0};
	ball->Speed = 3.f;
	ball->AngularVelocity = {};
	const float initialEnergy = energy();
	for (int i=0; i<240; ++i) ball->ApplyPlayfieldFriction(1.f/120.f);
	check("floor friction brings slip to rolling for the solid sphere", fabsf(ball->Speed - 15.f/7.f)<.001f &&
	      fabsf(ball->Speed - radius*ball->AngularVelocity.Y)<.001f && energy() < initialEnergy);
	const auto rollingSpeed = ball->Speed;
	ball->ApplyPlayfieldFriction(.25f);
	check("rolling equilibrium has zero additional sliding loss", fabsf(ball->Speed - rollingSpeed) < .0001f);

	vector2 normal{1,0}, point{0,0};
	float side[2]{};
	for (int i=0; i<2; ++i)
	{
		ball->AngularVelocity = {0, 0, (i?1.f:-1.f)*8.f/radius};
		ball->Direction = {-1,0,0};
		ball->Speed = 10.f;
		const auto energyBefore = energy();
		maths::basic_collision(ball,&point,&normal,1.f,1.f,1e9f,0);
		side[i] = ball->Speed*ball->Direction.Y;
		check("passive rail contact transfers side spin without creating energy",
		      energy() < energyBefore && fabsf(ball->AngularVelocity.Z) < 8.f/radius);
	}
	fprintf(report,"Opposite side spins at the same rail leave with tangent speeds %.3f and %.3f.\n",side[0],side[1]);
	check("opposite side spin changes the outgoing trajectory symmetrically",side[1]>.15f && fabsf(side[0]+side[1])<.001f);

	auto other=table->AddBall({0,0});
	if (!other) return false;
	ball->Position={2*radius,0,radius};
	ball->Speed=10.f;
	ball->Direction={-1,0,0};
	ball->AngularVelocity={0,0,8.f/radius};
	other->EdgeCollision(ball,0.f);
	const float away = ball->Direction.Y*ball->Speed, opposite=other->Direction.Y*other->Speed;
	check("sphere-to-sphere friction shares torque and preserves linear momentum",
	      away>0.f && fabsf(away+opposite)<.0001f && other->AngularVelocity.Z<0.f &&
	      fabsf(ball->Speed*ball->Direction.X+other->Speed*other->Direction.X+10.f)<.001f);
	other->Disable();

	ball->AngularVelocity = {0,0,6};
	ball->Orientation = {};
	for (int i=0; i<120; ++i) ball->AdvanceOrientation(1.f/120.f);
	const auto q = ball->Orientation;
	ball->Orientation = {};
	ball->AdvanceOrientation(1.f);
	check("orientation is independent of render frequency and subdivision at fixed angular speed",
	      fabsf(q.z-sinf(3.f))<.00002f && fabsf(q.w-cosf(3.f))<.00002f &&
	      fabsf(ball->Orientation.w-q.w)<.00002f && fabsf(ball->Orientation.z-q.z)<.00002f);
	ball->Disable();
	ball = table->AddBall({0,0});
	if (!ball) return false;
	check("reused ball starts with zero angular velocity and identity orientation",
	      !maths::magnitude(ball->AngularVelocity) && ball->Orientation.w==1.f);
	auto lamp = dynamic_cast<TLight*>(table->find_component("lite16"));
	if (!lamp) return false;
	const bool previouslyLit = lamp->LightOnFlag;
	const auto ground = proj::ReverseXForm({80,322});
	ball->Position = {ground.X,ground.Y,radius};
	ball->AngularVelocity = {0.f,0.f,4.2f};
	ball->Repaint();
	float imageRadius;
	const auto center = proj::BallImage(ball->Position, radius, imageRadius);
	const SDL_Rect crop{int(center.X)-14,int(center.Y)-14,29,29};
	uint32_t hashes[2][2]{};
	uint32_t repeated{};
	for (int pose=0; pose<2; ++pose)
	{
		for (int lit=0; lit<2; ++lit)
		{
			lamp->Message(lit?MessageCode::TLightTurnOn:MessageCode::TLightTurnOff,0);
			render::update();
			const auto path = prefix+"spin70-"+std::to_string(pose)+"-"+std::to_string(lit)+".bmp";
			good &= visualChecks::capture(path,crop,hashes[pose][lit],&center,imageRadius-.4f,0.f);
			if (lit && pose)
			{
				ball->Repaint();
				render::update();
				good &= visualChecks::capture(prefix+"spin70-repeat.bmp",crop,repeated,&center,imageRadius-.4f,0.f);
			}
		}
		ball->AdvanceOrientation(.32f);
		ball->Repaint();
	}
	check("fixed ball center: steel turns with spin, stationary poses repeat, native lamp changes both reflections",
	      hashes[0][1]!=hashes[1][1] && repeated==hashes[1][1] &&
	      hashes[0][0]!=hashes[0][1] && hashes[1][0]!=hashes[1][1]);
	vector2 highlights[2]{};
	const bool litBoth=reflectionCenter(prefix+"spin70-0",crop,center,imageRadius-.4f,highlights[0]) &&
	                   reflectionCenter(prefix+"spin70-1",crop,center,imageRadius-.4f,highlights[1]);
	const float shift=maths::Distance(highlights[0],highlights[1]);
	fprintf(report,"Same lamp after 77 degrees of material rotation: reflected light centroid shifted %.3f of %.3f pixel radius.\n",shift,imageRadius);
	check("lamp reflection stays anchored in the cabinet",litBoth && shift < .2f*imageRadius);
	if (SDL_GetHintBoolean("SPACECADET_CAPTURE_SPIN",SDL_FALSE))
	{
		// Fixed-center diagnostic isolates angular motion from translation and
		// moving lights. Each frame uses the real sphere and its physical pose.
		const auto still = ball->Position;
		ball->Speed=0.f;
		for (int i=0; i<60; ++i)
		{
			ball->ApplyPlayfieldFriction(1.f/30.f);
			ball->AdvanceOrientation(1.f/30.f);
			ball->Repaint();
			render::update();
			good &= visualChecks::capture(prefix+"spin70-frame-"+std::to_string(i)+".bmp",crop,repeated,nullptr,0,0.f);
		}
		good &= still.X==ball->Position.X && still.Y==ball->Position.Y;
	}
	lamp->Message(previouslyLit?MessageCode::TLightTurnOn:MessageCode::TLightTurnOff,0);
	table->Message(MessageCode::Reset, 0);
	return good;
}
}
