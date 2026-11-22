#pragma once

#include "EffectsChecks4.h"
#include "InputChecks.h"
#include "control.h"
#include "TDrain.h"
#include "TLight.h"
#include "TPlunger.h"
#include <array>

namespace starStripChecks63
{
constexpr SDL_Rect crop{115,309,136,107};
struct Sample
{
	double average[2]{};
	int narrowest[2]{255,255};
	int protectedPixels{};
	bool protectedUnchanged = true;
};

inline bool sample(const std::string& stem, float seconds, Sample& result)
{
	uint32_t hash{};
	FieldTraces::SetDiagnosticBypass(true);
	const bool baseline = visualChecks::capture(stem+"-print.bmp",crop,hash,nullptr,0,seconds);
	FieldTraces::SetDiagnosticBypass(false);
	if (!baseline || !visualChecks::capture(stem+".bmp",crop,hash,nullptr,0,seconds)) return false;
	auto a=SDL_LoadBMP((stem+"-print.bmp").c_str()), b=SDL_LoadBMP((stem+".bmp").c_str());
	bool ok=a && b && a->format->BytesPerPixel==4 && b->format->BytesPerPixel==4;
	if (ok)
	{
		// A transverse section of two strokes on each printed ribbon. These
		// include the darker bezel-facing edge as well as the ink proper.
		constexpr int bands[][2]={{626,633},{665,671},{714,720},{740,746}};
		int counts[2]{};
		for (int k=0;k<4;++k)
			for (int x=bands[k][0];x<=bands[k][1];++x)
			{
				const int y=(358-crop.y)*4, i=(x-crop.x*4)*4;
				const int rise=static_cast<const uint8_t*>(b->pixels)[y*b->pitch+i+1]-
					static_cast<const uint8_t*>(a->pixels)[y*a->pitch+i+1];
				const int side=k/2;
				result.average[side]+=rise;
				result.narrowest[side]=std::min(result.narrowest[side],rise);
				++counts[side];
			}
		for (int s=0;s<2;++s) result.average[s]/=counts[s];
		for (int y=0;y<a->h;++y)
			for (int x=0;x<a->w;++x)
			{
				const auto p=static_cast<uint8_t*>(a->pixels)+y*a->pitch+x*4;
				if (std::min({p[0],p[1],p[2]})<174) continue;
				const auto q=static_cast<uint8_t*>(b->pixels)+y*b->pitch+x*4;
				result.protectedUnchanged &= memcmp(p,q,3)==0;
				++result.protectedPixels;
			}
		for (const SDL_Rect native : {SDL_Rect{168,384,8,8}, SDL_Rect{180,392,5,5}})
		{
			SDL_Rect box{(native.x-crop.x)*4,(native.y-crop.y)*4,native.w*4,native.h*4};
			result.protectedUnchanged &= effectsChecks4::difference(stem+"-print.bmp",stem+".bmp",box).maximum==0;
		}
		ok &= result.protectedUnchanged && result.protectedPixels>1000;
	}
	SDL_FreeSurface(a);
	SDL_FreeSurface(b);
	return ok;
}

inline bool deployment(FILE* report, const std::string& prefix, bool waiting, const char* stage=nullptr)
{
	if (!stage) stage=waiting?"fed":"in-play";
	bool captured=true;
	for (int side=0;side<2;++side)
	{
		Sample frame;
		captured &= sample(prefix+"star-strips632-"+stage+"-"+std::to_string(side),.3f+side*.6f,frame);
		captured &= frame.average[1-side]==0 && (waiting ? frame.narrowest[side]>40 : frame.average[side]==0);
	}
	const bool state=control::AwaitingManualDeployment();
	if (waiting && SDL_GetHintBoolean("SPACECADET_CAPTURE_STRIPS632",SDL_FALSE))
	{
		uint32_t hash{};
		for (int i=0;i<30;++i)
			if (!visualChecks::capture(prefix+"strips632-motion-"+std::to_string(i)+".bmp",
				crop,hash,nullptr,0,i*.04f)) return false;
	}
	fprintf(report,"Deployment strips at %s: hardware waiting %d; both alternating phases match %d.\n",
		stage,state,captured);
	return captured && !pb::MainTable->LightShowTimer && state==waiting &&
		control::HasDeployedBall()==!waiting;
}

inline bool run(FILE* report,const std::string& prefix)
{
	if (!pb::MainTable->LightShowTimer || !control::AwaitingManualDeployment() || control::HasDeployedBall()) return false;
	bool ok=true;
	constexpr float times[]={0.f,.3f,.599f,.601f,.9f,1.199f,1.201f};
	double crest[2]{};
	for (int i=0;i<7;++i)
	{
		const int lit=(i>=3 && i<=5)?1:0;
		Sample frame;
		ok &= sample(prefix+"star-strips632-beat-"+std::to_string(i),times[i],frame);
		ok &= frame.average[1-lit]==0 && frame.average[lit]>52 && frame.narrowest[lit]>.66*frame.average[lit];
		if (i==0 || i==3) crest[lit]=frame.average[lit];
		else ok &= frame.average[lit]==crest[lit];
	}
	fprintf(report,"Alternating ink every 600 ms: full-width pink/violet green rise %.1f/%.1f; off-phase emits zero.\n",crest[0],crest[1]);
	const char* flag="SPACECADET_BREATHE_STAR_OUTLINES";
	const char* prev=SDL_GetHint(flag);
	const bool hadPrevious=prev!=nullptr;
	const std::string previous=prev?prev:"";
	SDL_SetHintWithPriority(flag,"1",SDL_HINT_OVERRIDE);
	std::array<Sample,10> frames{};
	for (int beat=0;beat<int(frames.size());++beat)
		ok &= sample(prefix+"star-strips632-breathe-"+std::to_string(beat),.2f*beat,frames[beat]);
	if (hadPrevious) SDL_SetHintWithPriority(flag,previous.c_str(),SDL_HINT_OVERRIDE);
	else SDL_ResetHint(flag);
	for (int side=0;side<2;++side)
	{
		const int peak=4+side;
		double previous=0;
		for (int k=0;k<=4;++k)
		{
			const auto level=frames[side+k].average[side];
			ok &= k==0 ? level==0 : level>previous+3;
			previous=level;
		}
		for (int k=1;k<=4;++k)
		{
			const auto level=frames[peak+k].average[side];
			ok &= level+3<previous;
			previous=level;
		}
		ok &= frames[peak].narrowest[side]>.66*frames[peak].average[side] && frames[peak].average[side]>52;
		ok &= frames[side+8].average[side]==0;
		fprintf(report,"Archived breathing %s: dark %.1f, crest %.1f, dimmest across full lit width %d, protected hardware %d.\n",
			side?"violet":"pink",frames[side].average[side],frames[peak].average[side],
			frames[peak].narrowest[side],frames[peak].protectedPixels);
	}
	return ok;
}

inline bool replayAndWeakLaunch(FILE* report, const std::string& prefix)
{
	auto table=pb::MainTable;
	auto advance=[](int ticks){for (int i=0;i<ticks;++i) pb::frame(1000.f/120.f);};
	bool continuous=true;
	for (int skip=0;skip<2;++skip)
	{
		table->Message(MessageCode::Reset,0);
		pb::replay_level(false);
		continuous &= table->LightShowTimer && control::AwaitingManualDeployment() && !control::HasDeployedBall();
		if (skip) pb::replay_level(false);
		int delayTicks=0,ticks=0;
		while (!table->BallList.front()->ActiveFlag && ticks++<2000)
		{
			if (!table->LightShowTimer && ++delayTicks==1)
			{
				Sample frame;
				continuous &= sample(prefix+"star-strips632-feed-delay-"+std::to_string(skip),.3f,frame);
				continuous &= frame.narrowest[0]>45 && frame.average[1]==0;
				// Let the spring snap while the shooter is still empty.
				table->Plunger->Message(MessageCode::PlungerInputPressed,0);
				table->Plunger->Message(MessageCode::PlungerInputReleased,0);
			}
			continuous &= control::AwaitingManualDeployment() && !control::HasDeployedBall();
			advance(1);
		}
		continuous &= delayTicks>100 && table->BallList.front()->ActiveFlag &&
			control::AwaitingManualDeployment() && !control::HasDeployedBall();
		fprintf(report,"Start show %s: %d consecutive waiting feed ticks.\n",skip?"skipped":"finished",delayTicks);
	}
	advance(540);
	auto ball=table->BallList.front();
	auto mission=table->find_component("lite198");
	auto save=table->find_component("lite200");
	if (!mission || !save || !control::AwaitingManualDeployment()) return false;
	const auto key=options::Options.Key[static_cast<int>(GameBindings::Plunger)].Inputs[0];
	pb::InputDown(key);
	advance(13);
	pb::InputUp(key);
	bool left=false, returned=false;
	for (int i=0;i<320;++i)
	{
		advance(1);
		if (!control::AwaitingManualDeployment()) left=true;
		else if (left) returned=true;
	}
	const bool countdownSetting=options::Options.ResumeCountdown;
	options::Options.ResumeCountdown=true;
	inputChecks::tap(SDLK_F3);
	const bool pausedAtShooter=returned && winmain::single_step && control::AwaitingManualDeployment();
	inputChecks::tap(SDLK_SPACE);
	const bool returnedResume=pausedAtShooter && winmain::CanAdvanceGame() &&
		!winmain::ResumePending() && !control::HasDeployedBall();
	options::Options.ResumeCountdown=countdownSetting;
	fprintf(report,"F3 then Space after a weak shot returned to the spring: immediate resume %d.\n",returnedResume);
	// A saved ball leaves its mission controller in place while passing
	// through the real drain, feed timer and automatic delivery sequence.
	mission->MessageField=1;
	save->Message(MessageCode::TLightResetAndTurnOn,0);
	table->Drain->Collision(ball,nullptr,nullptr,0,nullptr);
	const bool waitingDuringDrain=control::AwaitingManualDeployment();
	int ticks=0;
	bool savedFeedPending=false;
	while (ticks<1800 && !ball->ActiveFlag)
	{
		continuous &= !control::HasDeployedBall();
		savedFeedPending |= control::AwaitingManualDeployment();
		advance(1);
		++ticks;
	}
	const bool fed=ball->ActiveFlag && mission->MessageField==1 && control::AwaitingManualDeployment();
	fprintf(report,"Launch cue: weak launch departed %d and returned %d; drain %d, saved-ball delivery %d at tick %d.\n",
		left,returned,waitingDuringDrain,fed,ticks);
	// A second ball on the table must still get its countdown when the
	// other one is sitting on the plunger.
	auto second=table->AddBall({table->PlungerPosition.X-10.f,table->PlungerPosition.Y-10.f});
	if (!second) return false;
	++table->MultiballCount;
	bool multi=control::HasDeployedBall() && !control::AwaitingManualDeployment();
	table->Message(MessageCode::Reset,0);
	table->Plunger->Message(MessageCode::PlungerRelaunchBall,.1f);
	bool automatic=true,sawAutoFeed=false,sawAutoStrike=false;
	for (int i=0;i<240 && !sawAutoStrike;++i)
	{
		automatic &= !control::AwaitingManualDeployment();
		sawAutoFeed |= table->MultiballCount>0 && !control::HasDeployedBall();
		sawAutoStrike=control::HasDeployedBall();
		advance(1);
	}
	table->Message(MessageCode::GameOver,0);
	const bool finished=!control::AwaitingManualDeployment() && !control::HasDeployedBall();
	fprintf(report,"Independent deployed ball %d, automatic feed %d and strike %d, game-over lights off %d.\n",
		multi,sawAutoFeed,sawAutoStrike,finished);
	return continuous && left && returnedResume && !waitingDuringDrain && savedFeedPending && fed &&
		multi && automatic && sawAutoFeed && sawAutoStrike && finished;
}
}
