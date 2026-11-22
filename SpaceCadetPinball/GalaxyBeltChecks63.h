#pragma once
#include "EffectsChecks4.h"

namespace galaxyBeltChecks63
{
inline float red(const SDL_Surface* image,float x,float y)
{
	const int ix=int(floorf(x)), iy=int(floorf(y));
	const float fx=x-ix, fy=y-iy;
	float value=0;
	for (int j=0;j<2;++j)
		for (int i=0;i<2;++i)
		{
			const auto p=static_cast<const Uint8*>(image->pixels)+(iy+j)*image->pitch+(ix+i)*image->format->BytesPerPixel;
			value+=p[2]*(i?fx:1-fx)*(j?fy:1-fy);
		}
	return value;
}
inline float star(const SDL_Surface* image,float x,float y)
{
	return red(image,x,y)-.25f*(red(image,x-4,y)+red(image,x+4,y)+red(image,x,y-4)+red(image,x,y+4));
}
struct Match
{
	int n{};
	double a{},b{},aa{},bb{},ab{};
	void add(float x,float y) {++n; a+=x; b+=y; aa+=x*x; bb+=y*y; ab+=x*y;}
	double coefficient() const
	{
		if (n<2) return 0;
		const double variance=(aa-a*a/n)*(bb-b*b/n);
		return variance>0 ? (ab-a*b/n)/sqrt(variance) : 0;
	}
};
inline bool run(FILE* report,const std::string& prefix)
{
	struct Disk {SDL_Rect crop; float x,y,rx,ry,tilt,speed;};
	constexpr float elapsed=3.f;
	const Disk disks[]={{{250,140,67,66},285.7f,173.6f,21.6f,19.5f,0,.0425f},
		{{232,10,57,35},262.25f,22.10f,17.4f,10.05f,-.112f,-.0495f},
		{{252,19,39,29},274.38f,33.35f,8.1f,5.7f,0,.0845f}};
	bool ok=true;
	for (int k=0;k<3;++k)
	{
		const auto& disk=disks[k];
		const auto file=prefix+"galaxy-belt63-"+std::to_string(k)+"-";
		uint32_t unused{};
		ok &= visualChecks::capture(file+"start.bmp",disk.crop,unused,nullptr,0,0);
		ok &= visualChecks::capture(file+"orbit.bmp",disk.crop,unused,nullptr,0,elapsed);
		auto a=SDL_LoadBMP((file+"start.bmp").c_str()),b=SDL_LoadBMP((file+"orbit.bmp").c_str());
		if (!a || !b) {SDL_FreeSurface(a);SDL_FreeSurface(b);return false;}
		Match fixed,moving;
		const float c=cosf(disk.tilt),s=sinf(disk.tilt);
		for (int ring=0;ring<7;++ring)
			for (int degree=-179;degree<=180;++degree)
			{
				const float radius=1.03f+ring*.029f;
				auto position=[&](float phase) {
					const float x=radius*disk.rx*cosf(phase),y=radius*disk.ry*sinf(phase);
					return SDL_FPoint{disk.x+x*c-y*s,disk.y+x*s+y*c};
				};
				const float angle=float(degree*M_PI/180);
				const auto from=position(angle),to=position(angle-elapsed*disk.speed);
				auto open=[&](SDL_FPoint p) {
					if (!k) return (p.x>=268 && p.x<=301 && p.y>=150 && p.y<=188) ||
						(p.x>=272 && p.x<=288 && p.y>=185 && p.y<=198);
					if (k==1) return p.y>=15 && p.y<=27 && (p.x<=251 || p.x>=276);
					return p.y>=33 && p.y<=42 && p.x>=274;
				};
				if (!open(from) || !open(to)) continue;
				auto signal=[&](const SDL_Surface* im,SDL_FPoint p) {
					return star(im,(p.x-disk.crop.x)*4-.5f,(p.y-disk.crop.y)*4-.5f);
				};
				const float first=signal(a,from);
				fixed.add(first,signal(b,from));moving.add(first,signal(b,to));
			}
		const double follows=moving.coefficient(),stays=fixed.coefficient();
		fprintf(report,"Outer red stars %d, %d samples beyond the old silhouette: orbit %.3f, stationary %.3f.\n",
			k+1,moving.n,follows,stays);
		ok &= moving.n>150 && follows>.55 && follows>stays+.17;
		if (!k)
		{
			float confidence=0;
			const int angle=effectsChecks4::rotation(file+"start.bmp",file+"orbit.bmp",
				(disk.x-disk.crop.x)*4,(disk.y-disk.crop.y)*4,disk.rx*4,disk.ry*4,disk.tilt,confidence);
			fprintf(report,"Right galaxy core: %+d degrees in %.1f s, correlation %.3f.\n",angle,elapsed,confidence);
			ok &= abs(angle+7)<=2 && confidence>.90f;
			const SDL_Rect screw{(261-disk.crop.x)*4,(167-disk.crop.y)*4,12,8};
			const SDL_Rect white{1112-disk.crop.x*4,591-disk.crop.y*4,1,1};
			for (auto fixedRegion : {screw,white})
				ok &= effectsChecks4::difference(file+"start.bmp",file+"orbit.bmp",fixedRegion).maximum==0;
			// The corners of a box around either diagonal target contain open sky.
			// Compare its actual brass pigment, including the highlights at its edges.
			int brass=0,moved=0;
			for (const auto box : {SDL_Rect{288,140,18,10},SDL_Rect{290,179,25,18}})
				for (int j=(box.y-disk.crop.y)*4;j<(box.y-disk.crop.y+box.h)*4;++j)
					for (int i=(box.x-disk.crop.x)*4;i<(box.x-disk.crop.x+box.w)*4;++i)
					{
						const auto p=static_cast<const Uint8*>(a->pixels)+j*a->pitch+i*a->format->BytesPerPixel;
						const auto q=static_cast<const Uint8*>(b->pixels)+j*b->pitch+i*b->format->BytesPerPixel;
						if (p[2]<=40 || p[1]<=40 || p[1]<=p[0]+20 || abs(p[2]-p[1])>=45) continue;
						++brass;
						moved+=p[0]!=q[0] || p[1]!=q[1] || p[2]!=q[2];
					}
			fprintf(report,"Right sky guards: %d/%d brass texels changed, screw and white star checked.\n",moved,brass);
			ok &= brass>2900 && moved==0;
		}
		SDL_FreeSurface(a);SDL_FreeSurface(b);
	}
	return ok;
}
}
