#pragma once

#include "HdrOutput.h"
#include "TLight.h"
#include "VisualChecks.h"

namespace insertChecks4
{
inline bool runHdr(FILE* report)
{
	auto lamp=dynamic_cast<TLight*>(pb::MainTable->find_component("lite17"));
	auto renderer=winmain::Renderer;
	int width, height;
	if (!lamp || SDL_GetRenderTarget(renderer) || SDL_GetRendererOutputSize(renderer,&width,&height) ||
	    width<=0 || height<=0) return false;
	fullscrn::window_size_changed();
	const auto dest=render::DestinationRect;
	auto site=[&](float x,float y) -> HdrOutput::LuminanceSite {
		return {(dest.x+x*dest.w/render::vscreen->Width)/width,(dest.y+y*dest.h/render::vscreen->Height)/height};
	};
	// The raised guide has a narrow specular edge inside this emitter's ellipse.
	// Take the brightest unlit bracket site so a dark, unreflective pixel cannot
	// conceal a missing HDR mask. Use the identical output pixel for both states.
	const HdrOutput::LuminanceSite locations[] = {site(52.875f,285.125f),
		site(57.00f,284.75f),site(57.05f,285.15f),site(57.35f,285.30f)};
	constexpr unsigned siteCount=sizeof(locations)/sizeof(*locations);
	HdrOutput::LuminanceSite readings[2][siteCount]{};
	const bool previous=lamp->LightOnFlag;
	const bool wasActive=HdrOutput::Active();
	bool ok=true;
	for (int lit=0;lit<2 && ok;++lit)
	{
		lamp->Message(lit?MessageCode::TLightTurnOn:MessageCode::TLightTurnOff,0);
		render::update();
		SDL_RenderClear(renderer);
		render::PresentVScreen(pb::time_now);
		std::copy_n(locations,siteCount,readings[lit]);
		ok=HdrOutput::Present(winmain::MainWindow,renderer,true,true,false,readings[lit],siteCount);
		for (const auto& reading:readings[lit]) ok &= reading.nits>=0.f;
	}
	lamp->Message(previous?MessageCode::TLightTurnOn:MessageCode::TLightTurnOff,0);
	render::update();
	if (!wasActive) HdrOutput::Close();
	if (!ok)
	{
		fprintf(report,"FP16 lite17 readback failed at %d x %d.\n",width,height);
		return false;
	}
	unsigned bracket=1;
	for (unsigned i=2;i<siteCount;++i)
		if (readings[0][i].nits>readings[0][bracket].nits) bracket=i;
	const float lensOff=readings[0][0].nits, lensOn=readings[1][0].nits;
	const float frontOff=readings[0][bracket].nits, frontOn=readings[1][bracket].nits;
	fprintf(report,"FP16 lite17 at %dx%d, lens off/on %.2f / %.2f nits; raised guide off/on %.2f / %.2f nits (pixel %d,%d).\n",
	        width,height,lensOff,lensOn,frontOff,frontOn,int(locations[bracket].u*width),int(locations[bracket].v*height));
	const float lensRise=lensOn-lensOff, guideRise=fabsf(frontOn-frontOff);
	return lensOn>160.f && lensRise>50.f && guideRise<20.f && guideRise<.16f*lensRise;
}

// Compare the same scene before and after switching the stock lamp. Probe
// native hardware coordinates at 4x, independently of the replacement mask.
inline float change(const SDL_Surface* a, const SDL_Surface* b, float x, float y)
{
	const int ix=int(x*HdRenderer::Scale), iy=int(y*HdRenderer::Scale);
	float sum=0;
	for (int j=0;j<3;++j)
		for(int i=0;i<3;++i)
		{
			const auto first=static_cast<const Uint8*>(a->pixels)+(iy+j)*a->pitch+(ix+i)*4;
			const auto second=static_cast<const Uint8*>(b->pixels)+(iy+j)*b->pitch+(ix+i)*4;
			for (int c=0;c<3;++c) sum+=abs(int(first[c])-int(second[c]));
		}
	return sum/27;
}

inline bool run(FILE* report,const std::string& prefix)
{
	struct Study { SDL_Rect crop; const char* name; std::vector<const char*> lights; vector2 lens, bracket; };
	const Study studies[] = {
		{{29,247,54,52},"left-outlane",{"lite17"},{23.5f,37.8f},{29.9f,38.4f}},
		{{270,251,49,48},"right-outlane",{"lite18"},{21.8f,34.3f},{16.f,34.3f}},
		{{44,171,57,35},"ramp-guides",{"lite169","lite170","lite171"},{12.f,13.f},{16.5f,15.2f}},
		{{61,119,67,88},"underpass",{"literoll179","literoll180","literoll181","literoll182","literoll183","literoll184"},
		 {46.5f,78.4f},{20.f,24.f}}
	};
	bool passed=true;
	for(const auto& study:studies)
	{
		std::vector<TLight*> lamps;
		for(const auto name:study.lights)
		{
			auto lamp=dynamic_cast<TLight*>(pb::MainTable->find_component(name));
			if(!lamp) return false;
			lamps.push_back(lamp);
		}
		const auto stem=prefix+"insert-"+study.name;
		uint32_t hash;
		for(int lit=0;lit<2;++lit)
		{
			for(auto lamp:lamps) lamp->Message(lit?MessageCode::TLightTurnOn:MessageCode::TLightTurnOff,0);
			render::update();
			if(!visualChecks::capture(stem+(lit?"-on.bmp":"-off.bmp"),study.crop,hash)) return false;
		}
		auto before=SDL_LoadBMP((stem+"-off.bmp").c_str()), after=SDL_LoadBMP((stem+"-on.bmp").c_str());
		bool ok=before && after && before->format->BytesPerPixel==4 && after->format->BytesPerPixel==4;
		if(ok)
		{
			const auto lens=change(before,after,study.lens.X,study.lens.Y);
			const auto hardware=change(before,after,study.bracket.X,study.bracket.Y);
			fprintf(report,"%s: on/off mean channel change at visible lens %.2f, foreground %.2f.\n",study.name,lens,hardware);
			ok=lens>15 && hardware<lens*.42f && hardware<23;
		}
		SDL_FreeSurface(before);
		SDL_FreeSurface(after);
		passed &= ok;
	}
	return passed;
}
}
