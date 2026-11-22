#pragma once

#include "CabinetArtChecks.h"

namespace spinnerRestChecks62
{
inline bool faceVisible(const std::string& path, SDL_Rect crop, bool right)
{
	auto saved = SDL_LoadBMP(path.c_str());
	auto image = saved ? SDL_ConvertSurfaceFormat(saved, SDL_PIXELFORMAT_ARGB8888, 0) : nullptr;
	SDL_FreeSurface(saved);
	if (!image) return false;
	constexpr int scale = HdRenderer::Scale;
	int bright = 0, total = 0;
	const float axisX = right ? .891f : 1.f, axisY = right ? .454f : .012f;
	for (int y=0; y<image->h; ++y)
		for (int x=0; x<image->w; ++x)
		{
			const float dx = crop.x+(x+.5f)/scale-(right?256.55f:74.17f);
			const float dy = crop.y+(y+.5f)/scale-(right?136.85f:108.95f);
			const float v = fabsf(-dx*axisY+dy*axisX);
			if (fabsf(dx*axisX+dy*axisY)>3.3f || v<1.4f || v>2.5f) continue;
			const auto row = reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(image->pixels)+y*image->pitch);
			const Uint32 pixel=row[x];
			const int r=(pixel>>16)&255, g=(pixel>>8)&255, b=pixel&255;
			bright += std::min(r,std::min(g,b))>145 && std::max(r,std::max(g,b))-std::min(r,std::min(g,b))<55;
			++total;
		}
	SDL_FreeSurface(image);
	return total>0 && bright*4 >= total*3;
}

inline bool run(FILE* report, const std::string& prefix)
{
	bool ok=true;
	for (int side : {1,2})
	{
		int edgeStops=0, faceStops=0;
		for (bool reverse : {false,true})
			for (int speed : {5,10,18,28})
			{
				spinnerTrace::freshTable();
				auto table=pb::MainTable;
				auto p=static_cast<TFlagSpinner*>(table->find_component(side==1 ? "a_flag1" : "a_flag2"));
				auto ball=table->BallList.front();
				auto edge=static_cast<TLine*>(reverse ? p->PrevCollider : p->EdgeList.front());
				const auto normal=edge->Line.PerpendicularC;
				const float lead=ball->Radius*1.35f;
				ball->Position={(edge->X0+edge->X1)*.5f+normal.X*lead,(edge->Y0+edge->Y1)*.5f+normal.Y*lead,ball->Radius};
				ball->Direction={-normal.X,-normal.Y,0};
				ball->Speed=float(speed);
				ball->ActiveFlag=1;
				ball->CollisionComp=nullptr;
				ball->CollisionFlag=0;
				ball->CollisionDisabledFlag=false;
				ball->EdgeCollisionCount=0;
				ball->CollisionMask=edge->CollisionGroup;
				int steps=0;
				while (!p->BmpIndex && steps++<32) pb::frame(1000.f/120);
				if (!p->BmpIndex) { ok=false; continue; }
				// The crossing uses the real collision sweep. Remove the ball so a
				// second collision cannot disturb the switch as it comes to rest.
				ball->Disable();
				for (int ms=0; ms<1400; ++ms) spinnerTrace::tick();
				render::update();
				const int index=p->BmpIndex, phase=p->VisualPhase(), score=table->CurScore;
				const bool edgeOn=(index&3)==1;
				const auto crop=cabinetArtChecks::pad(cabinetArtChecks::rect(p->RenderSprite->BoundingRect),12,10);
				const std::string stem=prefix+"rest-"+std::to_string(side)+"-"+(reverse?"back":"front")+"-"+std::to_string(speed)+".bmp";
				uint32_t hash=0;
				const bool visible=visualChecks::capture(stem,crop,hash) && faceVisible(stem,crop,side==2);
				for (int ms=0; ms<800; ++ms) spinnerTrace::tick();
				render::update();
				const bool stable=index==p->BmpIndex && phase==p->VisualPhase() && score==table->CurScore;
				const int expected=(4*index+(edgeOn?4*p->SpinDirection:0)+32)%32;
				const bool pass=visible && stable && phase==expected;
				fprintf(report,"%s  spinner %d %s speed %d: DAT %d, resting visual %d, white face %d, stable %d\n",
					pass?"PASS":"FAIL",side,reverse?"back":"front",speed,index,phase,visible,stable);
				if (edgeOn) ++edgeStops; else ++faceStops;
				ok &= pass;
				if (edgeOn)
				{
					// Two glancing collisions from opposite directions can both
					// fall below the threshold for scheduling another scoring step.
					spinnerTrace::cross(p,!reverse,0.f);
					p->RefreshVisualPhase();
					bool continuous = p->VisualPhase()==phase;
					for (int ms=0; ms<45; ++ms) spinnerTrace::tick();
					p->RefreshVisualPhase();
					const int between=p->VisualPhase();
					spinnerTrace::cross(p,reverse,0.f);
					p->RefreshVisualPhase();
					continuous &= p->VisualPhase()==between;
					const int postHitsScore=table->CurScore;
					for (int ms=0; ms<850; ++ms) spinnerTrace::tick();
					p->RefreshVisualPhase();
					const bool weak=continuous && p->BmpIndex==index && p->VisualPhase()==expected && table->CurScore==postHitsScore;
					fprintf(report,"%s  spinner %d repeated gentle hits pick up its current pose and settle without extra scoring\n",
						weak?"PASS":"FAIL",side);
					ok &= weak;
				}
			}
		ok &= edgeStops>0 && faceStops>0;
	}
	return ok;
}
}
