#pragma once

namespace cabinetMenuArt
{
enum class Icon { None, Play, Settings, Controls, Scores, Help, Power, Back };
constexpr ImU32 amber=IM_COL32(255,189,99,255), ice=IM_COL32(173,212,231,255);
static SDL_Texture* portrait{}, *planet{};
static bool portraitTried{};

inline void release()
{
	SDL_DestroyTexture(portrait);
	SDL_DestroyTexture(planet);
	portrait=planet=nullptr;
	portraitTried=false;
}

inline SDL_Texture* sampleArtwork(SDL_Surface* sheet, SDL_Rect box, int size, bool relief)
{
	auto sample=SDL_CreateRGBSurfaceWithFormat(0,size,size,32,SDL_PIXELFORMAT_ARGB8888);
	if (!sample) return nullptr;
	for(int y=0;y<size;++y)
		for(int x=0;x<size;++x)
		{
			const int left=box.x+x*box.w/size, top=box.y+y*box.h/size;
			const int right=box.x+(x+1)*box.w/size, bottom=box.y+(y+1)*box.h/size;
			int r=0,g=0,b=0;
			for(int j=top;j<bottom;++j)
				for(int i=left;i<right;++i)
				{
					const Uint32 c=reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(sheet->pixels)+j*sheet->pitch)[i];
					r+=(c>>16)&255;g+=(c>>8)&255;b+=c&255;
				}
			const int count=(right-left)*(bottom-top);
			r/=count;g/=count;b/=count;
			if (relief)
			{
				// Reprint the painted globe as a low-contrast blue relief on the console.
				const float u=(x+.5f)/size*2-1,v=(y+.5f)/size*2-1;
				const float nz=sqrtf(std::max(0.f,1-u*u-v*v));
				const float light=std::max(0.f,-.65f*u-.42f*v+.36f*nz);
				const float value=(.27f*r+.62f*g+.11f*b)/255.f;
				r=int(21+light*(15+value*23));g=int(29+light*(28+value*37));b=int(43+light*(37+value*42));
			}
			reinterpret_cast<Uint32*>(static_cast<Uint8*>(sample->pixels)+y*sample->pitch)[x]=
				0xff000000u|(unsigned(r)<<16)|(unsigned(g)<<8)|unsigned(b);
		}
	auto tex=SDL_CreateTextureFromSurface(winmain::Renderer,sample);
	SDL_FreeSurface(sample);
	if(tex) SDL_SetTextureScaleMode(tex,SDL_ScaleModeLinear);
	return tex;
}
inline SDL_Texture* pilot()
{
	if (portraitTried) return portrait;
	portraitTried=true;
	auto base=SDL_GetBasePath();
	const auto path=std::string(base ? base : "")+"HD/2.bmp";
	SDL_free(base);
	auto image=SDL_LoadBMP(path.c_str());
	if (!image) return nullptr;
	// A detail of the same pilot painted on the cabinet's own backglass.
	if (image->w==812 && image->h==1576)
	{
		auto sheet=SDL_ConvertSurfaceFormat(image,SDL_PIXELFORMAT_ARGB8888,0);
		if (sheet)
		{
			portrait=sampleArtwork(sheet,{462,222,187,187},67,false);
			planet=sampleArtwork(sheet,{38,487,133,143},125,true);
			SDL_FreeSurface(sheet);
		}
	}
	SDL_FreeSurface(image);
	return portrait;
}

inline void starChart()
{
	const auto start=ImGui::GetCursorScreenPos(), pos=ImGui::GetWindowPos(), size=ImGui::GetWindowSize();
	const float end=pos.y+size.y-21, w=ImGui::GetContentRegionAvail().x, available=end-start.y-12;
	if(available<58.f) return;
	auto dl=ImGui::GetWindowDrawList();
	const float h=std::min(available,Clamp(w*.54f,100.f,228.f));
	const ImVec2 p{start.x,end-h}, q{start.x+w,end};
	dl->PushClipRect(p,q,true);
	const ImVec2 center{p.x+w*.77f,q.y+h*.12f};
	const float r=std::min(w*.36f,h*.76f);
	for(int i=0;i<29;++i)
	{
		const float x=p.x+w*(((i*113+17)%263)/263.f),y=p.y+h*(((i*73+5)%151)/151.f);
		dl->AddCircleFilled({x,y},i%7==0?1.25f:.7f,IM_COL32(140,175,196,70+i%4*15),6);
	}
	if(planet)
		dl->AddImageRounded(planet,{center.x-r,center.y-r},{center.x+r,center.y+r},
			{0,0},{1,1},IM_COL32_WHITE,r,ImDrawFlags_RoundCornersAll);
	dl->PathArcTo(center,r+.5f,-3.1f,-.42f,55);
	dl->PathStroke(IM_COL32(112,171,188,179),0,1.4f);
	dl->PathArcTo(center,r+3,-2.87f,-1.49f,32);dl->PathStroke(IM_COL32(113,181,208,57),0,1);
	// A single inclined orbital path crosses the globe and fades into the chart.
	const ImVec2 a{p.x+w*.065f,q.y-h*.13f}, b{p.x+w*.53f,p.y+h*.05f}, c{q.x+w*.06f,p.y+h*.38f};
	dl->AddBezierQuadratic(a,b,c,IM_COL32(134,163,181,103),1,36);
	const float t=.24f;
	const ImVec2 beacon{(1-t)*(1-t)*a.x+2*t*(1-t)*b.x+t*t*c.x,(1-t)*(1-t)*a.y+2*t*(1-t)*b.y+t*t*c.y};
	dl->AddCircle(beacon,4.f,IM_COL32(221,171,109,200),16,1);
	dl->AddCircleFilled(beacon,1.4f,amber,8);
	dl->PopClipRect();
}

inline void rivet(ImDrawList* dl, float x, float y)
{
	dl->AddCircleFilled({x,y+1},3.6f,IM_COL32(6,8,14,255),12);
	dl->AddCircleFilled({x,y},2.8f,IM_COL32(94,99,112,255),12);
	dl->AddLine({x-1.5f,y+1},{x+1.5f,y-1},IM_COL32(23,31,43,255),1.1f);
}

inline void bezel(float x, float w, float h)
{
	auto dl=ImGui::GetBackgroundDrawList();
	const float end=x+w;
	dl->AddRectFilledMultiColor({0,0},{x,h},IM_COL32(3,7,13,55),IM_COL32(2,5,10,122),
		IM_COL32(2,5,10,122),IM_COL32(2,5,10,58));
	dl->AddRectFilledMultiColor({x,0},{end,h},IM_COL32(46,43,64,255),IM_COL32(23,34,48,255),
		IM_COL32(10,20,30,255),IM_COL32(15,20,33,255));
	// The steel extrusion and the gasket continue all the way to the cabinet edges.
	dl->AddRectFilledMultiColor({x,0},{x+7,h},IM_COL32(19,25,33,255),IM_COL32(171,177,183,255),
		IM_COL32(80,93,103,255),IM_COL32(17,24,30,255));
	dl->AddLine({x+8,0},{x+8,h},IM_COL32(4,8,15,255),3);
	dl->AddLine({x+11,0},{x+11,h},IM_COL32(91,94,116,245),1);
	dl->AddLine({x+13,2},{end,2},IM_COL32(125,125,143,150),1);
	dl->AddLine({x+13,h-3},{end,h-3},IM_COL32(87,103,120,150),1);
	for (float y : {10.f,h-10.f}) { rivet(dl,x+18,y); rivet(dl,end-10,y); }
	// Short recessed cooling slots below the instruments.
	for (int i=0;i<7;++i)
	{
		const float a=end-34-i*6.f;
		dl->AddLine({a-2,h-9},{a+1,h-14},IM_COL32(80,93,111,120),2.f);
		dl->AddLine({a-3,h-10},{a,h-15},IM_COL32(4,10,18,255),2.f);
	}
}

inline void symbol(ImDrawList* dl, Icon kind, ImVec2 c, float scale, ImU32 tint)
{
	const auto p=[&](float x,float y) {return ImVec2(c.x+x*scale,c.y+y*scale);};
	const auto line=[&](float x,float y,float u,float v) {dl->AddLine(p(x,y),p(u,v),tint,1.5f*scale);};
	switch(kind)
	{
	case Icon::Play:
		dl->AddTriangleFilled(p(-4,-6),p(5,0),p(-4,6),tint);break;
	case Icon::Settings:
		for (int i=0;i<3;++i) { float y=-5.f+i*5, a=(i==1 ? 2.f : -2.f); line(-6,y,6,y); dl->AddCircleFilled(p(a,y),2*scale,tint,8); }break;
	case Icon::Controls:
		dl->AddRect(p(-7,-4),p(7,5),tint,3*scale,0,1.4f*scale);
		line(-5,0,-1,0);line(-3,-2,-3,2);dl->AddCircleFilled(p(4,-1),1.2f*scale,tint,8);break;
	case Icon::Scores:
		line(-6,5,-6,0);line(0,5,0,-6);line(6,5,6,-2);line(-8,7,8,7);break;
	case Icon::Help:
		dl->AddCircle(c,6.8f*scale,tint,18,1.4f*scale);
		line(0,0,0,4);dl->AddCircleFilled(p(0,-3),1*scale,tint,6);break;
	case Icon::Power:
		dl->PathArcTo(c,6.f*scale,-.95f,4.1f,14);dl->PathStroke(tint,0,1.6f*scale);line(0,-7,0,0);break;
	case Icon::Back:
		line(-6,0,6,0);line(-6,0,-1,-5);line(-6,0,-1,5);break;
	default: break;
	}
}

inline void masthead(const char* name, ImFont* font, bool home)
{
	const auto p=ImGui::GetCursorScreenPos();
	const float w=ImGui::GetContentRegionAvail().x;
	const float h=Clamp(ImGui::GetIO().DisplaySize.y*(home?.158f:.116f),home?66.f:53.f,home?112.f:82.f);
	auto dl=ImGui::GetWindowDrawList();
	const ImVec2 end{p.x+w,p.y+h};
	dl->AddRectFilledMultiColor(p,end,IM_COL32(17,24,38,255),IM_COL32(34,43,65,255),
		IM_COL32(22,35,51,255),IM_COL32(11,20,31,255));
	dl->PushClipRect(p,end,true);
	for (int i=0;i<36;++i)
	{
		const float fx=((i*137+19)%311)/311.f, fy=((i*127+13)%283)/283.f;
		dl->AddCircleFilled({p.x+fx*w,p.y+fy*h},i%8==0?1.07f:.68f,IM_COL32(156,185,214,50+i%4*15),6);
	}
	const float r=std::min(h*.375f,w*.105f);
	const ImVec2 center{end.x-r-14,p.y+h*.5f};
	// An engraved orbital track behind the round pilot badge.
	dl->AddCircle({center.x+1,center.y},r+11,IM_COL32(111,153,180,63),56,1);
	dl->AddLine({center.x-r-53,end.y-9},{end.x+4,p.y+8},IM_COL32(126,164,187,56),1);
	dl->AddCircleFilled(center,r+3,IM_COL32(8,14,24,255),44);
	auto artwork=pilot();
	if (artwork)
		dl->AddImageRounded(artwork,{center.x-r,center.y-r},{center.x+r,center.y+r},
			{0,0},{1,1},IM_COL32_WHITE,r,ImDrawFlags_RoundCornersAll);
	else
	{
		dl->AddCircleFilled(center,r,IM_COL32(39,60,81,255),36);
		dl->AddTriangle({center.x-r*.5f,center.y+r*.45f},{center.x,center.y-r*.55f},
			{center.x+r*.5f,center.y+r*.45f},ice,2);
	}
	dl->AddCircle(center,r+1,IM_COL32(175,165,151,255),48,1.5f);
	dl->PathArcTo(center,r+3.6f,-2.80f,-1.13f,20);dl->PathStroke(amber,0,1.6f);
	auto body=ImGui::GetFont();
	dl->AddText(body,Clamp(h*.19f,12.f,17.f),{p.x+13,p.y+h*.12f},amber,home?"3D PINBALL":"SPACE CADET");
	if (!font) font=body;
	const float letterW=font->CalcTextSizeA(1.f,FLT_MAX,0.f,name).x;
	const float size=std::min(Clamp(h*.46f,22.f,46.f),std::max(1.f,(center.x-r-22-p.x)/letterW));
	dl->AddText(font,size,{p.x+12,p.y+h*.38f+1},IM_COL32(4,7,13,255),name);
	dl->AddText(font,size,{p.x+12,p.y+h*.38f},IM_COL32(230,235,235,255),name);
	dl->PopClipRect();
	dl->AddLine({p.x,end.y-1},{end.x,end.y-1},IM_COL32(93,115,135,255),1);
	dl->AddLine({p.x,end.y-1},{p.x+std::min(68.f,w*.19f),end.y-1},amber,2);
	ImGui::Dummy({w,h});
}
}
