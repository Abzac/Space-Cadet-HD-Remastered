#include "pch.h"
#include "CabinetEffects.h"
#include "FieldTraces.h"
#include "HdRenderer.h"
#include "GroupData.h"
#include "gdrv.h"
#include "pb.h"
#include "render.h"
#include "winmain.h"

namespace
{
constexpr int S = HdRenderer::Scale;
constexpr uint16_t Board = 108;
constexpr float pi = 3.14159265f;
const SDL_Surface *boardArt{};
bool accentBypass{};
struct Picture
{
	SDL_Texture *texture{};
	SDL_Rect rect{};
	std::vector<uint32_t> pixels;
	void init(int x, int y, int w, int h, SDL_BlendMode mode)
	{
		rect = {x, y, w, h};
		pixels.assign(w * S * h * S, 0);
		texture = SDL_CreateTexture(winmain::Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
									w * S, h * S);
		if (texture)
		{
			SDL_SetTextureBlendMode(texture, mode);
			SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
		}
	}
	void draw(const SDL_FRect &viewport, int width, int height, bool upload = true)
	{
		if (!texture) return;
		if (upload) SDL_UpdateTexture(texture, nullptr, pixels.data(), rect.w * S * 4);
		float sx = viewport.w / width, sy = viewport.h / height;
		const SDL_FRect dst{viewport.x + rect.x * sx, viewport.y + rect.y * sy, rect.w * sx, rect.h * sy};
		SDL_RenderCopyF(winmain::Renderer, texture, nullptr, &dst);
	}
	void reset()
	{
		if (texture) SDL_DestroyTexture(texture);
		texture = nullptr;
		pixels.clear();
	}
};
float sat(float v)
{
	return std::max(0.f, std::min(1.f, v));
}
float smooth(float v)
{
	v = sat(v);
	return v * v * (3.f - 2.f * v);
}
int chan(float v)
{
	return int(std::min(255.f, std::max(0.f, v)) + .5f);
}
unsigned rgb(float r, float g, float b, int alpha = 255)
{
	return (unsigned(alpha) << 24) | (chan(r) << 16) | (chan(g) << 8) | chan(b);
}
unsigned texel(const SDL_Surface *s, int x, int y)
{
	x = std::max(0, std::min(s->w - 1, x));
	y = std::max(0, std::min(s->h - 1, y));
	return reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(s->pixels) + y * s->pitch)[x];
}
unsigned bilinear(const SDL_Surface *s, float x, float y)
{
	const int ix = int(floorf(x)), iy = int(floorf(y));
	const float fx = x - ix, fy = y - iy;
	auto a = texel(s, ix, iy), b = texel(s, ix + 1, iy), c = texel(s, ix, iy + 1), d = texel(s, ix + 1, iy + 1);
	auto channel = [&](int shift) {
		const float top = (1 - fx) * ((a >> shift) & 255) + fx * ((b >> shift) & 255);
		const float bottom = (1 - fx) * ((c >> shift) & 255) + fx * ((d >> shift) & 255);
		return (1 - fy) * top + fy * bottom;
	};
	return rgb(channel(16), channel(8), channel(0));
}

struct StarSite
{
	int index;
	float u, v, weight;
};
// The left pocket has fixed metalwork baked into the field itself. Its
// unlit arrow and rubber guide still belong in front of the printed sky.
float leftSkyCoverage(float x, float y)
{
	const float frame=x-(78.43f-.1568f*y);
	const float guide=79.80f+1.17f*(y-50.f)-x;
	const float arrow=std::max(76.65f-x,std::max(57.90f-.44f*(x-77.f)-y,y-58.90f-.53f*(x-77.f)));
	return smooth(std::min(frame,std::min(guide,arrow))*4.f);
}
#include "GalaxyBelt.h"
struct Galaxy
{
	Picture picture;
	GalaxyBelt belt;
	float cx{}, cy{}, rx{}, ry{}, speed{}, twinkle{}, axisX{1.f}, axisY{};
	bool underRail{}, leftPocket{};
	int tick{-1};
	std::vector<StarSite> sites;
	std::vector<unsigned> colors;
	struct Star
	{
		float u{}, v{}, contrast{-100.f};
	};
	Star stars[3];
	void init(int beltDisk, float x, float y, float w, float h, float rotation, float phase = 0.f,
			  bool clipped = false, float tilt = 0.f, bool leftSlot = false)
	{
		cx = x;
		cy = y;
		rx = w;
		ry = h;
		speed = rotation;
		twinkle = phase;
		underRail = clipped;
		leftPocket = leftSlot;
		axisX = cosf(tilt);
		axisY = sinf(tilt);
		const float ew = hypotf(w * axisX, h * axisY), eh = hypotf(w * axisY, h * axisX);
		const int x0 = int(floorf(x - ew)), y0 = int(floorf(y - eh));
		picture.init(x0, y0, int(ceilf(x + ew)) - x0, int(ceilf(y + eh)) - y0, SDL_BLENDMODE_BLEND);
		for (int j = 0; j < picture.rect.h * S; ++j)
			for (int i = 0; i < picture.rect.w * S; ++i)
			{
				const float dx = x0 + (i + .5f) / S - x, dy = y0 + (j + .5f) / S - y;
				const float u = (axisX * dx + axisY * dy) / w, v = (-axisY * dx + axisX * dy) / h;
				const float r = sqrtf(u * u + v * v);
				if (r >= 1.f || (underRail && y0 + (j + .5f) / S <= 12.25f)) continue;
				const float nucleus = phase == 0.f ? 1.f : smooth(r / .23f);
				float aperture = leftPocket ? leftSkyCoverage(x+dx,y+dy) : 1.f;
				if (beltDisk>=0 && r>.73f)
					aperture *= GalaxyBelt::aperture(beltDisk,x+dx,y+dy,r);
				if (!aperture) continue;
				sites.push_back({j * picture.rect.w * S + i, u, v, smooth((1.f - r) / .22f) * nucleus * aperture});
				if (r > .46f && r < .75f)
				{
					const int px = x0 * S + i, py = y0 * S + j;
					auto value = [&](int sx, int sy) {
						const auto shade = texel(boardArt, sx, sy);
						return .30f * ((shade >> 16) & 255) + .57f * ((shade >> 8) & 255) +
							   .13f * (shade & 255);
					};
					const float contrast = value(px, py) - .25f * (value(px - S, py) + value(px + S, py) +
																   value(px, py - S) + value(px, py + S));
					const int sector = std::min(2, int((atan2f(v, u) + pi) * 3.f / (2.f * pi)));
					if (contrast > stars[sector].contrast) stars[sector] = {u, v, contrast};
				}
			}
		colors.resize(sites.size());
		if (beltDisk>=0)
			belt.init(beltDisk,{cx,cy,rx,ry,axisX,axisY},speed);
		tick = -1;
	}
	void draw(float time, const SDL_FRect &dest, int w, int h, const uint16_t *owners, int stride, float nova,
			  int star)
	{
		const int frame = int(time * 30.f);
		if (frame != tick || cachedNova != nova)
		{
			tick = frame;
			cachedNova = nova;
			const float phase = frame * (speed / 30.f), c = cosf(phase), s = sinf(phase),
						breath = .032f * sinf(phase * 4.2f + twinkle);
			const float starU = stars[star].u * c + stars[star].v * s,
						starV = -stars[star].u * s + stars[star].v * c;
			const float flareScale = std::min(1.f, .16f * ry);
			for (size_t i = 0; i < sites.size(); ++i)
			{
				const auto &p = sites[i];
				float u = p.u * c - p.v * s, v = p.u * s + p.v * c;
				// A small cluster peeks out from beneath the fixed top frame. Its
				// opposite half supplies the missing starlight on a full turn.
				float dx = axisX * rx * u - axisY * ry * v, dy = axisY * rx * u + axisX * ry * v;
				const float r = p.u * p.u + p.v * p.v;
				if ((underRail && cy + dy < 12.25f) || (r>.73f*.73f && belt.covered(cx+dx,cy+dy)) ||
				    (leftPocket && leftSkyCoverage(cx+dx,cy+dy) < .99f))
				{
					dx = -dx;
					dy = -dy;
				}
				const auto color = bilinear(boardArt, (cx + dx) * S - .5f, (cy + dy) * S - .5f);
				const float flicker = 1.f + breath * (1.f - r);
				float glint = 0;
				if (nova > 0.f)
				{
					const float du = (p.u - starU) * rx, dv = (p.v - starV) * ry;
					const float x = (axisX * du - axisY * dv) / flareScale,
								y = (axisY * du + axisX * dv) / flareScale;
					if (fabsf(x) < 3.f && fabsf(y) < 3.f)
					{
						const float rr = x * x + y * y;
						glint = nova * (.76f * expf(-rr * 4.9f) + .19f * expf(-rr * .65f) +
										.28f * expf(-x * x * .9f - y * y * 26.f) +
										.28f * expf(-x * x * 26.f - y * y * .9f));
					}
				}
				colors[i] = rgb(((color >> 16) & 255) * flicker + 77 * glint,
								((color >> 8) & 255) * flicker + 89 * glint,
								(color & 255) * flicker + 114 * glint, chan(255 * p.weight));
			}
		}
		const int width = picture.rect.w * S;
		for (size_t i = 0; i < sites.size(); ++i)
		{
			const int p = sites[i].index, x = picture.rect.x * S + p % width,
					  y = picture.rect.y * S + p / width;
			picture.pixels[p] = (owners[y * stride + x] == Board ? colors[i] : 0);
		}
		picture.draw(dest, w, h);
	}
	void reset()
	{
		picture.reset();
		belt.reset();
		sites.clear();
		colors.clear();
		for (auto &star : stars)
			star = Star{};
		cachedNova = -1;
	}
	float cachedNova{-1};
};
Galaxy galaxies[5];

// A rare accent is scheduled from simulation time. Resuming a paused game
// restores the same frame without touching the table's random-number stream.
float eventAge(float time, float period, float first, float scatter, unsigned &cycle)
{
	cycle = unsigned(floorf(std::max(time, 0.f) / period));
	unsigned bits = cycle * 2654435761u;
	const float offset = float((bits ^ (bits >> 13)) & 1023u) / 1023.f;
	return time - period * cycle - first - scatter * offset;
}

struct PosterPlanet
{
	// Full turn in five minutes. Longitude advances independently of the
	// painted limb, lighting and poster aperture. HD texture coordinates.
	static constexpr int period = 300, mapWidth = 768, mapHeight = 384;
	Picture image;
	std::vector<Uint32> surface;
	struct Site { int pixel, row; float u, fy, shade; int alpha; };
	std::vector<Site> sites;
	int lastTick{-1};

	void init(const SDL_Surface *albedo, const SDL_Surface *print, int originX, int originY)
	{
		if (!albedo || albedo->w != mapWidth || albedo->h != mapHeight) return;
		surface.resize(mapWidth * mapHeight);
		for (int y = 0; y < mapHeight; ++y)
			memcpy(surface.data() + y*mapWidth, static_cast<const Uint8*>(albedo->pixels) + y*albedo->pitch,
				   mapWidth*sizeof(Uint32));
		constexpr int left = 9, top = 120, width = 35, height = 39;
		image.init(originX + left, originY + top, width, height, SDL_BLENDMODE_BLEND);
		for (int j=0; j<height*S; ++j)
			for (int i=0; i<width*S; ++i)
			{
				const int px=left*S+i, py=top*S+j;
				const float x=(px+.5f-99.71f)/70.21f, y=(py+.5f-557.68f)/70.70f, r2=x*x+y*y;
				if (r2>=1.f) continue;
				const auto ink=texel(print,px,py);
				const int green=(ink>>8)&255, blue=ink&255;
				// The poster master starts at 40. Its painted limb extends
				// beneath the moving surface to the fixed bevel at 37.
				if (px < 37 || (px >= 40 && (green <= blue+5 || green<17))) continue;
				const float z=sqrtf(1.f-r2);
				const float u=(.5f+atan2f(x,z)/(2*pi))*mapWidth-.5f;
				const float v=std::min(float(mapHeight)-1.001f,std::max(0.f,(.5f+asinf(y)/pi)*mapHeight-.5f));
				const int row=int(v);
				const float rim=smooth((1.f-sqrtf(r2))/.065f);
				const float shade=.62f+std::max(0.f,.27f*x-.42f*y+.46f*z);
				sites.push_back({j*width*S+i,row*mapWidth,u,v-row,shade,chan(255*rim)});
			}
	}
	void draw(float time, const SDL_FRect &view, int w, int h)
	{
		if (!image.texture) return;
		const int tick=int(fmodf(std::max(0.f,time),float(period))*24.f);
		const bool changed=tick!=lastTick;
		if (changed)
		{
			lastTick=tick;
			const float offset=(1.f-float(tick)/(period*24))*mapWidth;
			for (const auto &p : sites)
			{
				const float u=p.u+offset;
				const int ix=int(u);
				const float fx=u-ix;
				const int left=ix%mapWidth, right=(left+1)%mapWidth;
				const auto row=surface.data()+p.row;
				const auto a=row[left], b=row[right], c=row[mapWidth+left], d=row[mapWidth+right];
				auto channel=[&](int shift) {
					auto top=(1.f-fx)*((a>>shift)&255)+fx*((b>>shift)&255);
					auto bottom=(1.f-fx)*((c>>shift)&255)+fx*((d>>shift)&255);
					return ((1.f-p.fy)*top+p.fy*bottom)*p.shade;
				};
				image.pixels[p.pixel]=rgb(channel(16),channel(8),channel(0),p.alpha);
			}
		}
		image.draw(view,w,h,changed);
	}
	void reset()
	{
		image.reset(); surface.clear(); sites.clear(); lastTick=-1;
	}
};

struct PosterPrint
{
	Picture jet, lettering, star, headlights;
	PosterPlanet planet;
	struct Dot
	{
		int index;
		float x, y, weight;
	};
	std::vector<Dot> flame;
	struct LensDot { int index, side; float weight; };
	std::vector<LensDot> glass;
	std::vector<uint8_t> pigment;
	int originX{}, originY{}, inkWidth{};
	static constexpr int top = 8, depth = 94;

	void init(const SDL_Surface *print, const SDL_Surface *planetMap)
	{
		if (!print || print->w != 203 * S || print->h < 172 * S) return;
		const auto id = pb::record_table->record_labeled("background");
		const auto bmp = id >= 0 ? pb::record_table->GetBitmap(id) : nullptr;
		if (!bmp) return;
		originX = bmp->XPosition;
		originY = bmp->YPosition;
		planet.init(planetMap,print,originX,originY);
		star.init(originX,originY,11,11,SDL_BLENDMODE_ADD);
		constexpr int lensLeft=65, lensTop=102, lensWidth=33;
		headlights.init(originX+lensLeft,originY+lensTop,lensWidth,28,SDL_BLENDMODE_ADD);
		// Both yellow lenses are present on the original front fairings. Read
		// their own pigment, including the narrow bevel, at poster resolution.
		const SDL_Rect lenses[]={{266,413,12,29},{367,476,19,36}};
		for (int side=0;side<2;++side)
		{
			const auto& lens=lenses[side];
			for (int py=lens.y;py<lens.y+lens.h;++py)
				for (int px=lens.x;px<lens.x+lens.w;++px)
				{
					const auto ink=texel(print,px,py);
					const float r=float((ink>>16)&255),g=float((ink>>8)&255),b=float(ink&255);
					const float yellow=smooth((r-105.f)/70.f)*smooth((g-80.f)/80.f)*
						smooth((std::min(r,g)-b-8.f)/56.f);
					if (yellow>.015f) glass.push_back({(py-lensTop*S)*lensWidth*S+px-lensLeft*S,side,yellow});
				}
		}
		jet.init(originX + 180, originY + 64, 13, 24, SDL_BLENDMODE_ADD);
		for (int j = 0; j < jet.rect.h * S; ++j)
			for (int i = 0; i < jet.rect.w * S; ++i)
			{
				const float x = 180 + (i + .5f) / S, y = 64 + (j + .5f) / S;
				const auto pixel = texel(print, 180 * S + i, 64 * S + j);
				const float r = float((pixel >> 16) & 255), g = float((pixel >> 8) & 255),
							b = float(pixel & 255);
				const float exhaustEdge=181.f+4.1f*smooth((y-74.f)/5.5f);
				const float edge=smooth((x-exhaustEdge+.5f)/1.25f)*smooth((192.1f-x)/.42f);
				const float heat =
					smooth((r - 128.f) / 58.f) * smooth((r - g - 8.f) / 39.f) * smooth((g - b - 2.f) / 26.f);
				if (edge * heat > .01f) flame.push_back({j * jet.rect.w * S + i, x, y, edge * heat});
			}
		inkWidth = print->w;
		pigment.assign(inkWidth * depth * S, 0);
		for (int j = 0; j < depth * S; ++j)
			for (int i = 10 * S; i < 192 * S; ++i)
			{
				const float x = (i + .5f) / S, y = top + (j + .5f) / S;
				// Follow the bottom of the printed title, clear of the cockpit.
				if (y > (x < 64 ? 100 : x < 80 ? 84 : x < 98 ? 75 : x < 130 ? 70 : 57)) continue;
				const auto pixel = texel(print, i, j + top * S);
				const float r = float((pixel >> 16) & 255), g = float((pixel >> 8) & 255),
							b = float(pixel & 255);
				const float purple = smooth((r - g - 10.f) / 25.f) * smooth((b - r + 17.f) / 30.f) *
									 smooth((b - g - 20.f) / 54.f);
				pigment[j * inkWidth + i] = uint8_t(chan(255.f * purple * smooth((r - 45.f) / 83.f)));
			}
		lettering.init(originX, originY + top, 36, depth, SDL_BLENDMODE_ADD);
	}

	void draw(float time, int w, int h)
	{
		const auto plain = render::DestinationRect;
		const SDL_FRect viewport{float(plain.x), float(plain.y), float(plain.w), float(plain.h)};
		planet.draw(time,viewport,w,h);
		if (!jet.texture || !lettering.texture || accentBypass) return;
		unsigned epoch;
		const float age = eventAge(time, 46.f, 7.f, 8.f, epoch);
		const float gleam = eventAge(time, 101.f, 63.f, 25.f, epoch);
		const float ageStar=eventAge(time,97.f,44.f,18.f,epoch);
		if (ageStar>=0.f && ageStar<1.85f && star.texture)
		{
			// Centers of isolated stars on the original poster, clear of all foreground ink.
			constexpr float locations[][2]={{54.63f,122.12f},{146.60f,132.22f},{51.28f,36.66f},{49.58f,164.24f}};
			const auto &location=locations[epoch%4];
			const int left=int(location[0])-5, topY=int(location[1])-5;
			star.rect.x=originX+left;
			star.rect.y=originY+topY;
			const float pulse=smooth(ageStar/.52f)*smooth((1.85f-ageStar)/1.10f);
			for (int j=0;j<11*S;++j)
				for (int i=0;i<11*S;++i)
				{
					const float x=left+(i+.5f)/S-location[0], y=topY+(j+.5f)/S-location[1];
					const float d=x*x+y*y;
					const float light=pulse*(.78f*expf(-d*1.6f)+.20f*expf(-d*.48f)+
						.43f*expf(-x*x*.36f-y*y*8.f)+.43f*expf(-y*y*.36f-x*x*8.f));
					star.pixels[j*11*S+i]=rgb(109*light,131*light,153*light);
				}
			star.draw(viewport,w,h);
		}
		const float lensAge=eventAge(time,117.f,26.f,21.f,epoch);
		if (lensAge>=0.f && lensAge<1.69f && headlights.texture)
		{
			// Keep the quiet pilot lights away from the other poster accents.
			// Sample their phase at the onset so a glimmer is never cut in half.
			if (fabsf(ageStar-lensAge)>3.4f && fabsf(gleam-lensAge)>3.4f && fabsf(age-lensAge)>3.4f)
			{
				const auto pulse=[](float t) {return smooth(t/.59f)*smooth((1.54f-t)/.74f);};
				const float level[]={pulse(lensAge-.15f),pulse(lensAge)};
				for (const auto& dot : glass)
				{
					const float current=level[dot.side]*dot.weight;
					headlights.pixels[dot.index]=rgb(6.f*current,19.f*current,39.f*current);
				}
				headlights.draw(viewport,w,h);
			}
		}
		if (age >= 0 && age < 1.85f)
		{
			const float gate=smooth(age/.25f)*smooth((1.85f-age)/.72f);
			for (const auto &dot : flame)
			{
				const float ray = dot.x - .48f * dot.y;
				const float roll = .5f + .5f * cosf(ray * .94f - age * 10.8f);
				const float heat=gate*dot.weight*(.24f+.76f*roll*roll);
				jet.pixels[dot.index]=rgb(18*heat,105*heat,58*heat);
			}
			jet.draw(viewport, w, h);
		}
		if (gleam >= 0 && gleam < 1.8f)
		{
			const float progress = gleam / 1.8f, center = 4.f + 194.f * progress;
			const int left = int(floorf(center)) - 18;
			lettering.rect.x = originX + left;
			const int span = lettering.rect.w * S;
			for (int j = 0; j < depth * S; ++j)
				for (int i = 0; i < span; ++i)
				{
					const int px = left * S + i;
					auto &color = lettering.pixels[j * span + i];
					color = 0;
					if (px < 0 || px >= inkWidth || !pigment[j * inkWidth + px]) continue;
					const float d = (px + .5f) / S - center + .13f * ((j + .5f) / S - 43.f);
					const float glint = pigment[j * inkWidth + px] / 255.f *
										(.68f * expf(-d * d / 3.4f) + .32f * expf(-d * d / 27.f));
					color = rgb(41 * glint, 55 * glint, 49 * glint);
				}
			lettering.draw(viewport, w, h);
		}
	}
	void reset()
	{
		jet.reset();
		planet.reset();
		star.reset();
		headlights.reset();
		glass.clear();
		lettering.reset();
		flame.clear();
		pigment.clear();
	}
} posterPrint;

struct Point
{
	float x, y;
};
struct Segment
{
	Point a, b;
	float brightness, width;
};
float randomUnit(unsigned &state)
{
	state = 1664525u * state + 1013904223u;
	return float(state >> 8) / 16777216.f;
}
float segmentDistance(float x, float y, const Point &a, const Point &b)
{
	const float dx = b.x - a.x, dy = b.y - a.y;
	const float t = sat(((x - a.x) * dx + (y - a.y) * dy) / std::max(.001f, dx * dx + dy * dy));
	const float ax = x - a.x - t * dx, ay = y - a.y - t * dy;
	return ax * ax + ay * ay;
}
std::vector<Segment> filaments(int variant, int side)
{
	unsigned seed = 0x4a29ac62u + variant * 9427 + side * 4331;
	std::vector<Segment> result;
	Point last{8.f, 5.4f};
	for (int i = 1; i <= 17; ++i)
	{
		const float t = i / 17.f, arch = sinf(pi * t);
		const float bend = 6.6f * arch + .9f * sinf(2 * pi * t);
		const Point next{8.f + 20.8f * t + bend + arch * (randomUnit(seed) - .5f) * 4.2f,
						 5.4f + t * 58.5f + arch * (randomUnit(seed) - .5f) * 2.8f};
		result.push_back({last, next, .87f + .13f * randomUnit(seed), .30f + .16f * randomUnit(seed)});
		if (i > 1 && i < 15 && (i + variant) % 4 == 0)
		{
			Point elbow{next.x + 1.6f + randomUnit(seed) * 1.8f, next.y + .4f + randomUnit(seed) * 2.8f};
			Point end{elbow.x + .8f + randomUnit(seed) * 2.6f, elbow.y + randomUnit(seed) * 2.6f};
			result.push_back({next, elbow, .60f, .22f});
			result.push_back({elbow, end, .32f, .12f});
		}
		last = next;
	}
	return result;
}
struct Sling
{
	Picture picture;
	int x{}, y{}, side{};
	float struck{-10.f};
	std::vector<unsigned> inner[3], outer[8];
	struct Site
	{
		unsigned index, ownerOffset;
		int row;
	};
	std::vector<Site> sites;
	int ownerStride{};
	void init(int hand, int left, int top, SDL_Surface *const frames[3])
	{
		if (!frames[0] || !boardArt) return;
		side = hand;
		x = left;
		y = top;
		const int w = frames[0]->w / S, h = frames[0]->h / S, pad = 7;
		picture.init(x - pad, y - pad, w + 2 * pad, h + 2 * pad, SDL_BLENDMODE_ADD);
		const int span = picture.rect.w * S;
		for (auto &image : inner)
			image.assign(picture.pixels.size(), 0);
		for (auto &image : outer)
			image.assign(picture.pixels.size(), 0);
		for (int j = 0; j < h * S; ++j)
			for (int i = 0; i < w * S; ++i)
			{
				const float u = (hand == 0 ? i + .5f : w * S - i - .5f) / S, v = (j + .5f) / S;
				if (v < 9.f || v > 60.f || u < 4.f || u > 27.f) continue;
				const auto dark = texel(boardArt, x * S + i, y * S + j);
				for (int k = 0; k < 3; ++k)
				{
					auto color = texel(frames[k] ? frames[k] : frames[0], i, j);
					const int r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
					const float glow = std::max(sat((b - r + 12.f) / 57.f),
												smooth((std::min(r, std::min(g, b)) - 128.f) / 85.f)) *
									   sat((b - 72.f) / 65.f);
					if (glow <= 0.f) continue;
					inner[k][(j + pad * S) * span + i + pad * S] =
						rgb(glow * (r - int((dark >> 16) & 255)), glow * (g - int((dark >> 8) & 255)),
							glow * (b - int(dark & 255)));
				}
			}
		for (int k = 0; k < 8; ++k)
		{
			auto lines = filaments(k, side);
			for (int j = 0; j < picture.rect.h * S; ++j)
				for (int i = 0; i < span; ++i)
				{
					const float px = (i + .5f) / S - pad, py = (j + .5f) / S - pad, u = hand ? w - px : px;
					float core = 0.f, halo = 0.f;
					for (const auto &l : lines)
					{
						if (u < std::min(l.a.x, l.b.x) - 4.f || u > std::max(l.a.x, l.b.x) + 4.f ||
							py < std::min(l.a.y, l.b.y) - 4.f || py > std::max(l.a.y, l.b.y) + 4.f)
							continue;
						const float d = segmentDistance(u, py, l.a, l.b);
						core = std::max(core, l.brightness * expf(-.5f * d / (l.width * l.width)));
						halo = std::max(halo, l.brightness * expf(-.52f * d));
					}
					for (const Point p : {Point{8.f, 5.4f}, Point{28.8f, 63.9f}})
					{
						const float dx = u - p.x, dy = py - p.y, dist = dx * dx + dy * dy;
						core += .95f * expf(-dist / .9f);
						halo += .72f * expf(-dist / 6.f);
					}
					outer[k][j * span + i] =
						rgb(234.f * core + 37.f * halo, 248.f * core + 73.f * halo, 210.f * core + 175.f * halo);
				}
		}
	}
	void registerSites(int stride)
	{
		sites.clear();
		const int width = picture.rect.w * S;
		for (int i = 0; i < int(picture.pixels.size()); ++i)
		{
			unsigned bits = 0;
			for (const auto &image : inner)
				bits |= image[i];
			for (const auto &image : outer)
				bits |= image[i];
			if (bits & 0xffffff)
				sites.push_back(
					{unsigned(i),
					 unsigned((picture.rect.y * S + i / width) * stride + picture.rect.x * S + i % width),
					 i / width});
		}
		ownerStride = stride;
	}
	float strength(float time) const
	{
		const float age = time - struck;
		if (age < 0.f || age >= .63f) return 0.f;
		// Fast individual discharges keep restriking while the charge decays.
		// Hold a useful bright interval before drawing the short residual glow.
		const float envelope = age < .19f ? 1.f : expf(-(age - .19f) * 3.6f) * smooth((.63f - age) / .14f);
		return 1.10f * envelope * (.93f + .07f * cosf(age * 133.f));
	}
	void draw(float gameTime, float decorTime, const SDL_FRect &dest, int w, int h, const uint16_t *owners, int stride,
			  std::vector<CabinetEffects::Light> &lights)
	{
		if (!picture.texture) return;
		const float age = gameTime - struck, flash = strength(gameTime);
		const float idleTime = flash > 0.f ? gameTime : decorTime;
		const int phase = flash > 0 ? int(age * 69.f) % 8 : 0;
		const int inside = flash > 0 ? (int(age * 55.f) % 3) : 0;
		const auto &a = inner[inside];
		const auto &b = outer[phase];
		// Keep the original scorers and 100 ms light timer intact. Only the
		// luminous filaments continue to discharge after the switch opens.
		const float idle = .205f + .028f * sinf(idleTime * 2.13f + side) + .020f * sinf(idleTime * 4.81f + side * 1.7f);
		const float innerGain = flash > 0 && age < .1f ? idle : idle + .96f * flash;
		if (ownerStride != stride) registerSites(stride);
		// A short local corona travels over a second branch at irregular
		// intervals. The etched primary symbol remains visible underneath.
		const float epoch = idleTime * .56f + side * .31f;
		unsigned seed = unsigned(floorf(epoch)) * 43867u + side * 511u + 61u;
		const int ghostPhase = 1 + int(randomUnit(seed) * 1.99f);
		const float ageInEpoch = epoch - floorf(epoch);
		const float onset = .10f + .52f * randomUnit(seed), travel = (ageInEpoch - onset) / .25f;
		const auto &ghost = inner[ghostPhase];
		const int in = int(innerGain * 256), out = int(flash * 256);
		std::vector<int> corona(picture.rect.h * S);
		if (!flash && travel >= 0 && travel < 1)
			for (int row = 0; row < int(corona.size()); ++row)
			{
				const float distance = (row / S - 13.f - travel * 48.f) / 4.4f;
				corona[row] = int(35.f * expf(-distance * distance) * sinf(pi * travel));
			}
		for (const auto site : sites)
		{
			const auto p = site.index;
			const int owner = owners[site.ownerOffset];
			if (owner != Board && owner != 181 + side && owner != 470 + side)
			{
				picture.pixels[p] = 0;
				continue;
			}
			const auto c = a[p], d = b[p], e = ghost[p];
			const int g = corona[site.row];
			auto chan = [&](int shift) {
				const unsigned value =
					((c >> shift) & 255) * in + ((d >> shift) & 255) * out + ((e >> shift) & 255) * g;
				return std::min(255u, (value + 128) >> 8);
			};
			picture.pixels[p] = 0xff000000u | (chan(16) << 16) | (chan(8) << 8) | chan(0);
		}
		picture.draw(dest, w, h);
		if (flash > .035f)
			for (auto p : {Point{8.f, 5.4f}, Point{22.f, 30.f}, Point{28.8f, 63.9f}})
				lights.push_back({x + (side ? 33.f - p.x : p.x), y + p.y, 3.4f, flash});
	}
	void reset()
	{
		picture.reset();
		for (auto &im : inner)
			im.clear();
		for (auto &im : outer)
			im.clear();
		sites.clear();
		ownerStride = 0;
		struck = -10.f;
	}
};
Sling slings[2];
} // namespace

void CabinetEffects::Init(const SDL_Surface *board, const SDL_Surface *poster, const SDL_Surface *planetMap)
{
	boardArt = board;
	if (!board || board->w != 365 * S || board->h != 470 * S) return;
	FieldTraces::Init(board);
	galaxies[0].init(0, 285.7f, 173.6f, 21.6f, 19.5f, .0425f);
	galaxies[1].init(1, 262.25f, 22.10f, 17.4f, 10.05f, -.0495f, 1.23f, true, -.112f);
	galaxies[2].init(2, 274.38f, 33.35f, 8.1f, 5.7f, .0845f, 3.71f);
	galaxies[3].init(-1, 211.2f, 15.92f, 5.65f, 3.27f, -.087f, 2.56f, false, .13f);
	galaxies[4].init(-1, 76.12f, 54.65f, 6.38f, 4.52f, .065f, 4.1f, false, -.37f, true);
	posterPrint.init(poster,planetMap);
}
void CabinetEffects::SetSling(int side, int x, int y, SDL_Surface *const frames[3])
{
	slings[side].init(side, x, y, frames);
}
void CabinetEffects::Strike(int side, float time)
{
	if (side >= 0 && side < 2) slings[side].struck = time;
}
void CabinetEffects::Draw(float gameTime, float decorTime, const SDL_FRect &dest, int w, int h, const uint16_t *owners, int stride,
						  std::vector<Light> &lights)
{
	unsigned event;
	const float age = eventAge(decorTime, 77.f, 34.f, 25.f, event);
	const float nova = accentBypass ? 0.f : smooth(age / .27f) * smooth((1.7f - age) / 1.19f);
	for (int i = 0; i < 3; ++i)
		galaxies[i].belt.draw(decorTime, dest, w, h, owners, stride);
	for (int i = 0; i < 5; ++i)
		if (galaxies[i].picture.texture)
			galaxies[i].draw(decorTime, dest, w, h, owners, stride, i == int((event * 3u) % 5) ? nova : 0.f,
							 (event + 1u) % 3);
	FieldTraces::Draw(decorTime, dest, w, h, owners, stride);
	for (auto &s : slings)
		s.draw(gameTime, decorTime, dest, w, h, owners, stride, lights);
	posterPrint.draw(decorTime, w, h);
}
void CabinetEffects::SetCelestialAccentDiagnosticBypass(bool bypass)
{
	accentBypass = bypass;
}
float CabinetEffects::FlashStrength(int side, float time)
{
	return side >= 0 && side < 2 ? slings[side].strength(time) : 0.f;
}
void CabinetEffects::Reset()
{
	for (auto &g : galaxies)
		g.reset();
	for (auto &s : slings)
		s.reset();
	FieldTraces::Reset();
	posterPrint.reset();
	accentBypass = false;
	boardArt = nullptr;
}
