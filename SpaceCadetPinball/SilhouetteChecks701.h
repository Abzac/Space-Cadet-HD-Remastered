#pragma once

#include "options.h"
#include "TFlipper.h"

namespace silhouetteChecks701
{
inline bool run(FILE* out, const std::string& prefix)
{
	auto table = pb::MainTable;
	table->Message(MessageCode::Reset,0);
	auto ball = table->AddBall({0,0});
	if (!ball || !table->FlipperL || !table->FlipperL->RenderSprite) return false;
	const auto ground = proj::ReverseXForm({175,329});
	ball->Position = {ground.X,ground.Y,ball->Radius};
	ball->Repaint();
	render::update();
	float radius;
	const auto center = proj::BallImage(ball->Position,ball->Radius,radius);
	const auto camera = proj::CameraPoint(ball->Position);
	const auto bat = table->FlipperL->RenderSprite->BmpRect;
	const auto previousDest = render::DestinationRect;
	auto renderer = winmain::Renderer;
	auto previousTarget = SDL_GetRenderTarget(renderer);
	const bool previous = options::Options.SmoothSilhouettes;
	bool ok = true;
	for (int width : {600,900,1200,1800})
	{
		const float scale = float(width)/render::vscreen->Width;
		const int height = int(std::lround(render::vscreen->Height*scale));
		const float sy = float(height)/render::vscreen->Height;
		auto target = SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,width,height);
		if (!target || SDL_SetRenderTarget(renderer,target)!=0) { SDL_DestroyTexture(target); ok=false; break; }
		render::DestinationRect = {0,0,width,height};
		std::vector<Uint32> samples[2];
		for (int mode=0; mode<2; ++mode)
		{
			options::Options.SmoothSilhouettes = mode!=0;
			samples[mode].resize(width*height);
			SDL_RenderClear(renderer);
			render::PresentVScreen(0.f);
			ok &= SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,samples[mode].data(),width*4)==0;
			auto pic=SDL_CreateRGBSurfaceWithFormatFrom(samples[mode].data(),width,height,32,width*4,SDL_PIXELFORMAT_ARGB8888);
			if (pic)
			{
				SDL_SaveBMP(pic,(prefix+"edges-"+std::to_string(width)+(mode?"-on.bmp":"-off.bmp")).c_str());
				SDL_FreeSurface(pic);
			}
		}
		int ballRim=0, ballCore=0, batChanged=0, batPixels=0;
		float deepest=0;
		for (int y=0;y<height;++y)
			for (int x=0;x<width;++x)
			{
				const float px=(x+.5f)/scale, py=(y+.5f)/sy;
				const bool changed=(samples[0][y*width+x]&0xffffff)!=(samples[1][y*width+x]&0xffffff);
				if (px>=bat.XPosition && px<bat.XPosition+bat.Width && py>=bat.YPosition && py<bat.YPosition+bat.Height)
				{
					++batPixels;
					if (changed) ++batChanged;
				}
				if (!changed || fabsf(px-center.X)>radius+2.f || fabsf(py-center.Y)>radius+2.f) continue;
				const auto ray=proj::ViewRay(px,py);
				vector3 lateral;
				maths::cross(camera,ray,lateral);
				const float inset=(1.f-maths::magnitude(lateral)/ball->Radius)*radius*scale;
				if (inset>=-1.5f)
				{
					++ballRim;
					deepest=std::max(deepest,inset);
					if (inset>2.f) ++ballCore;
				}
			}
		fprintf(out,"Edges at %d px: %d changed sphere rim pixels, deepest %.2f output pixels, %d beyond 2 px; flipper %d/%d pixels changed.\n",
		        width,ballRim,deepest,ballCore,batChanged,batPixels);
		ok &= ball->RenderSprite->HdBallVisible && ballRim>2 && !ballCore && batChanged>0 && batChanged<batPixels/2;
		options::Options.SmoothSilhouettes=false;
		SDL_RenderClear(renderer);
		render::PresentVScreen(0.f);
		ok &= SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,samples[1].data(),width*4)==0 &&
		      samples[0]==samples[1];
		SDL_SetRenderTarget(renderer,previousTarget);
		SDL_DestroyTexture(target);
	}
	options::Options.SmoothSilhouettes = previous;
	render::DestinationRect=previousDest;
	table->Message(MessageCode::Reset,0);
	return ok;
}
}
