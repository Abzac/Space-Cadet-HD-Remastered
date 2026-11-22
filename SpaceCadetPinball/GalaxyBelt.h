// Outer stars are printed over the fixed sky, sometimes behind a guide.
// Recover the warm, locally brighter ink once. Only that ink is displaced.
struct GalaxyBelt
{
	struct Plane
	{
		float cx, cy, rx, ry, ax, ay;
		SDL_FPoint uv(float x, float y) const
		{
			x -= cx; y -= cy;
			return {(ax*x + ay*y)/rx, (-ay*x + ax*y)/ry};
		}
		SDL_FPoint xy(float u, float v) const
		{
			return {cx + ax*rx*u - ay*ry*v, cy + ay*rx*u + ax*ry*v};
		}
	};
	struct Site
	{
		int index, x, y, alpha;
		float u, v;
		unsigned base;
	};
	Picture picture;
	SDL_Surface* ink{};
	std::vector<Site> sites;
	std::vector<unsigned> frame;
	std::vector<Uint8> visibleSky;
	float speed{};
	int tick{-1};
	Plane plane{};

	template<size_t N>
	static float window(float x, float y, const SDL_FPoint (&edge)[N])
	{
		bool inside = false;
		float nearest = 9.f;
		for (size_t i=0, j=N-1; i<N; j=i++)
		{
			const auto a=edge[j], b=edge[i];
			const float dx=b.x-a.x, dy=b.y-a.y;
			if ((a.y>y)!=(b.y>y) && x<a.x+(y-a.y)*dx/dy) inside=!inside;
			const float t=sat(((x-a.x)*dx+(y-a.y)*dy)/(dx*dx+dy*dy));
			const float px=x-a.x-t*dx, py=y-a.y-t*dy;
			nearest=std::min(nearest,px*px+py*py);
		}
		return inside ? smooth((sqrtf(nearest)-.30f)/.80f) : 0.f;
	}
	static float aperture(int disk, float x, float y, float radius)
	{
		// Measured in native playfield coordinates. These guides, droptargets,
		// and screw heads are part of the background bitmap, unlike live sprites.
		static constexpr SDL_FPoint right[]={{270.9f,134},{286,134},{287,142},{289.1f,149.2f},
			{306.7f,141.4f},{306.7f,135},{308.8f,135},{315.1f,181.5f},{312.5f,182.8f},
			{311,180.1f},{291.6f,190},{293.6f,197},{292.7f,203},{261,203},{257.8f,177},
			{259.2f,172},{263.2f,163},{267,154},{269,145}};
		static constexpr SDL_FPoint upper[]={{226,12.35f},{290.7f,12.35f},{294.7f,48},
			{273,46.8f},{266.2f,44.3f},{261.5f,42},{255.8f,40.2f},{254.9f,38.5f},
			{254,38.4f},{252.4f,38.9f},{248,36.6f},{248,32.7f},{250.3f,30.3f},
			{240.5f,28.8f},{243.1f,33.8f},{229,28},{227.4f,26.8f},{226,26.2f}};
		if (!disk)
		{
			const float screw=hypotf((x-262.87f)/3.18f,(y-168.28f)/2.92f);
			return window(x,y,right)*smooth((screw-1.f)/.18f);
		}
		// Separate the overlapping prints along the valley between their cores.
		static const Plane upperDisks[]={{262.25f,22.10f,17.4f,10.05f,cosf(-.112f),sinf(-.112f)},
			{274.38f,33.35f,8.1f,5.7f,1.f,0.f}};
		const auto other=upperDisks[2-disk].uv(x,y);
		return window(x,y,upper)*smooth((hypotf(other.x,other.y)-radius)/.16f);
	}
	static unsigned starInk(int x, int y, float& fixedWhite)
	{
		// The local median estimates the sky underneath a star even beside
		// another star. Neutral distant lights keep their printed position.
		constexpr int offsets[][2]={{-5,0},{5,0},{0,-5},{0,5},{-4,-4},{4,4},{4,-4},{-4,4}};
		const auto color=texel(boardArt,x,y);
		float signal[3]{};
		for (int channel=0; channel<3; ++channel)
		{
			const int shift=16-channel*8;
			int neighbors[8];
			for (int k=0;k<8;++k)
				neighbors[k]=(texel(boardArt,x+offsets[k][0],y+offsets[k][1])>>shift)&255;
			std::sort(neighbors,neighbors+8);
			signal[channel]=std::max(0.f,float((color>>shift)&255) - .5f*(neighbors[3]+neighbors[4]));
		}
		const float warmth=float((color>>16)&255)-float(color&255);
		fixedWhite=smooth((16.f-warmth)/9.f)*smooth((signal[1]-5.f)/9.f);
		const float warm=smooth((warmth-4.f)/17.f);
		return rgb(signal[0]*warm,signal[1]*warm,signal[2]*warm);
	}
	void init(int disk, Plane geometry, float spin)
	{
		plane=geometry; speed=spin;
		const float limit=disk==0 ? 1.43f : disk==1 ? 1.57f : 1.68f;
		const float ew=limit*hypotf(plane.rx*plane.ax,plane.ry*plane.ay);
		const float eh=limit*hypotf(plane.rx*plane.ay,plane.ry*plane.ax);
		const int x0=int(floorf(plane.cx-ew)), y0=int(floorf(plane.cy-eh));
		picture.init(x0,y0,int(ceilf(plane.cx+ew))-x0,int(ceilf(plane.cy+eh))-y0,SDL_BLENDMODE_BLEND);
		const int width=picture.rect.w*S, height=picture.rect.h*S;
		ink=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);
		if (!ink) return;
		std::vector<unsigned> source(width*height);
		std::vector<float> visible(width*height);
		for (int j=0;j<height;++j)
			for (int i=0;i<width;++i)
			{
				const int px=x0*S+i, py=y0*S+j, pos=j*width+i;
				const float x=(px+.5f)/S, y=(py+.5f)/S;
				const auto uv=plane.uv(x,y);
				const float r=hypotf(uv.x,uv.y);
				if (r<.68f || r>=limit) continue;
				visible[pos]=aperture(disk,x,y,r);
				if (!visible[pos]) continue;
				float white=0;
				source[pos]=starInk(px,py,white);
				const auto original=texel(boardArt,px,py);
				const int alpha=chan(255*visible[pos]*(1-white)*smooth((limit-r)/.16f)*smooth((r-.68f)/.12f));
				if (alpha)
					sites.push_back({pos,px,py,alpha,uv.x,uv.y,(original & 0xffffff)-(source[pos] & 0xffffff)});
			}
		// A turn reveals formerly hidden sectors. Continue the same annulus
		// with ink from the opposite side, or a quarter turn if both are covered.
		for (int j=0;j<height;++j)
			for (int i=0;i<width;++i)
			{
				const int pos=j*width+i;
				auto& pixel=reinterpret_cast<unsigned*>(static_cast<Uint8*>(ink->pixels)+j*ink->pitch)[i];
				pixel=source[pos];
				if (visible[pos]>=1.f) continue;
				const auto p=plane.uv(x0+(i+.5f)/S,y0+(j+.5f)/S);
				const float r=hypotf(p.x,p.y);
				if (r<.68f || r>=limit) continue;
				const SDL_FPoint alternatives[]={plane.xy(-p.x,-p.y),plane.xy(p.y,-p.x),plane.xy(-p.y,p.x)};
				for (auto world : alternatives)
				{
					const int ix=int((world.x-x0)*S), iy=int((world.y-y0)*S);
					if (ix<0 || ix>=width || iy<0 || iy>=height || visible[iy*width+ix]<.99f) continue;
					const auto fill=source[iy*width+ix];
					const float a=visible[pos];
					auto blend=[&](int shift) {return ((pixel>>shift)&255)*a+((fill>>shift)&255)*(1-a);};
					pixel=rgb(blend(16),blend(8),blend(0));
					break;
				}
			}
		frame.resize(sites.size());
		visibleSky.resize(visible.size());
		std::transform(visible.begin(),visible.end(),visibleSky.begin(),[](float opacity) {
			return Uint8(chan(opacity*255));
		});
	}
	bool covered(float x,float y) const
	{
		if (visibleSky.empty()) return false;
		const int ix=int((x-picture.rect.x)*S), iy=int((y-picture.rect.y)*S);
		const int width=picture.rect.w*S, height=picture.rect.h*S;
		return ix<0 || ix>=width || iy<0 || iy>=height || visibleSky[iy*width+ix]<190;
	}
	void draw(float time,const SDL_FRect& dest,int w,int h,const uint16_t* owners,int stride)
	{
		if (!ink || !picture.texture) return;
		const int next=int(time*30.f);
		if (next!=tick)
		{
			tick=next;
			const float angle=next*(speed/30.f), c=cosf(angle), s=sinf(angle);
			for (size_t i=0;i<sites.size();++i)
			{
				const auto& p=sites[i];
				const auto at=plane.xy(p.u*c-p.v*s,p.u*s+p.v*c);
				const auto star=bilinear(ink,(at.x-picture.rect.x)*S-.5f,(at.y-picture.rect.y)*S-.5f);
				auto channel=[&](int shift){return float(((p.base>>shift)&255)+((star>>shift)&255));};
				frame[i]=rgb(channel(16),channel(8),channel(0),p.alpha);
			}
		}
		for (size_t i=0;i<sites.size();++i)
		{
			const auto& p=sites[i];
			picture.pixels[p.index]=owners[p.y*stride+p.x]==Board ? frame[i] : 0;
		}
		picture.draw(dest,w,h);
	}
	void reset()
	{
		picture.reset();
		SDL_FreeSurface(ink); ink=nullptr;
		sites.clear(); frame.clear(); visibleSky.clear(); tick=-1;
	}
};
