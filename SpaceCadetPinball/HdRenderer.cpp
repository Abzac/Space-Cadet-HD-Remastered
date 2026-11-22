#include "pch.h"
#include "HdRenderer.h"
#include "DomeVeins.h"
#include "RailReflections.h"
#include "TableRelief.h"
#include "CabinetEffects.h"
#include "TPlunger.h"
#include "TBall.h"

#include "GroupData.h"
#include "loader.h"
#include "options.h"
#include "fullscrn.h"
#include "pb.h"
#include "proj.h"
#include "render.h"
#include "winmain.h"

namespace
{
constexpr int S = HdRenderer::Scale;
constexpr int Tile = 24;
gdrv_bitmap8* screen = nullptr;
SDL_Texture* sceneTexture = nullptr;
struct Layer
{
	SDL_Surface* image{};
	bool authored{};
	std::vector<uint8_t> coverage;
	std::vector<uint16_t> depth;
};
struct SpriteArt
{
	Layer frames[3];
	std::vector<uint32_t> spinnerFrames;
	unsigned group{}, frame{};
	float pulseStart{-1};
	bool isArc() const { return group == 180 || group == 181; }
	Layer& current() { return frames[frame]; }
};
std::unordered_map<const gdrv_bitmap8*, SpriteArt> assets;
constexpr int SpringSteps = 16, SpringLast = 7 * SpringSteps;
struct SpringFrame
{
	rectangle_type box{};
	std::vector<uint32_t> pixels;
	std::vector<uint16_t> depth;
};
std::vector<SpringFrame> springFrames;
std::vector<uint32_t> scene;
std::vector<uint16_t> sceneDepth, surfaceOwner;
std::vector<uint8_t> tiles;
int pitch, tileCols, tileRows;
int edgeRadius, edgeStrength;

struct Rgb
{
	float r, g, b;
};
Rgb rgb(uint32_t c)
{
	return {float((c >> 16) & 255), float((c >> 8) & 255), float(c & 255)};
}
int channel(float v)
{
	return static_cast<int>(Clamp(v + 0.5f, 0.0f, 255.0f));
}
uint32_t pixel(float r, float g, float b, int alpha = 255)
{
	return (uint32_t(alpha) << 24) | (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

void mark(int x, int y, int w, int h)
{
	if (w < 1 || h < 1) return;
	if (edgeRadius)
	{
		// The neighboring tile can sample this tile across a component's rim.
		const int endX = std::min(screen->Width, x+w+1), endY = std::min(screen->Height, y+h+1);
		x = std::max(x-1,0);
		y = std::max(y-1,0);
		w = endX-x;
		h = endY-y;
	}
	for (int ty = y / Tile; ty <= (y + h - 1) / Tile; ++ty)
		std::fill(tiles.begin() + ty * tileCols + x / Tile, tiles.begin() + ty * tileCols + (x + w - 1) / Tile + 1,
				  1);
}

void span(const gdrv_bitmap8* bmp, const SDL_Surface* art, int dx, int dy, int sx, int sy, int width)
{
	auto out = scene.data() + dy * S * pitch + dx * S;
	if (art)
	{
		const int stride = art->pitch / sizeof(uint32_t);
		auto src = static_cast<const uint32_t*>(art->pixels) + sy * S * stride + sx * S;
		for (int i = 0; i < S; ++i)
			std::memcpy(out + i * pitch, src + i * stride, width * S * sizeof(uint32_t));
	}
	else
	{
		const auto* src = bmp->BmpBufPtr1 + sy * bmp->Stride + sx;
		for (int i = 0; i < width; ++i)
			std::fill_n(out + i * S, S, src[i].Color | 0xff000000u);
		for (int i = 1; i < S; ++i)
			std::memcpy(out + i * pitch, out, width * S * sizeof(uint32_t));
	}
}

// Choose runs with the same pass/fail test as the native sprite compositor.
template <typename Predicate>
void maskedRow(const gdrv_bitmap8* bmp, const SDL_Surface* art, int dx, int dy, int sx, int sy, int width,
			   Predicate passes)
{
	int i = 0;
	while (i < width)
	{
		while (i < width && !passes(i))
			++i;
		int start = i;
		while (i < width && passes(i))
			++i;
		if (start != i) span(bmp, art, dx + start, dy, sx + start, sy, i - start);
	}
}

Rgb sample(float x, float y, int boardWidth)
{
	int sx = Clamp(static_cast<int>(x * S), 0, boardWidth * S - 1);
	int sy = Clamp(static_cast<int>(y * S), 0, screen->Height * S - 1);
	return rgb(scene[sy * pitch + sx]);
}

struct Core
{
	float x{}, y{}, rx{}, ry{}, weight{};
};
struct Lamp
{
	SDL_Texture* texture{};
	float x{}, y{}, rx{}, ry{};
	Rgb color{};
	int pad{}, width{}, height{};
	std::vector<Core> cores;
};
struct LitPoint
{
	float x, y;
	Rgb color;
	float radius{3.f}, strength{1.f};
	unsigned owner{};
};
std::unordered_map<const Layer*, Lamp> lamps;
std::vector<LitPoint> litPoints;
std::vector<SDL_Texture*> sphereTextures;

// Recover the surface normal around an insert from the original camera-depth
// samples. Fluted rails and flat artwork therefore respond differently when
// the same bulb switches on. Clamp discontinuities at silhouettes.
float surfaceResponse(int x, int y, float lightX, float lightY, Rgb color)
{
	const auto* z = render::background_zmap;
	if (!z || x < 1 || y < 1 || x >= z->Width - 1 || y >= z->Height - 1) return 0.8f;
	const auto* d = z->ZPtr1 + y * z->Stride + x;
	if (d[0] == 65535 || d[-1] == 65535 || d[1] == 65535 || d[-z->Stride] == 65535 || d[z->Stride] == 65535)
		return 0.8f;
	float units = std::max(1.0f, 2.0f * proj::DepthUnitsPerPixel(d[0]));
	float nx = Clamp((d[1] - d[-1]) / units, -2.5f, 2.5f);
	float ny = Clamp((d[z->Stride] - d[-z->Stride]) / units, -2.5f, 2.5f);
	float normalLength = sqrtf(nx * nx + ny * ny + 1.0f);
	float lx = lightX - x, ly = lightY - y;
	float inverseDistance = 1.0f / sqrtf(lx * lx + ly * ly + 25.0f);
	lx *= inverseDistance;
	ly *= inverseDistance;
	float lz = 5.0f * inverseDistance;
	float diffuse = std::max(0.0f, (nx * lx + ny * ly + lz) / normalLength);
	float halfLength = sqrtf(2.0f + 2.0f * lz);
	float highlight = std::max(0.0f, (nx * lx + ny * ly + 1.0f + lz) / (normalLength * halfLength));
	float metal = std::min(color.r, std::min(color.g, color.b)) / 255.0f;
	return 0.65f + 0.44f * diffuse + 1.8f * metal * powf(highlight, 12.0f);
}

Lamp bakeLamp(const render_sprite& sprite, int boardWidth, const SpriteArt& art)
{
	auto bmp = sprite.Bmp;
	Lamp lamp;
	int w = bmp->Width, h = bmp->Height;
	lamp.pad = Clamp((w + h) / 3, 4, 16);
	lamp.width = w + 2 * lamp.pad;
	lamp.height = h + 2 * lamp.pad;
	int lw = lamp.width, lh = lamp.height;
	std::vector<Rgb> emission(lw * lh), output(lw * lh);
	float total = 0;
	Rgb bulbColor{};
	float bulbWeight = 0.f;
	const auto& layer = art.frames[art.frame];
	const int segments = art.isArc() ? 8 : 0;
	lamp.cores.resize(segments);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			Rgb c{};
			if (layer.image && !layer.coverage.empty())
			{
				for (int j = 0; j < S; ++j)
				{
					const auto* row = reinterpret_cast<const uint32_t*>(
						static_cast<const uint8_t*>(layer.image->pixels) + (y * S + j) * layer.image->pitch);
					for (int i = 0; i < S; ++i)
					{
						Rgb color = rgb(row[x * S + i]);
						float a = layer.coverage[(y * S + j) * w * S + x * S + i] / (255.f * S * S);
						c.r += a * color.r;
						c.g += a * color.g;
						c.b += a * color.b;
					}
				}
			}
			else
			{
				if (sprite.ZMap->ZPtr1[y * sprite.ZMap->Stride + x] == 65535) continue;
				c = rgb(bmp->BmpBufPtr1[y * bmp->Stride + x].Color);
			}
			float peak = std::max(c.r, std::max(c.g, c.b)) / 255.0f;
			float weight = peak * peak * peak;
			auto& e = emission[(y + lamp.pad) * lw + x + lamp.pad];
			e = {c.r * weight, c.g * weight, c.b * weight};
			lamp.x += (x + 0.5f) * weight;
			lamp.y += (y + 0.5f) * weight;
			lamp.color.r += e.r;
			lamp.color.g += e.g;
			lamp.color.b += e.b;
			const float chroma = (std::max(c.r, std::max(c.g, c.b)) - std::min(c.r, std::min(c.g, c.b))) / 255.f;
			const float radiating = weight * weight * (.22f + 4.5f * chroma * chroma);
			bulbWeight += radiating;
			bulbColor.r += radiating * c.r;
			bulbColor.g += radiating * c.g;
			bulbColor.b += radiating * c.b;
			total += weight;
			if (segments)
			{
				auto& core = lamp.cores[std::min(segments - 1, y * segments / h)];
				float bright = weight * weight * weight;
				core.x += (x + .5f) * bright;
				core.y += (y + .5f) * bright;
				core.weight += bright;
			}
		}
	if (total < 0.05f) return lamp;
	lamp.x /= total;
	lamp.y /= total;
	lamp.rx = std::max(1.5f, w * 0.44f);
	lamp.ry = std::max(1.5f, h * 0.44f);
	lamp.color = {lamp.color.r / total, lamp.color.g / total, lamp.color.b / total};
	if (bulbWeight > 0.f)
		lamp.color = {bulbColor.r / bulbWeight, bulbColor.g / bulbWeight, bulbColor.b / bulbWeight};
	for (auto& core : lamp.cores)
		if (core.weight > 0.f)
		{
			core.x /= core.weight;
			core.y /= core.weight;
			core.rx = std::max(2.0f, w * .15f);
			core.ry = std::max(2.f, float(h) / segments * .78f);
		}

	// A tight diffuser around the insert plus a softer pool of light on the table.
	for (int pass = 0; pass < 2; ++pass)
	{
		float sigma = pass ? lamp.pad * 0.49f : 1.3f;
		int radius = static_cast<int>(ceilf(2.5f * sigma));
		std::vector<float> weights(2 * radius + 1);
		float sum = 0;
		for (int k = -radius; k <= radius; ++k)
			sum += weights[k + radius] = expf(-0.5f * k * k / (sigma * sigma));
		for (auto& weight : weights)
			weight /= sum;
		std::vector<Rgb> horizontal(lw * lh);
		for (int y = 0; y < lh; ++y)
			for (int x = 0; x < lw; ++x)
			{
				auto& c = horizontal[y * lw + x];
				for (int k = std::max(-radius, -x); k <= std::min(radius, lw - 1 - x); ++k)
				{
					auto e = emission[y * lw + x + k];
					float f = weights[k + radius];
					c.r += e.r * f;
					c.g += e.g * f;
					c.b += e.b * f;
				}
			}
		for (int y = 0; y < lh; ++y)
			for (int x = 0; x < lw; ++x)
			{
				auto& c = output[y * lw + x];
				for (int k = std::max(-radius, -y); k <= std::min(radius, lh - 1 - y); ++k)
				{
					auto e = horizontal[(y + k) * lw + x];
					float f = weights[k + radius] * (pass ? 0.97f : 0.35f);
					c.r += e.r * f;
					c.g += e.g * f;
					c.b += e.b * f;
				}
			}
	}
	std::vector<uint32_t> pixels(lw * lh);
	for (int y = 0; y < lh; ++y)
		for (int x = 0; x < lw; ++x)
		{
			auto c = output[y * lw + x];
			int px = sprite.BmpRect.XPosition + x - lamp.pad, py = sprite.BmpRect.YPosition + y - lamp.pad;
			auto surface = sample(px + 0.5f, py + 0.5f, boardWidth);
			float gain = surfaceResponse(px, py, sprite.BmpRect.XPosition + lamp.x,
										 sprite.BmpRect.YPosition + lamp.y, surface);
			// The pool belongs to the mounting face. Nearby raised steel can
			// receive real reflected light in lightReceivers, below.
			const auto boardZ = render::background_zmap;
			if (art.group >= 115 && art.group <= 260 && !art.isArc() && !layer.depth.empty() &&
				boardZ && px >= 0 && py >= 0 && px < boardZ->Width && py < boardZ->Height)
			{
				const int lx = Clamp(x-lamp.pad,0,w-1)*S+S/2;
				const int ly = Clamp(y-lamp.pad,0,h-1)*S+S/2;
				const int delta = int(layer.depth[ly*w*S+lx])-int(boardZ->ZPtr1[py*boardZ->Stride+px]);
				gain *= 1.f-.96f*Clamp((delta-420.f)/600.f,0.f,1.f);
			}
			pixels[y * lw + x] = pixel(c.r * gain, c.g * gain, c.b * gain);
		}
	lamp.texture = SDL_CreateTexture(winmain::Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, lw, lh);
	if (lamp.texture)
	{
		SDL_UpdateTexture(lamp.texture, nullptr, pixels.data(), lw * 4);
		SDL_SetTextureBlendMode(lamp.texture, SDL_BLENDMODE_ADD);
		SDL_SetTextureScaleMode(lamp.texture, SDL_ScaleModeLinear);
	}
	return lamp;
}

bool lampVisible(const render_sprite& sprite, const zmap_header_type& z)
{
	for (int y = 0; y < sprite.Bmp->Height; ++y)
	{
		int py = y + sprite.BmpRect.YPosition;
		if (py < 0 || py >= z.Height) continue;
		for (int x = 0; x < sprite.Bmp->Width; ++x)
		{
			int px = x + sprite.BmpRect.XPosition;
			if (px < 0 || px >= z.Width) continue;
			auto d = sprite.ZMap->ZPtr1[y * sprite.ZMap->Stride + x];
			if (d != 65535 && d == z.ZPtr1[py * z.Stride + px]) return true;
		}
	}
	return false;
}

SDL_Surface* readSurface(const std::string& path, int width, int height)
{
	auto input = SDL_LoadBMP(path.c_str());
	if (!input) return nullptr;
	auto result = input->w == width && input->h == height
					  ? SDL_ConvertSurfaceFormat(input, SDL_PIXELFORMAT_ARGB8888, 0)
					  : nullptr;
	SDL_FreeSurface(input);
	return result;
}

std::vector<uint32_t> loadSpinnerFrames(const std::string& stem, int w, int h)
{
	auto rgbSheet = readSurface(stem + ".bmp", w * 8, h * 4);
	auto masks = readSurface(stem + "-mask.bmp", w * 8, h * 4);
	std::vector<uint32_t> pixels;
	if (rgbSheet && masks)
	{
		pixels.resize(32 * w * h);
		for (int phase = 0; phase < 32; ++phase)
			for (int y = 0; y < h; ++y)
			{
				const int sy = phase / 8 * h + y, sx = phase % 8 * w;
				const auto rgb = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(rgbSheet->pixels) + sy * rgbSheet->pitch) + sx;
				const auto mask = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(masks->pixels) + sy * masks->pitch) + sx;
				auto row = pixels.data() + (phase * h + y) * w;
				for (int x = 0; x < w; ++x) row[x] = (mask[x] << 24) | (rgb[x] & 0x00ffffffu);
			}
	}
	SDL_FreeSurface(masks);
	SDL_FreeSurface(rgbSheet);
	return pixels;
}

// Color is authored at 4x. Reconstruct depth only, extending original surface
// values through its transparent border so a newly drawn curved outline can
// still be occluded by a rail on a different layer.
void prepareLayer(Layer& layer, const std::string& stem, const gdrv_bitmap8& bmp, const zmap_header_type* z,
				  const zmap_header_type* apron = nullptr, int apronX = 0, int apronY = 0)
{
	const int w = bmp.Width, h = bmp.Height, hw = w * S, hh = h * S;
	layer.image = readSurface(stem + ".bmp", hw, hh);
	if (!layer.image || !z || z->Width != w || z->Height != h) return;
	auto mask = readSurface(stem + "-mask.bmp", hw, hh);
	layer.authored = mask != nullptr;
	std::vector<uint16_t> extended(w * h, 65535);
	std::vector<int> frontier;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const int i = y * w + x;
			extended[i] = z->ZPtr1[y * z->Stride + x];
			if (extended[i] != 65535) frontier.push_back(i);
		}
	for (size_t i = 0; i < frontier.size(); ++i)
	{
		int p = frontier[i], x = p % w;
		auto spread = [&](int neighbor)
		{
			if (extended[neighbor] == 65535)
			{
				extended[neighbor] = extended[p];
				frontier.push_back(neighbor);
			}
		};
		if (x > 0) spread(p - 1);
		if (x + 1 < w) spread(p + 1);
		if (p >= w) spread(p - w);
		if (p + w < w * h) spread(p + w);
	}
	layer.coverage.resize(hw * hh);
	layer.depth.resize(hw * hh);
	for (int y = 0; y < hh; ++y)
	{
		auto maskRow =
			mask ? reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(mask->pixels) + y * mask->pitch)
				 : nullptr;
		const float fy = (y + .5f) / S - .5f;
		const int iy = int(floorf(fy)), ay = Clamp(iy, 0, h - 1), by = Clamp(iy + 1, 0, h - 1);
		const float v = fy - iy;
		for (int x = 0; x < hw; ++x)
		{
			const float fx = (x + .5f) / S - .5f;
			const int ix = int(floorf(fx)), ax = Clamp(ix, 0, w - 1), bx = Clamp(ix + 1, 0, w - 1);
			const float u = fx - ix;
			const int p = y * hw + x, center = (y / S) * w + x / S;
			const uint16_t ref = extended[center];
			auto dz = [&](int k)
			{
				const float d = float(extended[k]) - ref;
				return fabsf(d) < 150.f ? d : 0.f;
			};
			float delta = (1 - v) * ((1 - u) * dz(ay * w + ax) + u * dz(ay * w + bx)) +
						  v * ((1 - u) * dz(by * w + ax) + u * dz(by * w + bx));
			layer.depth[p] = uint16_t(Clamp(ref + delta, 0.f, 65535.f));
			if (maskRow)
			{
				float visible = 1.f;
				if (apron)
				{
					auto exposed = [&](int a, int b)
					{
						const int x = apronX + b, y = apronY + a;
						return x >= 0 && y >= 0 && x < apron->Width && y < apron->Height &&
									   extended[a * w + b] <= apron->ZPtr1[y * apron->Stride + x]
								   ? 1.f
								   : 0.f;
					};
					visible = (1 - v) * ((1 - u) * exposed(ay, ax) + u * exposed(ay, bx)) +
							  v * ((1 - u) * exposed(by, ax) + u * exposed(by, bx));
					visible = Clamp((visible - .25f) / .50f, 0.f, 1.f);
				}
				layer.coverage[p] = uint8_t(channel(visible * (maskRow[x] & 255)));
			}
			else
			{
				auto solid = [&](int a, int b) { return z->ZPtr1[a * z->Stride + b] != 65535 ? 1.f : 0.f; };
				float f = (1 - v) * ((1 - u) * solid(ay, ax) + u * solid(ay, bx)) +
						  v * ((1 - u) * solid(by, ax) + u * solid(by, bx));
				layer.coverage[p] = uint8_t(channel((f - .30f) * 255 / .40f));
			}
		}
	}
	if (mask) SDL_FreeSurface(mask);
}

// Read the in-between poses baked from the same steel helix as the eight
// approved bitmaps. Interpolate the two native camera-depth maps in head/seat
// coordinates so the ball and rails can still pass in front of the mechanism.
bool readSpringMotion(SDL_RWops* in, const gdrv_bitmap8& table)
{
	char signature[4];
	if (SDL_RWread(in,signature,1,4)!=4 || memcmp(signature,"SPR1",4) ||
		SDL_ReadLE16(in)!=SpringLast+1 || SDL_ReadLE16(in)!=S) return false;
	gdrv_bitmap8* keys[8]{};
	for (int k=0;k<8;++k)
	{
		if (473+k>=int(pb::record_table->Groups.size())) return false;
		keys[k]=pb::record_table->GetBitmap(473+k);
		const auto i=assets.find(keys[k]);
		if (i==assets.end() || !i->second.current().authored || i->second.current().depth.empty()) return false;
	}
	springFrames.resize(SpringLast+1);
	for (int phase=0;phase<=SpringLast;++phase)
	{
		auto& f=springFrames[phase];
		const int k=phase/SpringSteps;
		const auto a=keys[k];
		if (phase%SpringSteps==0)
		{
			f.box={a->XPosition-table.XPosition,a->YPosition-table.YPosition,a->Width,a->Height};
			continue;
		}
		const auto b=keys[k+1];
		const int left=keys[0]->XPosition+SDL_ReadLE16(in), top=keys[0]->YPosition+SDL_ReadLE16(in);
		const int w=SDL_ReadLE16(in), h=SDL_ReadLE16(in);
		if (left<a->XPosition || left>b->XPosition || top<a->YPosition || top>b->YPosition ||
			w<1 || w>std::max(a->Width,b->Width)+1 || h<1 || h>std::max(a->Height,b->Height)+1) return false;
		f.box={left-table.XPosition,top-table.YPosition,w,h};
		f.pixels.resize(w*h*S*S);
		if (SDL_RWread(in,f.pixels.data(),4,f.pixels.size())!=f.pixels.size()) return false;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		for (auto& pixel : f.pixels) pixel = SDL_SwapLE32(pixel);
#endif
		const auto& za=assets.at(a).current().depth;
		const auto& zb=assets.at(b).current().depth;
		f.depth.resize(f.pixels.size());
		const float t=float(phase%SpringSteps)/SpringSteps, head=a->YPosition+t*(b->YPosition-a->YPosition);
		const float seat=table.YPosition+414.24f;
		for (int y=0;y<h*S;++y)
			for (int x=0;x<w*S;++x)
			{
				const float gy=top+(y+.5f)/S,gx=left+(x+.5f)/S;
				const float travel=Clamp((seat-gy)/(seat-head),0.f,1.f);
				auto sampleDepth=[&](const gdrv_bitmap8* key,const std::vector<uint16_t>& z) {
					const float dy=(key->YPosition-head)*travel;
					int ix=Clamp(int((gx+.147f*dy-key->XPosition)*S),0,key->Width*S-1);
					int iy=Clamp(int((gy+dy-key->YPosition)*S),0,key->Height*S-1);
					return z[iy*key->Width*S+ix];
				};
				const auto da=sampleDepth(a,za),db=sampleDepth(b,zb);
				f.depth[y*w*S+x]=uint16_t(da+t*(int(db)-int(da))+.5f);
			}
	}
	return SDL_RWtell(in)==SDL_RWsize(in);
}
struct BallPose
{
	vector3 world{}, camera{};
	vector2 center{}, foot{};
	float radius{}, physicalRadius{};
	vector3 grainX{}, grainY{};
};
std::unordered_map<const render_sprite*, BallPose> ballPoses;

float saturate(float x)
{
	return Clamp(x, 0.f, 1.f);
}
float smooth(float x)
{
	x = saturate(x);
	return x * x * (3.f - 2.f * x);
}

struct CabinetRoom
{
	float left{}, right{}, back{}, front{};
	const SDL_Surface* backglass{};
	bool measured{};
} cabinet;

void measureCabinet()
{
	if (cabinet.measured) return;
	const auto left = proj::ReverseXForm({29, 400}), right = proj::ReverseXForm({339, 400});
	cabinet.left = std::min(left.X, right.X);
	cabinet.right = std::max(left.X, right.X);
	const auto near = proj::ReverseXForm({182, 411}), far = proj::ReverseXForm({182, 7});
	cabinet.back = std::min(near.Y, far.Y);
	cabinet.front = std::max(near.Y, far.Y);
	cabinet.measured = true;
}

// Ray intersection with the local cabinet and room. Rectangular diffusers
// and their dark mullions occupy fixed locations in table/world space.
// All lighting calculations below are in linear light.
Rgb room(const vector3& position, const vector3& ray)
{
	const auto world = proj::WorldDirection(ray);
	const float wx = world.X, wy = world.Y, wz = world.Z;
	const float x = position.X, y = position.Y, z = position.Z;
	if (cabinet.measured)
	{
		float side = 1.e7f, back = 1.e7f;
		float sideHeight = 0.f, backX = 0.f, backHeight = 0.f;
		if (fabsf(wx) > .0001f)
		{
			const float t = ((wx > 0 ? cabinet.right : cabinet.left) - x) / wx;
			const float hy = y + t * wy, hz = z + t * wz;
			if (t > .015f && hy >= cabinet.back && hy <= cabinet.front && hz >= .03f && hz <= 1.48f)
			{
				side = t;
				sideHeight = hz;
			}
		}
		if (wy < -.001f)
		{
			const float t = (cabinet.back - y) / wy;
			const float hx = x + t * wx, hz = z + t * wz;
			if (t > .015f && hx >= cabinet.left && hx <= cabinet.right && hz > 0.f && hz < 13.f)
			{
				back = t;
				backX = hx;
				backHeight = hz;
			}
		}
		if (side < back)
		{
			// Raised stainless sidewall, black insulating rebate and rolled lip.
			const float line = expf(-32.f * (sideHeight - .94f) * (sideHeight - .94f));
			const float lip = smooth((sideHeight - 1.08f) / .32f);
			const float wall = .038f + .16f * sideHeight + .96f * line + .70f * lip;
			return {wall * .94f, wall, wall * 1.08f};
		}
		if (back < 1.e7f && cabinet.backglass)
		{
			const float u = (backX - cabinet.left) / (cabinet.right - cabinet.left), v = 1.f - backHeight / 13.f;
			const auto s = cabinet.backglass;
			const int ix = Clamp(int((.07f + .85f * u) * s->w), 0, s->w - 1),
					  iy = Clamp(int((.04f + .33f * v) * s->h), 0, s->h - 1);
			const auto c =
				rgb(reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(s->pixels) + iy * s->pitch)[ix]);
			const float metal =
				smooth((.07f - std::min(u, 1.f - u)) / .045f) + smooth((.06f - std::min(v, 1.f - v)) / .04f);
			return {.055f + c.r * c.r / 120000.f + metal * .80f, .057f + c.g * c.g / 120000.f + metal * .85f,
					.065f + c.b * c.b / 120000.f + metal * .90f};
		}
	}
	if (wz > .05f)
	{
		const float t = (21.f - z) / wz, hx = x + t * wx, hy = y + t * wy;
		if (fabsf(hx) < 32.f && fabsf(hy) < 42.f)
		{
			// A long, narrow sleeve and its dark recessed housing reflect as
			// adjacent curved bands on a steel sphere, with space between the tubes.
			const float cross = hx * .47f + (.88f * hy) - 19.f;
			const float length = (.88f * hx - .47f * hy + 3.f) / 23.f;
			const float extent = expf(-length * length * length * length);
			const float diffuser = extent * expf(-cross * cross / 4.3f);
			const float housing = extent * expf(-(cross - 6.f) * (cross - 6.f) / 29.f);
			const float strip = smooth((2.4f - fabsf(hx + 10.f)) / 1.5f) * smooth((22.f - fabsf(hy - 2.f)) / 2.7f);
			// A continuous bank down the length of the cabinet stays visible
			// along the upper flank, including at the far end of the table.
			const float bank =
				expf(-.5f * (hx - 18.f) * (hx - 18.f) / 15.f) * smooth((24.f - fabsf(hy - 7.f)) / 5.f);
			// Diffuse bounce from the light banks rolls across the vault.
			const float bounce = expf(-((hx - 7.f) * (hx - 7.f) / 470.f + (hy - 20.f) * (hy - 20.f) / 300.f));
			const float ceiling = (.035f + .43f * bounce + .15f * wz * wz) * (1.f - .94f * housing);
			return {ceiling + diffuser * 3.8f + strip * 1.15f + bank * 2.5f,
					ceiling * 1.04f + diffuser * 3.7f + strip * 1.27f + bank * 2.58f,
					ceiling * 1.10f + diffuser * 3.5f + strip * 1.43f + bank * 2.72f};
		}
	}
	const float wall = smooth((wz + .04f) / .59f);
	const float kick = smooth((-.28f - wz) / .24f);
	return {.008f + .080f * wall + .018f * kick, .010f + .094f * wall + .019f * kick,
			.014f + .112f * wall + .025f * kick};
}

const std::vector<float>& linearLut()
{
	static const auto lut = []()
	{
		std::vector<float> result(256);
		for (int i = 0; i < 256; ++i)
		{
			float v = i / 255.f;
			result[i] = v <= .04045f ? v / 12.92f : powf((v + .055f) / 1.055f, 2.4f);
		}
		return result;
	}();
	return lut;
}

struct ReflectionMip
{
	int width, height;
	std::vector<Rgb> light;
};
std::vector<ReflectionMip> reflectionMips;

void refreshReflections(int x, int y, int w, int h)
{
	// Only dirty tiles enter here. Build a linear-light irradiance pyramid so
	// one reflected texel can cover a broad footprint at grazing incidence.
	if (reflectionMips.empty())
	{
		int mw = screen->Width, mh = screen->Height;
		while (mw > 1 || mh > 1)
		{
			reflectionMips.push_back({mw, mh, std::vector<Rgb>(mw * mh)});
			mw = (mw + 1) / 2;
			mh = (mh + 1) / 2;
		}
	}
	const auto& lut = linearLut();
	int right = x + w, bottom = y + h;
	for (int yy = y; yy < bottom; ++yy)
		for (int xx = x; xx < right; ++xx)
		{
			Rgb sum{};
			for (int j = 0; j < S; ++j)
				for (int i = 0; i < S; ++i)
				{
					const uint32_t c = scene[(yy * S + j) * pitch + xx * S + i];
					sum.r += lut[(c >> 16) & 255];
					sum.g += lut[(c >> 8) & 255];
					sum.b += lut[c & 255];
				}
			reflectionMips[0].light[yy * screen->Width + xx] = {sum.r / (S * S), sum.g / (S * S), sum.b / (S * S)};
		}
	for (size_t level = 1; level < reflectionMips.size(); ++level)
	{
		const auto& previous = reflectionMips[level - 1];
		auto& mip = reflectionMips[level];
		x /= 2;
		y /= 2;
		right = (right + 1) / 2;
		bottom = (bottom + 1) / 2;
		for (int yy = y; yy < bottom; ++yy)
			for (int xx = x; xx < right; ++xx)
			{
				Rgb sum{};
				for (int j = 0; j < 2; ++j)
					for (int i = 0; i < 2; ++i)
					{
						const auto& c = previous.light[std::min(2 * yy + j, previous.height - 1) * previous.width +
													   std::min(2 * xx + i, previous.width - 1)];
						sum.r += c.r;
						sum.g += c.g;
						sum.b += c.b;
					}
				mip.light[yy * mip.width + xx] = {.25f * sum.r, .25f * sum.g, .25f * sum.b};
			}
	}
}

Rgb filteredReflection(float x, float y, float footprint)
{
	const int level = Clamp(int(log2f(footprint)), 0, int(reflectionMips.size()) - 1);
	const auto& mip = reflectionMips[level];
	const float u = Clamp(x / (1 << level) - .5f, 0.f, float(mip.width - 1));
	const float v = Clamp(y / (1 << level) - .5f, 0.f, float(mip.height - 1));
	const int ix = int(u), iy = int(v);
	Rgb radiance{};
	for (int j = 0; j < 2; ++j)
		for (int i = 0; i < 2; ++i)
		{
			const auto& c =
				mip.light[std::min(iy + j, mip.height - 1) * mip.width + std::min(ix + i, mip.width - 1)];
			const float weight = (i ? u - ix : 1.f - u + ix) * (j ? v - iy : 1.f - v + iy);
			radiance.r += weight * c.r;
			radiance.g += weight * c.g;
			radiance.b += weight * c.b;
		}
	return radiance;
}

float encodeSteel(float v)
{
	// Whites above the SDR shoulder feed the separate scRGB emissive pass.
	// The glass-covered cabin has pronounced dark rebates beside bright trim.
	// Preserve this low-end contrast when compressing reflected HDR radiance.
	v = 1.58f * v * (v + .016f) / (v + .43f);
	v = v / (.64f + v);
	return v < .00313f ? 3294.6f * v : 255.f * (1.055f * powf(v, 1.f / 2.4f) - .055f);
}

// Search the real DAT relief in reflection space. A hit supplies scene
// radiance, filtered over a small cone for polished (not perfectly mirrored)
// steel. A miss sees the room. No transmission or background alpha is used.
Rgb tableRadiance(const BallPose& pose, const vector3& ray, const TableRelief::Hit& surface)
{
	const auto coatColor = filteredReflection(surface.uv.X, surface.uv.Y, 1.2f + 3.3f * surface.distance);
	const auto& normal = surface.normal;
	const auto& p = surface.position;
	const float length = maths::magnitude(normal);
	if (length < .00001f) return coatColor;
	const float d = (normal.X * ray.X + normal.Y * ray.Y + normal.Z * ray.Z) / length;
	const float viewSide = normal.X * p.X + normal.Y * p.Y + normal.Z * p.Z;
	if (d * viewSide < 0.f)
	{
		// The DAT gives the outward, directly lit surfaces. Seen from below,
		// a raised guide instead exposes its shadowed mounting/back face.
		const float bounced = .024f + .055f * (1.f - fabsf(d));
		return {bounced + .09f * coatColor.r, bounced + .09f * coatColor.g, bounced + .09f * coatColor.b};
	}
	const float k = 2.f * d / length;
	const vector3 reflected{ray.X - k * normal.X, ray.Y - k * normal.Y, ray.Z - k * normal.Z};
	const auto displacement = proj::WorldDirection(maths::vector_sub(p, pose.camera));
	const vector3 position{pose.world.X + displacement.X, pose.world.Y + displacement.Y,
						   pose.world.Z + displacement.Z};
	const auto ceiling = room(position, reflected);
	const float grazing = 1.f - saturate(fabsf(d));
	const float lacquer = .055f + .80f * grazing * grazing * grazing * grazing;
	const float luma = .2126f * coatColor.r + .7152f * coatColor.g + .0722f * coatColor.b;
	const float indirect = .62f * (1.f - lacquer);
	return {indirect * (.83f * coatColor.r + .17f * luma) + lacquer * ceiling.r,
			indirect * (.83f * coatColor.g + .17f * luma) + lacquer * ceiling.g,
			indirect * (.83f * coatColor.b + .17f * luma) + lacquer * ceiling.b};
}

Rgb reflectedScene(const BallPose& pose, const vector3& origin, const vector3& ray)
{
	TableRelief::Hit surface;
	if (TableRelief::Trace(origin, ray, 22.f, surface)) return tableRadiance(pose, ray, surface);
	return room(pose.world, ray);
}
Rgb polishedReflection(const BallPose& pose, const vector3& hit, const vector3& ray, float ndv)
{
	// Integrate the finite reflection lobe. At a glancing angle a single
	// screen texel would otherwise resolve small alternating rails and paint.
	const float spread = .032f + .035f * (1.f - ndv);
	const float horizontal = std::max(.005f, hypotf(ray.X, ray.Z));
	const vector3 tangent{spread * ray.Z / horizontal, 0.f, -spread * ray.X / horizontal};
	vector3 bitangent{};
	maths::cross(ray, tangent, bitangent);
	Rgb result{};
	const vector3 offset{.82f * tangent.X + .57f * bitangent.X, .82f * tangent.Y + .57f * bitangent.Y,
						 .82f * tangent.Z + .57f * bitangent.Z};
	for (float sign : {-1.f, 1.f})
	{
		const vector3 reflected{ray.X + sign * offset.X, ray.Y + sign * offset.Y, ray.Z + sign * offset.Z};
		const auto c = reflectedScene(pose, hit, reflected);
		result.r += .5f * c.r;
		result.g += .5f * c.g;
		result.b += .5f * c.b;
	}
	// Fine wear spreads part of the conductor's response into a broad lobe.
	// It picks up the bright metal apron below the cabinet horizon and the
	// much darker sidewalls, while the narrow lobe retains nearby moving colors.
	const auto world = proj::WorldDirection(ray);
	const float ceiling = smooth((.70f * world.Z + .50f * world.X - .25f * world.Y + .08f) / .83f);
	const float apron = smooth((-world.Z - .25f) / .65f) * (.64f + .36f * world.Y);
	const float neutral = .012f + .79f * ceiling + .34f * apron;
	const float rough = .145f + .065f * (1.f - ndv);
	return {result.r * (1.f - rough) + rough * neutral, result.g * (1.f - rough) + rough * neutral * 1.03f,
			result.b * (1.f - rough) + rough * neutral * 1.09f};
}

struct LampSpec
{
	float x, y, radius, energy;
	vector3 position;
	Rgb radiance;
	unsigned owner;
};
std::vector<LampSpec> emitters;
SDL_Texture* receiverTexture{};
std::vector<Rgb> receiverLight;
std::vector<unsigned> receiverPixels, litReceivers;
bool bypassLighting{};

float dot(const vector3& a, const vector3& b)
{
	return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
}
void resolveLights(int boardWidth)
{
	emitters.clear();
	const auto& lut = linearLut();
	for (const auto& light : litPoints)
	{
		const int x = Clamp(int(light.x * S), 0, boardWidth*S - 1),
				  y = Clamp(int(light.y * S), 0, screen->Height*S - 1);
		const auto packed = sceneDepth[y * pitch + x];
		if (packed == 65535) continue;
		const auto view = proj::ViewRay(light.x, light.y);
		const auto neighbor = proj::ViewRay(light.x + 1.f, light.y);
		const float distance = proj::DepthDistance(packed);
		const float radius = light.radius * distance * maths::magnitude(maths::vector_sub(neighbor, view));
		const auto lift = proj::CameraDirection({0.f, 0.f, std::max(.04f, .40f * radius)});
		emitters.push_back({light.x,
							light.y,
							radius,
							light.strength,
							{view.X * distance + lift.X, view.Y * distance + lift.Y, view.Z * distance + lift.Z},
							{lut[channel(light.color.r)], lut[channel(light.color.g)], lut[channel(light.color.b)]},
							light.owner});
	}
}
struct SurfacePlacement
{
	float x, y, sx, sy;
	int outW, outH;
	SDL_FRect rect(float left, float top, float w, float h) const
	{
		return {x + left * sx, y + top * sy, w * sx, h * sy};
	}
	void emit(float cx, float cy, float rx, float ry, unsigned lensOwner = 0) const
	{
		EmissiveRegion region{(x + cx * sx) / outW, (y + cy * sy) / outH, rx * sx / outW, ry * sy / outH, {}};
		if (lensOwner)
			for (int row=0; row<16; ++row)
			{
				const int iy=Clamp(int((cy + ((row+.5f)/8.f-1.f)*ry)*S),0,screen->Height*S-1);
				for (int col=0; col<16; ++col)
				{
					const int ix=Clamp(int((cx + ((col+.5f)/8.f-1.f)*rx)*S),0,pitch-1);
					if (surfaceOwner[iy*pitch+ix]!=lensOwner)
						region.covered[row/2] |= uint32_t(1) << (col+16*(row%2));
				}
			}
		render::EmissiveRegions.push_back(region);
	}
};

bool blockedByPart(const vector3& start, const vector3& direction, int boardWidth)
{
	for (const float t : {.36f, .69f})
	{
		const vector3 p{start.X + t * direction.X, start.Y + t * direction.Y, start.Z + t * direction.Z};
		const auto uv = proj::ImagePoint(p);
		if (uv.X < 0.f || uv.X >= boardWidth || uv.Y < 0.f || uv.Y >= screen->Height) continue;
		const auto depth = sceneDepth[int(uv.Y * S) * pitch + int(uv.X * S)];
		const float visible = proj::DepthDistance(depth) + .19f;
		if (depth != 65535 && visible * visible < dot(p, p)) return true;
	}
	return false;
}

void lightReceivers(int boardWidth, const SurfacePlacement& place)
{
	if (bypassLighting || emitters.empty()) return;
	const int count = boardWidth * screen->Height;
	if (!receiverTexture)
	{
		receiverTexture = SDL_CreateTexture(winmain::Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
											boardWidth, screen->Height);
		if (!receiverTexture) return;
		SDL_SetTextureScaleMode(receiverTexture, SDL_ScaleModeLinear);
		SDL_SetTextureBlendMode(receiverTexture, SDL_BLENDMODE_ADD);
		receiverLight.assign(count, {});
		receiverPixels.assign(count, 0);
	}
	for (auto i : litReceivers)
	{
		receiverLight[i] = {};
		receiverPixels[i] = 0;
	}
	litReceivers.clear();
	const auto& base = reflectionMips[0];
	for (const auto& light : emitters)
	{
		const float reach = std::min(31.f, 10.f + light.radius * 91.f);
		const int left = std::max(0, int(light.x - reach)), right = std::min(boardWidth, int(light.x + reach) + 1);
		const int top = std::max(0, int(light.y - reach)),
				  bottom = std::min(screen->Height, int(light.y + reach) + 1);
		for (int y = top; y < bottom; ++y)
			for (int x = left; x < right; ++x)
			{
				const float dx = x + .5f - light.x, dy = y + .5f - light.y;
				if (dx * dx + dy * dy > reach * reach) continue;
				const auto owner = surfaceOwner[(y * S + S / 2) * pitch + x * S + S / 2];
				if (!owner || owner == light.owner) continue;
				const auto c = base.light[y * base.width + x];
				const float maxColor = std::max(c.r, std::max(c.g, c.b));
				const float neutral = std::min(c.r, std::min(c.g, c.b)) / std::max(.018f, maxColor);
				const float metal = neutral * smooth(maxColor * 4.f);
				// Strongest on hardware. Flat printed art only receives dim diffuse spill.
				const float material = owner == 108 ? .12f + .88f * metal : .49f + .51f * metal;
				vector3 p{}, n{};
				if (!TableRelief::Surface(x, y, p, n)) continue;
				const auto dir = maths::vector_sub(light.position, p);
				const float dist2 = dot(dir, dir), inverse = 1.f / sqrtf(std::max(.0001f, dist2));
				const float incidence = dot(n, dir) * inverse;
				if (incidence <= .015f) continue;
				const float radius2 = light.radius * light.radius;
				const float flux = 3.1f * light.energy * material * incidence * radius2 / (dist2 + radius2);
				if (flux < .006f) continue;
				const float eye = 1.f / std::max(.01f, maths::magnitude(p));
				const vector3 half{dir.X * inverse - p.X * eye, dir.Y * inverse - p.Y * eye,
								   dir.Z * inverse - p.Z * eye};
				const float ndh = std::max(0.f, dot(n, half)) / std::max(.005f, maths::magnitude(half));
				const float a2 = .11f - .088f * metal;
				const float d = 1.f + ndh * ndh * (a2 - 1.f);
				const float spec = std::min(4.f, (.065f + .17f * metal) * a2 / (d * d + .0001f));
				const float vis = blockedByPart(p, dir, boardWidth) ? .10f : 1.f;
				const float gain = flux * vis;
				const int i = y * boardWidth + x;
				if (receiverPixels[i] == 0)
				{
					litReceivers.push_back(i);
					receiverPixels[i] = 0xff000000u;
				}
				auto& e = receiverLight[i];
				e.r += gain * light.radiance.r * (spec + .38f * c.r);
				e.g += gain * light.radiance.g * (spec + .38f * c.g);
				e.b += gain * light.radiance.b * (spec + .38f * c.b);
			}
	}
	for (auto i : litReceivers)
	{
		const auto& c = base.light[(i / boardWidth) * base.width + i % boardWidth];
		const auto& e = receiverLight[i];
		auto add = [](float old, float energy) { return 247.f * (sqrtf(std::min(1.f, old + energy)) - sqrtf(old)); };
		receiverPixels[i] = pixel(add(c.r, e.r), add(c.g, e.g), add(c.b, e.b));
	}
	if (!litReceivers.empty())
	{
		SDL_UpdateTexture(receiverTexture, nullptr, receiverPixels.data(), boardWidth * 4);
		const auto area = place.rect(0, 0, float(boardWidth), float(screen->Height));
		SDL_RenderCopyF(winmain::Renderer, receiverTexture, nullptr, &area);
	}
}

void drawBall(render_sprite& ball, size_t index, const zmap_header_type& z, int boardWidth,
			  const SurfacePlacement& place)
{
	ball.HdBallVisible = false;
	if (!ball.Bmp || ball.DirtyRect.Width <= 0) return;
	auto it = ballPoses.find(&ball);
	if (it == ballPoses.end()) return;
	const BallPose& pose = it->second;
	const float cx = pose.center.X, cy = pose.center.Y, radius = pose.radius;
	if (radius < 1.f || !std::isfinite(radius)) return;
	const auto& center3 = pose.camera;
	const float sphereSquared = pose.physicalRadius * pose.physicalRadius;
	// Express the analytical silhouette in output pixels. The stock 4x
	// samples continue to shade the steel, its reflection and its depth.
	const float coverageScale = options::Options.SmoothSilhouettes ?
		std::min(float(S), std::min(place.sx,place.sy) / 1.65f) : float(S);
	const int margin = int(ceilf(radius * 1.7f)) + 2;
	const int side = (2 * margin + 1) * S;
	const int ox = int(floorf(cx)) - margin, oy = int(floorf(cy)) - margin;
	constexpr int maxSide = 256;
	if (side > maxSide) return;

	std::vector<const LampSpec*> nearby;
	for (const auto& light : emitters)
	{
		const float dx = light.x - cx, dy = light.y - cy;
		if (dx * dx + dy * dy < 64.f * 64.f) nearby.push_back(&light);
	}
	static std::vector<uint32_t> pixels;
	pixels.assign(side * side, 0);
	int visible = 0;
	// Sample two nonparallel polishing directions in the steel itself. Only
	// the microscopic slopes travel with the ball; the light and room stay put.
	auto reflectionRay = [&](const vector3& view, const vector3& n, float& wear) {
		const float u = dot(n, pose.grainX), v = dot(n, pose.grainY);
		const float broad = 5.1f*u + 2.4f*v + 1.9f*u*v;
		const float fine = 17.f*v - 6.3f*u + 2.2f*u*u;
		const float slopeX = .018f*cosf(broad) + .004f*cosf(fine);
		const float slopeY = .009f*cosf(fine) + .005f*sinf(7.f*u - 4.f*v);
		const float normalGain = 1.f - u*slopeX - v*slopeY;
		const vector3 micro{normalGain*n.X + slopeX*pose.grainX.X + slopeY*pose.grainY.X,
		                    normalGain*n.Y + slopeX*pose.grainX.Y + slopeY*pose.grainY.Y,
		                    normalGain*n.Z + slopeX*pose.grainX.Z + slopeY*pose.grainY.Z};
		const float k = 2.f*dot(view, micro)/dot(micro, micro);
		wear = .972f + .023f*sinf(broad) + .005f*sinf(fine);
		return vector3{view.X-k*micro.X, view.Y-k*micro.Y, view.Z-k*micro.Z};
	};
	const float shadowX = pose.foot.X + .34f * radius, shadowY = pose.foot.Y + .21f * radius;
	// The finite polished lobe is broader than an HD pixel. Reconstruct its
	// radiance from a 2x table-resolution grid. Keep direct reflections,
	// contact shadows, geometry tests and the silhouette at the full 4x.
	const int gridSide = side / 2 + 1;
	static std::vector<Rgb> grid;
	grid.assign(gridSide * gridSide, {-2.f, 0, 0});
	auto probe = [&](int u, int v) -> Rgb
	{
		auto& sample = grid[(v / 2) * gridSide + u / 2];
		if (sample.r != -2.f) return sample;
		const auto eye = proj::ViewRay(ox + (u + .5f) / S, oy + (v + .5f) / S);
		vector3 cross{};
		maths::cross(center3, eye, cross);
		const float remainder = sphereSquared - dot(cross, cross);
		if (remainder <= 0.f) return sample = {-1.f, 0, 0};
		const float t = dot(eye, center3) - sqrtf(remainder);
		const vector3 hit{eye.X * t, eye.Y * t, eye.Z * t};
		const vector3 n{(hit.X - center3.X) / pose.physicalRadius, (hit.Y - center3.Y) / pose.physicalRadius,
						(hit.Z - center3.Z) / pose.physicalRadius};
		const float ndv = saturate(-dot(n, eye));
		float wear;
		const auto ray = reflectionRay(eye, n, wear);
		return sample = polishedReflection(pose, hit, ray, ndv);
	};
	for (int v = 0; v < side; ++v)
		for (int u = 0; u < side; ++u)
		{
			float x = ox + (u + 0.5f) / S, y = oy + (v + 0.5f) / S;
			if (x < 0 || x >= boardWidth || y < 0 || y >= z.Height) continue;
			const uint16_t geometryDepth = sceneDepth[int(y * S) * pitch + int(x * S)];
			const auto view = proj::ViewRay(x, y);
			const float along = view.X * center3.X + view.Y * center3.Y + view.Z * center3.Z;
			vector3 crossing{};
			maths::cross(center3, view, crossing);
			const float lateralSquared = crossing.X * crossing.X + crossing.Y * crossing.Y + crossing.Z * crossing.Z;
			const float coverage = saturate((1.f - sqrtf(lateralSquared / sphereSquared)) * radius * coverageScale + .5f);
			float shadow = 0;
			if (geometryDepth != 65535 && geometryDepth > ball.Depth)
			{
				float dx = (x - shadowX) / radius, dy = (y - shadowY) / radius;
				float d2 = dx * dx + 1.12f * dy * dy;
				if (d2 < 3.6f) shadow = .49f * expf(-1.35f * d2) + .45f * expf(-7.f * d2);
			}
			const float front = along - sqrtf(std::max(0.f, sphereSquared - lateralSquared));
			if (coverage == 0.f || (geometryDepth != 65535 && proj::DepthDistance(geometryDepth) <= front))
			{
				pixels[v * side + u] = uint32_t(channel(shadow * 255.f)) << 24;
				continue;
			}
			++visible;
			const vector3 hit{view.X * front, view.Y * front, view.Z * front};
			const vector3 normal{(hit.X - center3.X) / pose.physicalRadius,
								 (hit.Y - center3.Y) / pose.physicalRadius,
								 (hit.Z - center3.Z) / pose.physicalRadius};
			const float ndv = saturate(-(normal.X * view.X + normal.Y * view.Y + normal.Z * view.Z));
			float wear;
			const auto ray = reflectionRay(view, normal, wear);
			const int u0 = u & ~1, v0 = v & ~1, u1 = u0 + 2 * (u & 1), v1 = v0 + 2 * (v & 1);
			const Rgb a = probe(u0, v0), b = probe(u1, v0), d = probe(u0, v1), e = probe(u1, v1);
			Rgb c{};
			if (a.r < 0 || b.r < 0 || d.r < 0 || e.r < 0)
				c = polishedReflection(pose, hit, ray, ndv);
			else
				c = {.25f * (a.r + b.r + d.r + e.r), .25f * (a.g + b.g + d.g + e.g), .25f * (a.b + b.b + d.b + e.b)};
			for (const auto* source : nearby)
			{
				const auto light = maths::vector_sub(source->position, hit);
				const float d2 = dot(light, light), inverse = 1.f / sqrtf(std::max(.00001f, d2));
				const float r2 = source->radius * source->radius, omega = r2 / (r2 + d2);
				if (dot(normal, light) <= 0.f) continue;
				const float alignment = saturate(dot(ray, light) * inverse);
				if (alignment < .73f) continue;
				const float angle2 = 2.f * (1.f - alignment);
				const float aperture = .60f * omega + .010f;
				const float gloss = expf(-angle2 / aperture) * omega / (omega + .012f);
				const float f = 4.8f * source->energy * gloss;
				c.r += f * source->radiance.r;
				c.g += f * source->radiance.g;
				c.b += f * source->radiance.b;
			}
			float fresnel = powf(1.f - ndv, 5.f);
			float opacity = coverage + (1.f - coverage) * shadow;
			float edge = coverage / opacity;
			pixels[v * side + u] =
				pixel(edge * encodeSteel(c.r * (.65f + .35f * fresnel) * wear),
					  edge * encodeSteel(c.g * (.67f + .33f * fresnel) * wear),
					  edge * encodeSteel(c.b * (.69f + .31f * fresnel) * wear), channel(255.f * opacity));
		}
	if (!visible) return;
	const SDL_FRect tableDestination = place.rect(0.f, 0.f, float(screen->Width), float(screen->Height));
	RailReflections::Draw(winmain::Renderer, tableDestination, screen->Width, screen->Height, surfaceOwner.data(),
						  pitch, boardWidth, cx, cy, radius, pixels.data(), side, ox, oy);
	if (index >= sphereTextures.size()) sphereTextures.resize(index + 1);
	auto& tex = sphereTextures[index];
	if (!tex)
	{
		tex = SDL_CreateTexture(winmain::Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, maxSide,
								maxSide);
		if (!tex) return;
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
	}
	SDL_Rect src{0, 0, side, side};
	SDL_UpdateTexture(tex, &src, pixels.data(), side * 4);
	auto dest = place.rect(float(ox), float(oy), float(side) / S, float(side) / S);
	ball.HdBallVisible = SDL_RenderCopyF(winmain::Renderer, tex, &src, &dest) == 0;
	if (ball.HdBallVisible) DomeVeins::CoverWithBall(pixels.data(), side, ox*S, oy*S);
	if (visible > Pi * radius * radius * S * S * .5f) place.emit(cx, cy, radius, radius);
}
} // namespace

void HdRenderer::WallContact(const gdrv_bitmap8* bmp)
{
	const auto it = assets.find(bmp);
	if (it != assets.end() && it->second.group >= 469 && it->second.group <= 470)
		CabinetEffects::Strike(int(it->second.group) - 469, pb::time_now);
}

void HdRenderer::SetReceiverDiagnosticBypass(bool enabled)
{
	bypassLighting = enabled;
}

void HdRenderer::TrackBall(render_sprite* sprite, const vector3& world, float radius, const BallOrientation& q)
{
	if (!screen || radius <= 0.f) return;
	auto& p = ballPoses[sprite];
	p.grainX = proj::CameraDirection({1.f-2.f*(q.y*q.y+q.z*q.z), 2.f*(q.x*q.y+q.w*q.z), 2.f*(q.x*q.z-q.w*q.y)});
	p.grainY = proj::CameraDirection({2.f*(q.x*q.y-q.w*q.z), 1.f-2.f*(q.x*q.x+q.z*q.z), 2.f*(q.y*q.z+q.w*q.x)});
	p.world = world;
	p.camera = proj::CameraPoint(world);
	p.physicalRadius = radius;
	p.center = proj::BallImage(world, radius, p.radius);
	vector3 foot{world.X, world.Y, world.Z - radius};
	float unused;
	p.foot = proj::BallImage(foot, radius, unused);
}

void HdRenderer::Init(gdrv_bitmap8* native)
{
	Shutdown();
	if (!pb::record_table || pb::FullTiltMode || pb::FullTiltDemoMode) return;
	char* base = SDL_GetBasePath();
	const std::string dir = std::string(base ? base : "") + "HD/";
	SDL_free(base);
	const auto tableId = pb::record_table->record_labeled("table");
	const auto tableBmp = tableId >= 0 ? pb::record_table->GetBitmap(tableId) : nullptr;
	const auto tableZ = tableBmp ? pb::record_table->Groups[tableId]->GetZMap(fullscrn::GetResolution()) : nullptr;
	for (auto group : pb::record_table->Groups)
	{
		auto bmp = group->GetBitmap(fullscrn::GetResolution());
		if (!bmp) continue;
		auto& art = assets[bmp];
		art.group = group->GroupId;
		const auto stem = dir + std::to_string(art.group);
		auto depth = group->GetZMap(fullscrn::GetResolution());
		// Side kicker plungers travel below the opaque lower cabinet apron.
		const bool kicker = art.group >= 505 && art.group <= 508 && tableBmp;
		prepareLayer(art.frames[0], stem, *bmp, depth, kicker ? tableZ : nullptr,
					 kicker ? bmp->XPosition - tableBmp->XPosition : 0,
					 kicker ? bmp->YPosition - tableBmp->YPosition : 0);
		if (art.frames[0].image) ++render::HdAssetCount;
		if ((art.group == 484 || art.group == 492) && art.frames[0].authored)
			art.spinnerFrames = loadSpinnerFrames(dir + "spinner-" + std::to_string(art.group - 3), bmp->Width * S, bmp->Height * S);
		if (art.group == 2) cabinet.backglass = art.frames[0].image;
		if (art.isArc())
			for (int i = 1; i < 3; ++i)
				prepareLayer(art.frames[i], stem + "-arc" + std::to_string(i), *bmp, depth);
	}
	if (!render::HdAssetCount)
	{
		Shutdown();
		return;
	}
	if (tableBmp)
		if (auto in=SDL_RWFromFile((dir+"plunger-motion.bin").c_str(),"rb"))
		{
			if (!readSpringMotion(in,*tableBmp)) springFrames.clear();
			SDL_RWclose(in);
		}
	pitch = native->Width * S;
	sceneTexture = SDL_CreateTexture(winmain::Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, pitch,
									 native->Height * S);
	if (!sceneTexture)
	{
		Shutdown();
		return;
	}
	SDL_SetTextureScaleMode(sceneTexture, SDL_ScaleModeLinear);
	SDL_SetTextureBlendMode(sceneTexture, SDL_BLENDMODE_NONE);
	screen = native;
	if (tableBmp && assets.count(tableBmp))
	{
		auto planetMap=readSurface(dir + "poster-planet.bmp",768,384);
		CabinetEffects::Init(assets[tableBmp].current().image, cabinet.backglass, planetMap);
		SDL_FreeSurface(planetMap);
		for (auto& entry : assets)
			if (entry.second.isArc())
			{
				auto& art = entry.second;
				SDL_Surface* frames[]{art.frames[0].image, art.frames[1].image, art.frames[2].image};
				CabinetEffects::SetSling(int(art.group) - 180, entry.first->XPosition - tableBmp->XPosition,
										 entry.first->YPosition - tableBmp->YPosition, frames);
			}
	}
	RailReflections::Init(dir);
	scene.assign(pitch * native->Height * S, 0xff000000u);
	sceneDepth.assign(scene.size(), 65535);
	surfaceOwner.assign(scene.size(), 0);
	tileCols = (native->Width + Tile - 1) / Tile;
	tileRows = (native->Height + Tile - 1) / Tile;
	tiles.assign(tileCols * tileRows, 1);
}

void HdRenderer::Shutdown()
{
	edgeRadius = edgeStrength = 0;
	RailReflections::Shutdown();
	TableRelief::Reset();
	if (receiverTexture) SDL_DestroyTexture(receiverTexture);
	receiverTexture = nullptr;
	receiverLight.clear();
	receiverPixels.clear();
	litReceivers.clear();
	emitters.clear();
	cabinet = CabinetRoom{};
	CabinetEffects::Reset();
	if (sceneTexture) SDL_DestroyTexture(sceneTexture);
	sceneTexture = nullptr;
	for (auto& it : assets)
		for (auto& layer : it.second.frames)
			SDL_FreeSurface(layer.image);
	for (auto& it : lamps)
		if (it.second.texture) SDL_DestroyTexture(it.second.texture);
	for (auto tex : sphereTextures)
		if (tex) SDL_DestroyTexture(tex);
	assets.clear();
	springFrames.clear();
	lamps.clear();
	sphereTextures.clear();
	litPoints.clear();
	ballPoses.clear();
	scene.clear();
	sceneDepth.clear();
	surfaceOwner.clear();
	tiles.clear();
	reflectionMips.clear();
	screen = nullptr;
	render::HdAssetCount = 0;
	render::EmissiveRegions.clear();
}

void HdRenderer::AnimateDischarges(const std::vector<render_sprite*>& sprites)
{
	if (!screen) return;
	for (auto sprite : sprites)
	{
		if (!sprite->Bmp) continue;
		auto it = assets.find(sprite->Bmp);
		if (it == assets.end() || !it->second.isArc()) continue;
		auto& art = it->second;
		if (sprite->DirtyFlag || art.pulseStart < 0.f || pb::time_now - art.pulseStart > .15f)
			art.pulseStart = pb::time_now;
		const float elapsed = std::max(0.f, pb::time_now - art.pulseStart);
		unsigned phase = std::min(2u, unsigned(elapsed * 32.f));
		if (!art.frames[phase].image) phase = 0;
		if (phase != art.frame)
		{
			art.frame = phase;
			sprite->DirtyFlag = true;
		}
	}
}

void HdRenderer::ClearDepth(int w, int h, int x, int y)
{
	if (!screen) return;
	for (int row = 0; row < h * S; ++row)
	{
		const int start = (y * S + row) * pitch + x * S;
		std::fill_n(sceneDepth.data() + start, w * S, 65535);
		std::fill_n(surfaceOwner.data() + start, w * S, 0);
	}
}

void HdRenderer::Fill(gdrv_bitmap8* dst, int w, int h, int x, int y, ColorRgba c)
{
	if (dst != screen || !screen) return;
	for (int i = 0; i < h * S; ++i)
		std::fill_n(scene.data() + (y * S + i) * pitch + x * S, w * S, c.Color | 0xff000000u);
	mark(x, y, w, h);
}

void HdRenderer::Copy(gdrv_bitmap8* dst, int w, int h, int x, int y, gdrv_bitmap8* bmp, int sx, int sy,
					  bool transparent)
{
	if (dst != screen || !screen) return;
	auto it = assets.find(bmp);
	// Ball restore buffers are native-only scratch copies. The HD scene keeps
	// the unobstructed artwork; HD balls are drawn on top during presentation.
	if (it == assets.end()) return;
	for (int j = 0; j < h; ++j)
	{
		if (transparent)
		{
			auto* src = bmp->BmpBufPtr1 + (sy + j) * bmp->Stride + sx;
			maskedRow(bmp, it->second.current().image, x, y + j, sx, sy + j, w,
					  [src](int i) { return src[i].Color != 0; });
		}
		else
			span(bmp, it->second.current().image, x, y + j, sx, sy + j, w);
	}
	mark(x, y, w, h);
}

bool HdRenderer::HasSpinnerFrames(const gdrv_bitmap8* carrier)
{
	const auto it = assets.find(carrier);
	return screen && it != assets.end() && !it->second.spinnerFrames.empty();
}

static void paintRegion(gdrv_bitmap8* dst, int w, int h, int x, int y, gdrv_bitmap8* bmp, int sx, int sy,
					   const uint16_t* z, int stride, int spinnerFrame, const SpringFrame* spring)
{
	if (dst != screen || !screen) return;
	auto it = assets.find(bmp);
	const Layer* layer = it == assets.end() ? nullptr : &it->second.current();
	const auto image = layer ? layer->image : nullptr;
	const uint16_t owner = it == assets.end() ? 0 : uint16_t(it->second.group + 1);
	const bool mapped = layer && !layer->depth.empty();
	const uint32_t* motion = spring ? spring->pixels.data() : spinnerFrame < 0 ? nullptr :
	    it->second.spinnerFrames.data() + spinnerFrame * bmp->Width * bmp->Height * S * S;
	const int frameWidth=spring ? spring->box.Width : bmp->Width;
	const auto depth=spring ? spring->depth.data() : mapped ? layer->depth.data() : nullptr;
	for (int j = 0; j < h * S; ++j)
	{
		const int start = (y * S + j) * pitch + x * S;
		const int srcOffset = (sy * S + j) * frameWidth * S + sx * S;
		auto src = motion ? motion+srcOffset : image ? reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(image->pixels) +
															 (sy * S + j) * image->pitch) +
							   sx * S
						 : nullptr;
		for (int i = 0; i < w * S; ++i)
		{
			const int d = start + i, k = srcOffset + i;
			uint16_t distance = depth ? depth[k] : z[(j / S) * stride + i / S];
			const int alpha = motion ? int(src[i] >> 24) : mapped ? layer->coverage[k] : 255;
			if (!alpha || distance == 65535) continue;
			// The DAT table depth includes the original low-resolution hardware.
			// Its pixels are the mounting surface for replacement artwork. Other
			// live parts still occlude one another, and the depth of the new
			// surface is kept for ball clipping and reflection rays.
			constexpr uint16_t boardOwner = 107 + 1;
			const auto under = surfaceOwner[d];
			const bool boardMount = layer && layer->authored && under == boardOwner;
			const bool insertOverRubber = (owner == 181 && under == 470) || (owner == 182 && under == 471);
			const bool rubberBelowInsert = (under == 181 && owner == 470) || (under == 182 && owner == 471);
			if ((owner == boardOwner && under && under != boardOwner) || rubberBelowInsert) continue;
			if (!boardMount && !insertOverRubber && distance > sceneDepth[d]) continue;
			uint32_t color = src ? src[i] : bmp->BmpBufPtr1[(sy + j / S) * bmp->Stride + sx + i / S].Color;
			if (owner == 470 || owner == 471)
			{
				// Original spring-band silhouette carries a cold-blue discharge.
				// Its sharp white filament is drawn separately between electrodes.
				const float level = float((color >> 8) & 255);
				color = pixel(.18f * level, .24f * level + 7.f, .37f * level + 19.f);
			}
			if (alpha == 255)
			{
				scene[d] = color | 0xff000000u;
			}
			else
			{
				if (surfaceOwner[d] == owner && sceneDepth[d] == distance) continue;
				const uint32_t back = scene[d];
				const int inv = 255 - alpha;
				const auto mix = [&](int shift) {
					return ((((color >> shift) & 255) * alpha + ((back >> shift) & 255) * inv + 127) / 255) << shift;
				};
				scene[d] = 0xff000000u | mix(16) | mix(8) | mix(0);
			}
			surfaceOwner[d] = owner;
			sceneDepth[d] = distance;
		}
	}
	mark(x, y, w, h);
}

void HdRenderer::Paint(gdrv_bitmap8* dst, int w, int h, int x, int y, gdrv_bitmap8* bmp, int sx, int sy,
					   const uint16_t* z, int stride, int spinnerFrame)
{
	paintRegion(dst,w,h,x,y,bmp,sx,sy,z,stride,spinnerFrame,nullptr);
}

bool HdRenderer::HasPlungerMotion() { return !springFrames.empty(); }

void HdRenderer::UpdatePlunger(render_sprite& sprite)
{
	const auto p=sprite.HdPlunger;
	const int phase=p->PullbackStartedFlag && p->MaxPullback>0 ?
		int(std::lround(Clamp(p->Boost/p->MaxPullback,0.f,1.f)*SpringLast)) : 0;
	if (phase!=sprite.PlungerPhase)
	{
		sprite.PlungerPhase=phase;
		sprite.DirtyFlag=true;
	}
}

void HdRenderer::PaintPlunger(const render_sprite& sprite, const rectangle_type& dirty)
{
	if (!screen || springFrames.empty()) return;
	const int phase=std::max(sprite.PlungerPhase,0);
	const auto& frame=springFrames[phase];
	rectangle_type clip{};
	if (!maths::rectangle_clip(frame.box,dirty,&clip)) return;
	const auto& native=sprite.HdPlunger->ListBitmap->at(phase/SpringSteps);
	paintRegion(screen,clip.Width,clip.Height,clip.XPosition,clip.YPosition,native.Bmp,
		clip.XPosition-frame.box.XPosition,clip.YPosition-frame.box.YPosition,nullptr,0,-1,
		frame.pixels.empty()?nullptr:&frame);
}

// Resolve only silhouettes between registered table surfaces. Sampling the
// already composited scene preserves the actual colors on both sides of a
// contour, including a lower part seen through a rotating spinner. The
// Reflection rays and HDR light masks continue to sample the original geometry.
static void uploadSceneRect(const SDL_Rect& rect)
{
	const auto original = scene.data() + rect.y*pitch + rect.x;
	if (!edgeRadius)
	{
		SDL_UpdateTexture(sceneTexture, &rect, original, pitch*sizeof(uint32_t));
		return;
	}
	static std::vector<uint32_t> resolved;
	resolved.resize(rect.w*rect.h);
	const int r = edgeRadius, dy = pitch*r;
	const int tableWidth = render::background_zmap ? std::min(screen->Width,render::background_zmap->Width)*S : 0;
	const int start = std::max(0,r-rect.x), end = std::min(rect.w,tableWidth-r-rect.x);
	const int offsets[]{-dy-r,-dy,-dy+r,-r,r,dy-r,dy,dy+r};
	const int weights[]{1,2,1,2,2,1,2,1};
	for (int y=0; y<rect.h; ++y)
	{
		const auto row = original+y*pitch;
		auto dst = resolved.data()+y*rect.w;
		std::copy_n(row,rect.w,dst);
		if (rect.y+y<r || rect.y+y>=screen->Height*S-r) continue;
		const auto owners = surfaceOwner.data() + (rect.y+y)*pitch+rect.x;
		for (int x=start; x<end; ++x)
		{
			const uint16_t id=owners[x];
			if (owners[x-r]==id && owners[x+r]==id && owners[x-dy]==id && owners[x+dy]==id &&
				owners[x-dy-r]==id && owners[x-dy+r]==id && owners[x+dy-r]==id && owners[x+dy+r]==id) continue;
			const auto center=row[x];
			const int red=(center>>16)&255, green=(center>>8)&255, blue=center&255;
			int dr=0,dg=0,db=0;
			for (int tap=0; tap<8; ++tap)
			{
				const auto otherId=owners[x+offsets[tap]];
				constexpr uint16_t board=107+1;
				if (id==otherId || ((id==0 || id==board) && (otherId==0 || otherId==board))) continue;
				const auto c=row[x+offsets[tap]];
				const int w=weights[tap];
				dr+=w*(int((c>>16)&255)-red);
				dg+=w*(int((c>>8)&255)-green);
				db+=w*(int(c&255)-blue);
			}
			const auto resolve=[](int center,int sum){return (center*4096+sum*edgeStrength+2048)/4096;};
			dst[x]=0xff000000u | (resolve(red,dr)<<16) | (resolve(green,dg)<<8) | resolve(blue,db);
		}
	}
	SDL_UpdateTexture(sceneTexture,&rect,resolved.data(),rect.w*sizeof(uint32_t));
}

SDL_Texture* HdRenderer::Frame()
{
	if (!sceneTexture) return nullptr;
	measureCabinet();
	const int outputWidth=std::max(1,render::DestinationRect.w);
	const float footprint=.66f*pitch/outputWidth;
	const int radius=options::Options.SmoothSilhouettes ? Clamp(int(std::lround(footprint)),1,3) : 0;
	const int strength=radius ? int(std::lround(192.f*std::min(1.f,footprint))) : 0;
	if (edgeRadius!=radius || edgeStrength!=strength)
	{
		edgeRadius=radius;
		edgeStrength=strength;
		std::fill(tiles.begin(),tiles.end(),1);
	}
	for (int ty = 0; ty < tileRows; ++ty)
	{
		auto row = tiles.data() + ty * tileCols;
		int tx = 0;
		while (tx < tileCols)
		{
			while (tx < tileCols && !row[tx])
				++tx;
			int start = tx;
			while (tx < tileCols && row[tx])
				row[tx++] = 0;
			if (tx == start) continue;
			int x = start * Tile, y = ty * Tile;
			SDL_Rect rect{x * S, y * S, (std::min(tx * Tile, screen->Width) - x) * S,
						  std::min(Tile, screen->Height - y) * S};
			refreshReflections(x, y, rect.w / S, rect.h / S);
			if (render::background_zmap)
				TableRelief::Update(std::min(render::background_zmap->Width, screen->Width), screen->Height, x, y,
									rect.w / S, rect.h / S, sceneDepth.data(), pitch, S);
			uploadSceneRect(rect);
		}
	}
	return sceneTexture;
}

void HdRenderer::Effects(const std::vector<render_sprite*>& sprites, const std::vector<render_sprite*>& balls,
						 const zmap_header_type& z, int boardWidth, const SDL_FRect& dest, float decorTime)
{
	SurfacePlacement place{dest.x, dest.y, dest.w / screen->Width, dest.h / screen->Height, 0, 0};
	if (SDL_GetRendererOutputSize(winmain::Renderer, &place.outW, &place.outH) || !place.outW || !place.outH) return;
	litPoints.clear();
	std::vector<CabinetEffects::Light> discharge;
	CabinetEffects::Draw(pb::time_now, decorTime, dest, screen->Width, screen->Height, surfaceOwner.data(), pitch, discharge);
	for (const auto& light : discharge)
	{
		litPoints.push_back({light.x, light.y, {148.f, 201.f, 255.f}, light.radius, 1.8f * light.intensity});
		place.emit(light.x, light.y, light.radius, light.radius);
	}
	for (auto sprite : sprites)
	{
		if (!sprite->Emissive || !sprite->Bmp || !sprite->ZMap || !lampVisible(*sprite, z)) continue;
		auto found = assets.find(sprite->Bmp);
		if (found == assets.end()) continue;
		auto& art = found->second;
		auto it = lamps.find(&art.current());
		if (it == lamps.end()) it = lamps.emplace(&art.current(), bakeLamp(*sprite, boardWidth, art)).first;
		auto& lamp = it->second;
		if (!lamp.texture) continue;
		float x = static_cast<float>(sprite->BmpRect.XPosition), y = static_cast<float>(sprite->BmpRect.YPosition);
		auto rect = place.rect(x - lamp.pad, y - lamp.pad, float(lamp.width), float(lamp.height));
		SDL_RenderCopyF(winmain::Renderer, lamp.texture, nullptr, &rect);
		if (lamp.cores.empty())
		{
			litPoints.push_back({x + lamp.x, y + lamp.y, lamp.color, std::max(1.8f, std::min(lamp.rx, lamp.ry)), 1.f,
								 art.group + 1});
			place.emit(x + lamp.x, y + lamp.y, lamp.rx, lamp.ry, art.group + 1);
		}
		else
			for (const auto& core : lamp.cores)
				if (core.weight > .03f)
				{
					litPoints.push_back({x + core.x, y + core.y, lamp.color});
					place.emit(x + core.x, y + core.y, core.rx, core.ry);
				}
	}
	resolveLights(boardWidth);
	lightReceivers(boardWidth, place);
	for (size_t i = 0; i < balls.size(); ++i)
		drawBall(*balls[i], i, z, boardWidth, place);
	DomeVeins::PublishHighlight(dest, place.sx, place.sy, place.outW, place.outH);
}
