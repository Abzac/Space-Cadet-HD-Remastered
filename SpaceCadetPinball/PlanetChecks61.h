#pragma once
#include "EffectsChecks4.h"
#include "GalaxyBeltChecks63.h"

namespace planetChecks61
{
inline float light(const SDL_Surface* im,int x,int y)
{
	const auto pixel=static_cast<const Uint8*>(im->pixels)+y*im->pitch+x*im->format->BytesPerPixel;
	return .10f*pixel[0]+.67f*pixel[1]+.23f*pixel[2];
}

inline double match(const SDL_Surface* a,const SDL_Surface* b,float angle)
{
	// Register the tracked clouds on the surface of the visible sphere.
	// An ordinary image-plane rotation or a changing brightness cannot pass.
	constexpr float cx=99.71f-8*4,cy=557.68f-119*4,rx=70.21f,ry=70.70f;
	double sumA=0,sumB=0,sumAA=0,sumBB=0,sumAB=0;
	int n=0;
	for (int j=32;j<136;j+=2)
		for (int i=30;i<110;i+=2)
		{
			const float x=(i+.5f-cx)/rx,y=(j+.5f-cy)/ry;
			const float z2=1.f-x*x-y*y;
			if (z2<.3f) continue;
			const float nx=x*cosf(angle)+sqrtf(z2)*sinf(angle);
			const float px=cx+nx*rx-.5f;
			const int ix=int(px);
			if (ix<0 || ix+1>=b->w) continue;
			const float va=light(a,i,j), vb=light(b,ix,j)+(px-ix)*(light(b,ix+1,j)-light(b,ix,j));
			++n; sumA+=va; sumB+=vb; sumAA+=va*va; sumBB+=vb*vb; sumAB+=va*vb;
		}
	const double variance=(sumAA-sumA*sumA/n)*(sumBB-sumB*sumB/n);
	return variance>0 ? (sumAB-sumA*sumB/n)/sqrt(variance) : 0;
}

inline bool run(FILE* report,const std::string& prefix)
{
	const float saved=pb::time_now;
	const SDL_Rect globe{386+8,12+119,38,42};
	const char* files[]={"planet61-start.bmp","planet61-travel.bmp","planet61-return.bmp","planet61-quiet.bmp"};
	const float times[]={0,15,300,15};
	uint32_t hashes[4]{};
	bool ok=true;
	for (int i=0;i<4;++i)
	{
		pb::time_now=times[i];
		ok &= visualChecks::capture(prefix+files[i],globe,hashes[i]);
	}
	ok &= hashes[0]==hashes[2] && hashes[1]==hashes[3] && hashes[0]!=hashes[1];
	auto a=SDL_LoadBMP((prefix+files[0]).c_str()),b=SDL_LoadBMP((prefix+files[1]).c_str());
	if (a && b)
	{
		int best=0;
		double confidence=-1;
		for (int degree=0;degree<=30;++degree)
		{
			const auto correlation=match(a,b,float(degree*M_PI/180));
			if (correlation>confidence) { best=degree; confidence=correlation; }
		}
		int changedRim=0, exposedClouds=0, greenLimb=0;
		for (int j=0;j<a->h;++j)
			for (int i=0;i<a->w;++i)
			{
				const float x=(8*4+i+.5f-99.71f)/70.21f,y=(119*4+j+.5f-557.68f)/70.70f;
				const int px=8*4+i;
				if (x*x+y*y>1.01f || px<37)
					changedRim+=light(a,i,j)!=light(b,i,j);
				if (px>=37 && px<40 && fabsf(y)<.25f)
				{
					const auto pixel=static_cast<const Uint8*>(a->pixels)+j*a->pitch+i*a->format->BytesPerPixel;
					greenLimb+=pixel[1]>pixel[0]+9;
					exposedClouds+=light(a,i,j)!=light(b,i,j);
				}
			}
		fprintf(report,"Poster planet in 15 s: surface longitude %+d deg (correlation %.3f), silhouette/bezel changes %d. Full turn 300 s.\n",best,confidence,changedRim);
		ok &= abs(best-18)<=1 && confidence>.94 && changedRim==0;
		fprintf(report,"Poster planet extends across the three gutter texels: %d green, %d visibly rotating samples.\n",greenLimb,exposedClouds);
		ok &= greenLimb>=96 && exposedClouds>=75;
	}
	else ok=false;
	SDL_FreeSurface(a); SDL_FreeSurface(b);

	// As the clouds turn through all four quadrants, the illuminated upper
	// right face and the lower left terminator must stay on the same sides.
	double dimmestDay=10.,brightestDay=0.;
	int largestLimbStep = 0;
	for (int q=0;q<4;++q)
	{
		const auto file=q?prefix+"planet61-quarter-"+std::to_string(q)+".bmp":prefix+files[0];
		uint32_t hash{};
		pb::time_now=q*75.f;
		if (q) ok &= visualChecks::capture(file,globe,hash);
		auto im=SDL_LoadBMP(file.c_str());
		if (!im) { ok=false; continue; }
		// The painted silhouette continues through the gutter on both sides
		// of the equator. Check the final composite as the surface rotates.
		int lastTop=0, lastBottom=0;
		for (int px=36;px<=44;++px)
		{
			int top=im->h, bottom=-1;
			for (int j=0;j<im->h;++j)
			{
				const auto pixel=static_cast<const Uint8*>(im->pixels)+j*im->pitch+(px-8*4)*im->format->BytesPerPixel;
				if (pixel[1]<=pixel[0]+9 || pixel[1]<=20) continue;
				top=std::min(top,j);
				bottom=j;
			}
			if (px==36) { ok &= bottom==-1; continue; } // Last bevel column.
			if (px==37)
				ok &= std::abs(top+119*4-523)<=2 && std::abs(bottom+119*4-593)<=2;
			else
			{
				largestLimbStep=std::max(largestLimbStep,std::max(std::abs(top-lastTop),std::abs(bottom-lastBottom)));
				ok &= top<=lastTop+1 && bottom>=lastBottom-1;
			}
			lastTop=top; lastBottom=bottom;
		}
		double energy[2]{};
		int count[2]{};
		for (int j=0;j<im->h;++j)
			for (int i=8;i<im->w;++i)
			{
				const float x=(8*4+i+.5f-99.71f)/70.21f,y=(119*4+j+.5f-557.68f)/70.70f;
				const float towardSun=.42f*x-.72f*y;
				if (x*x+y*y>.81f || fabsf(towardSun)<.25f) continue;
				const int side=towardSun>0 ? 1 : 0;
				energy[side]+=light(im,i,j); ++count[side];
			}
		const double ratio=energy[1]*count[0]/(energy[0]*count[1]);
		dimmestDay=std::min(dimmestDay,ratio); brightestDay=std::max(brightestDay,ratio);
		SDL_FreeSurface(im);
	}
	ok &= dimmestDay>1.25 && brightestDay<2.4;
	fprintf(report,"Planet fixed day-side / night-side luminance at four quadrants: %.2f to %.2f.\n",dimmestDay,brightestDay);
	ok &= largestLimbStep<=3;
	fprintf(report,"Largest silhouette step across the left poster gutter at four quadrants: %d HD texels.\n",largestLimbStep);

	const SDL_Rect sky{386+48,12+115,14,14};
	uint32_t on{},off{},dark{};
	pb::time_now=44.78f;
	ok &= visualChecks::capture(prefix+"poster-star61-peak.bmp",sky,on);
	CabinetEffects::SetCelestialAccentDiagnosticBypass(true);
	ok &= visualChecks::capture(prefix+"poster-star61-base.bmp",sky,off);
	CabinetEffects::SetCelestialAccentDiagnosticBypass(false);
	pb::time_now=45.92f;
	ok &= visualChecks::capture(prefix+"poster-star61-after.bmp",sky,dark);
	ok &= off==dark && on!=off;
	const auto glow=effectsChecks4::difference(prefix+"poster-star61-peak.bmp",prefix+"poster-star61-base.bmp",
		{0,0,sky.w*4,sky.h*4});
	fprintf(report,"Poster star: %d changed HD texels, peak %+u, returns to original print after 1.85 s.\n",glow.pixels,glow.maximum);
	ok &= glow.pixels>180 && glow.pixels<625 && glow.maximum>70;
	const SDL_Rect print{386+8,12+8,184,161};
	const float headlightTimes[]={25.8f,26.78f,27.80f};
	for (int i=0;i<3;++i)
	{
		const auto stem=prefix+"headlight61-"+std::to_string(i);
		pb::time_now=headlightTimes[i];
		ok &= visualChecks::capture(stem+"-on.bmp",print,on);
		CabinetEffects::SetCelestialAccentDiagnosticBypass(true);
		ok &= visualChecks::capture(stem+"-base.bmp",print,off);
		CabinetEffects::SetCelestialAccentDiagnosticBypass(false);
		if (i!=1) { ok &= on==off; continue; }
		const auto delta=[&](SDL_Rect r) {
			return effectsChecks4::difference(stem+"-on.bmp",stem+"-base.bmp",r);
		};
		const auto left=delta({266-8*4,413-8*4,12,29});
		const auto right=delta({367-8*4,476-8*4,19,36});
		const auto whole=delta({0,0,print.w*4,print.h*4});
		const int outside=whole.pixels-left.pixels-right.pixels;
		fprintf(report,"Painted headlights: %d and %d HD texels, brightest +%u, changes elsewhere %d. Dark print restored.\n",
			left.pixels,right.pixels,whole.maximum,outside);
		ok &= left.pixels>95 && right.pixels>200 && outside==0 && whole.maximum>=23 && whole.maximum<=42;
	}
	const SDL_Rect nozzle{386+175,12+61,19,29};
	for (const float t : {7.69f,53.78f,55.2f})
	{
		pb::time_now=t;
		const auto stem=prefix+"jet61-"+std::to_string(int(t));
		ok &= visualChecks::capture(stem+"-on.bmp",nozzle,on);
		CabinetEffects::SetCelestialAccentDiagnosticBypass(true);
		ok &= visualChecks::capture(stem+"-base.bmp",nozzle,off);
		CabinetEffects::SetCelestialAccentDiagnosticBypass(false);
		ok &= t<55.f ? on!=off : on==off;
	}
	if (SDL_GetHintBoolean("SPACECADET_CAPTURE_HD61_POSTER",SDL_FALSE))
		for (int i=0;i<=48;++i)
		{
			uint32_t hash{};
			const auto frame=std::to_string(i+100)+".bmp";
			pb::time_now=43.65f+i*.05f;
			ok &= visualChecks::capture(prefix+"poster61-film-"+frame,{386+8,12+87,91,84},hash);
			pb::time_now=i*.25f;
			ok &= visualChecks::capture(prefix+"planet61-film-"+frame,{386+8,12+109,71,63},hash);
			pb::time_now=6.75f+i*.05f;
			ok &= visualChecks::capture(prefix+"jet61-film-"+frame,{386+143,12+46,50,63},hash);
			pb::time_now=25.7f+i*.05f;
			ok &= visualChecks::capture(prefix+"headlights61-film-"+frame,{386+56,12+88,68,49},hash);
		}
	pb::time_now=saved;
	return galaxyBeltChecks63::run(report,prefix) && ok;
}
}
