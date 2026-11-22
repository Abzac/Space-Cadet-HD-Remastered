#pragma once

#include "ResumeBallCue.h"
#include "fullscrn.h"
#include "proj.h"
#include "winmain.h"

namespace resumeBallChecks62
{
inline std::vector<Uint32> patch(SDL_Rect box, float progress, const std::string& file = {})
{
	auto renderer = winmain::Renderer;
	ImGui_ImplSDL2_NewFrame();
	ImGui_Render_NewFrame();
	ImGui::NewFrame();
	ResumeBallCue::Draw(progress);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	render::PresentVScreen(pb::time_now);
	ImGui::Render();
	ImGui_Render_RenderDrawData(ImGui::GetDrawData());
	std::vector<Uint32> pixels(box.w*box.h);
	if (SDL_RenderReadPixels(renderer, &box, SDL_PIXELFORMAT_ARGB8888, pixels.data(), box.w*4) < 0)
		pixels.clear();
	if (!file.empty() && !pixels.empty())
	{
		auto surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), box.w, box.h, 32, box.w*4, SDL_PIXELFORMAT_ARGB8888);
		if (!surface || SDL_SaveBMP(surface, file.c_str()) != 0) pixels.clear();
		SDL_FreeSurface(surface);
	}
	SDL_RenderPresent(renderer);
	return pixels;
}

inline bool run(FILE* report, const std::string& prefix)
{
	auto ball = pb::MainTable->BallList.front();
	const bool wasActive = ball->ActiveFlag, wasHD = render::EnhancedGraphics;
	const int oldCollision = ball->CollisionFlag;
	const auto original = ball->Position;
	int width, height;
	SDL_GetRendererOutputSize(winmain::Renderer, &width, &height);
	const SDL_Rect bounds{0,0,width,height};
	ball->ActiveFlag = 1;
	ball->CollisionFlag = 0;
	auto place = [&](const vector3& point) {
		ball->Position = point;
		ball->Repaint();
		render::update();
		render::PresentVScreen(pb::time_now);
	};
	auto ground = [&](int x, int y) {
		auto p = proj::ReverseXForm({x,y});
		p.Z = ball->Radius;
		return p;
	};
	bool ok = true;
	for (bool hd : {false,true})
	{
		render::EnhancedGraphics = hd;
		const auto plunger = pb::MainTable->PlungerPosition;
		const vector3 points[] = {{plunger.X, plunger.Y, ball->Radius}, ground(186,263)};
		for (int i = 0; i < 2; ++i)
		{
			place(points[i]);
			SDL_FRect image{}, clip{};
			if (!render::GetVisibleBallImage(*ball, image, clip)) { ok = false; continue; }
			const float rx=image.w*.5f, ry=image.h*.5f, cx=image.x+rx, cy=image.y+ry;
			SDL_Rect crop{int(cx-5*rx),int(cy-5*ry),int(10*rx+1),int(10*ry+1)};
			if (!SDL_IntersectRect(&bounds,&crop,&crop)) { ok=false; continue; }
			const auto stem=prefix+"cue-"+(hd?"hd-":"native-")+(i?"field":"launch");
			const auto plain=patch(crop,0.f,stem+"-base.bmp"), ring=patch(crop,.36f,stem+".bmp");
			bool localized = !plain.empty() && ring.size() == plain.size();
			int changed = 0;
			if (localized)
				for (int y=0; y<crop.h; ++y)
					for (int x=0; x<crop.w; ++x)
					{
						const int n=y*crop.w+x;
						if (ring[n]==plain[n]) continue;
						++changed;
						const float dx=(x+crop.x+.5f-cx)/rx, dy=(y+crop.y+.5f-cy)/ry;
						const float d2=dx*dx+dy*dy;
						localized &= d2>1.1f && d2<16.f && x+crop.x < ceilf(clip.x+clip.w);
					}
			const bool pass=localized && changed>=12 && patch(crop,1.f)==plain;
			fprintf(report,"%s  %s ball cue %s: center %.2f/%.2f, %d changed pixels outside sphere only\n",
				pass?"PASS":"FAIL",hd?"HD":"DAT",i?"open field":"shooter lane",cx,cy,changed);
			ok &= pass;
		}
		SDL_FRect image{}, clip{}, shifted{};
		const bool before=render::GetVisibleBallImage(*ball,image,clip);
		render::shift(2,-1);
		const bool after=render::GetVisibleBallImage(*ball,shifted,clip);
		render::shift(-2,1);
		ok &= before && after && fabsf(shifted.x-image.x-2*fullscrn::ScaleX)<.01f &&
			fabsf(shifted.y-image.y+fullscrn::ScaleY)<.01f;

		ball->ActiveFlag=0;
		const bool inactive = !render::GetVisibleBallImage(*ball,image,clip);
		ball->ActiveFlag=1;
		fprintf(report,"%s  %s cue suppressed when an exposed ball becomes inactive\n",
			inactive?"PASS":"FAIL",hd?"HD":"DAT");
		ok &= inactive;
		place(ground(77,189));
		const bool hidden = !render::GetVisibleBallImage(*ball,image,clip);
		fprintf(report,"%s  %s cue suppressed for an occluded ball below the raised left ramp\n",
			hidden?"PASS":"FAIL",hd?"HD":"DAT");
		ok &= hidden;
	}
	render::EnhancedGraphics = wasHD;
	ball->Position = original;
	ball->ActiveFlag = wasActive;
	ball->CollisionFlag = oldCollision;
	if (wasActive) ball->Repaint(); else ball->SpriteSet(-1);
	render::update();
	return ok;
}
}
