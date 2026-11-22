#include "pch.h"
#include "FieldTraces.h"
#include "DomeVeins.h"
#include "HdRenderer.h"
#include "TPinballTable.h"
#include "control.h"
#include "pb.h"
#include "winmain.h"

namespace
{
constexpr int S = HdRenderer::Scale, PatchSize = 40 * S;
constexpr uint16_t Board = 108;
struct Point
{
	float x, y;
};
struct Site
{
	uint16_t x, y;
	float distance, ink;
};
struct Trace
{
	std::vector<Site> sites;
	float length;
};
std::vector<Trace> traces;
SDL_Texture *patch{};
SDL_Texture *lens{};
bool diagnosticBypass{};
struct LensSite
{
	uint16_t x, y;
	uint8_t alpha;
	uint32_t dim, lit;
};
std::vector<LensSite> lensSites;
std::vector<uint32_t> lensPixels;
constexpr int LensX = 154, LensY = 243, LensW = 56, LensH = 48;
constexpr SDL_Rect Stars{115, 309, 136, 112};
struct StripSite
{
	uint32_t index, emission;
	uint8_t circuit;
};
SDL_Texture *starStrips{};
std::vector<StripSite> stripSites;
std::vector<uint32_t> stripPixels;

// Center lines of branches in the actual artwork, in native table units.
// The positions localize travel; the ink in the texture supplies every contour.
const std::vector<std::vector<Point>> branches = {
	{{179, 219},
	 {176.5, 214.5},
	 {169.5, 211},
	 {168.5, 203.5},
	 {172, 193},
	 {176.5, 188.5},
	 {183, 184.5},
	 {189, 178},
	 {194, 175},
	 {194.5, 171},
	 {191, 163.5},
	 {190.5, 155},
	 {192, 151.5}},
	{{211.5, 223}, {205, 214}, {204, 207}, {198.5, 200}, {194, 194.5}, {191.5, 193}, {178, 192}, {172, 194}},
	{{214.5, 224.5}, {219.5, 221.5}, {217.5, 215.5}, {222.5, 210}, {227.5, 208}, {229.5, 205}},
	{{237, 257}, {244, 258}, {248.5, 249}, {249, 245}, {248, 239}, {244, 229.5}, {237, 224}, {235.5, 220}},
	{{232, 283}, {234, 279}, {239, 279}, {244, 279}, {251.5, 267.5}, {258.5, 273}},
	{{207.5, 311.5}, {216, 319}, {220, 321}, {225, 321.5}, {231, 324}, {237.5, 327.5}},
	{{195, 315},
	 {195.5, 319},
	 {198, 324},
	 {195, 331},
	 {192, 333},
	 {186.5, 333.5},
	 {183.5, 335},
	 {175.5, 342},
	 {169, 343.5},
	 {166, 346.5}},
	{{166, 315}, {165.5, 321}, {163, 330}, {167.5, 335.5}, {169, 344}},
	{{139, 301}, {132, 308.5}, {130, 310.5}, {126, 314}, {119, 313}},
	{{131, 282}, {127, 285}, {123, 286.5}, {120.5, 286.5}, {116, 284.5}, {111.5, 285.5}},
	{{129, 249.5}, {126, 248}, {121, 243}, {119.5, 239.5}, {123.5, 230.5}, {123.5, 226.5}, {122.5, 225}},
	{{153, 228}, {150, 226}, {150, 220}, {144, 214}, {137, 213}, {134, 214}},
	{{192, 117},
	 {192, 111},
	 {188, 110},
	 {188, 106},
	 {182, 104},
	 {178.5, 101.5},
	 {176, 99},
	 {176, 95},
	 {170, 90.5},
	 {176, 85.5},
	 {176.5, 80.5},
	 {180, 77.5},
	 {183, 76.5}},
	{{102, 130},
	 {108.5, 124.5},
	 {108.5, 121.5},
	 {107.5, 120.5},
	 {108.5, 117},
	 {116, 109.5},
	 {115.5, 106.5},
	 {111, 99.5},
	 {107, 95.5},
	 {103.5, 97}},
	{{127, 105}, {120, 104}, {119, 99}, {124, 94}, {132.5, 90}},
	{{204, 101}, {195.5, 93.5}, {192.5, 88.5}, {192.5, 81}, {187.5, 71.5}}};
float unit(float v)
{
	return std::max(0.f, std::min(1.f, v));
}
float smooth(float v)
{
	v = unit(v);
	return v * v * (3.f - 2.f * v);
}
uint32_t color(const SDL_Surface *surface, int x, int y)
{
	const auto row =
		reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(surface->pixels) + y * surface->pitch);
	return row[x];
}
float blueInk(uint32_t c)
{
	const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
	return smooth((g - r - 13.f) / 46.f) * smooth((b - r - 26.f) / 51.f) * smooth((g - 29.f) / 55.f);
}
Trace registerTrace(const SDL_Surface *board, const std::vector<Point> &path)
{
	const float margin = 2.3f;
	std::vector<float> arc(path.size());
	float x0 = path[0].x, y0 = path[0].y, x1 = x0, y1 = y0;
	for (size_t i = 1; i < path.size(); ++i)
	{
		const auto p = path[i];
		arc[i] = arc[i - 1] + hypotf(p.x - path[i - 1].x, p.y - path[i - 1].y);
		x0 = std::min(x0, p.x);
		x1 = std::max(x1, p.x);
		y0 = std::min(y0, p.y);
		y1 = std::max(y1, p.y);
	}
	Trace trace;
	trace.length = arc.back();
	for (int y = int((y0 - margin) * S); y < int((y1 + margin) * S) + 1; ++y)
		for (int x = int((x0 - margin) * S); x < int((x1 + margin) * S) + 1; ++x)
		{
			const float ink = blueInk(color(board, x, y));
			if (ink < .015f) continue;
			const float u = (x + .5f) / S, v = (y + .5f) / S;
			// The pair of straight blue-edged rails are in the static painting.
			// Their highlights have the same hue as the branching floor ink.
			if (v > 228.f && (fabsf(u - (119.4f + .391f * (v - 280.f))) < 1.65f ||
							  fabsf(u - (245.6f - .405f * (v - 280.f))) < 1.65f))
				continue;
			float best = margin * margin, travel = -1.f;
			for (size_t i = 1; i < path.size(); ++i)
			{
				const Point a = path[i - 1], b = path[i];
				const float dx = b.x - a.x, dy = b.y - a.y, length = arc[i] - arc[i - 1];
				const float t = unit(((u - a.x) * dx + (v - a.y) * dy) / (length * length));
				const float ex = u - a.x - t * dx, ey = v - a.y - t * dy, d = ex * ex + ey * ey;
				if (d >= best) continue;
				best = d;
				travel = arc[i - 1] + t * length;
			}
			if (travel >= 0.f)
				trace.sites.push_back(
					{uint16_t(x), uint16_t(y), travel, ink * smooth(1.f - best / (margin * margin))});
		}
	return trace;
}
SDL_Texture *makeTexture(int w, int h)
{
	auto texture =
		SDL_CreateTexture(winmain::Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
	if (texture)
	{
		SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
	}
	return texture;
}

void registerStarStrips(const SDL_Surface *board)
{
	const int width = Stars.w * S, height = Stars.h * S;
	starStrips = makeTexture(width, height);
	if (!starStrips) return;
	stripPixels.resize(width * height);
	// The ink defines the full ribbon, including tapered tips. The lit
	// material keeps its printed grain and has a bounded surface color.
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			const auto c = color(board, Stars.x*S+x, Stars.y*S+y);
			const float r = float((c >> 16) & 255), g = float((c >> 8) & 255), b = float(c & 255);
			const float pink = smooth((r-g-34)/35)*smooth((62-g)/16)*smooth((r-b+16)/21)*
				smooth((71-r+b)/22)*smooth((b-36)/34);
			const float violet = smooth((b-g-85)/56)*smooth((112-r)/24)*smooth((b-r-69)/50)*
				smooth((r-g+11)/21);
			const bool isViolet = violet > pink;
			const float coverage = isViolet ? violet : pink;
			if (coverage < .035f) continue;
			const float grain = isViolet ? .89f+.11f*smooth((b-155)/70) :
										   .89f+.11f*smooth((r-86)/76);
			auto emission = [&](int top, float printed) {
				return uint32_t(coverage * std::max(0.f, grain*top - printed) + .5f);
			};
			const uint32_t rgb = (emission(isViolet ? 165 : 250,r)<<16) |
				(emission(isViolet ? 130 : 122,g)<<8) | emission(isViolet ? 251 : 227,b);
			stripSites.push_back({uint32_t(y*width+x), rgb, uint8_t(isViolet)});
		}
}

void drawStarStrips(float seconds, const SDL_FRect &dest, float sx, float sy, const uint16_t *owners, int stride, int height)
{
	if (!starStrips || !owners || !control::AwaitingManualDeployment()) return;
	// The decorative clock continues across the start show, feed delay and launch.
	const bool pinkOn = fmodf(seconds, 1.2f) < .6f;
	uint32_t alpha[] = {pinkOn ? 0xff000000u : 0u, pinkOn ? 0u : 0xff000000u};
	if (SDL_GetHintBoolean("SPACECADET_BREATHE_STAR_OUTLINES", SDL_FALSE))
	{
		auto pulse = [](float t) { return uint32_t(127.5f*(1.f-cosf(t*(2.f*float(M_PI)/1.6f)))+.5f)<<24; };
		alpha[0] = pulse(seconds);
		alpha[1] = pulse(seconds-.2f);
	}
	const int width = Stars.w*S;
	for (const auto &site : stripSites)
	{
		const int x = Stars.x*S + site.index % width, y = Stars.y*S + site.index / width;
		// The print is 470 units tall; the stock viewport ends at 416.
		stripPixels[site.index] = x < stride && y < height*S && owners[y*stride+x] == Board ?
			alpha[site.circuit] | site.emission : 0;
	}
	SDL_UpdateTexture(starStrips, nullptr, stripPixels.data(), width*sizeof(uint32_t));
	const SDL_FRect out{dest.x+Stars.x*sx, dest.y+Stars.y*sy, Stars.w*sx, Stars.h*sy};
	SDL_RenderCopyF(winmain::Renderer, starStrips, nullptr, &out);
}
uint32_t randomBits(uint32_t i)
{
	i ^= i >> 16;
	i *= 0x7feb352d;
	i ^= i >> 15;
	i *= 0x846ca68b;
	return i ^ (i >> 16);
}

void drawPacket(int event, float time, const SDL_FRect &dest, float sx, float sy, const uint16_t *owners,
				int stride)
{
	if (event < 0 || !patch) return;
	const uint32_t variation = randomBits(unsigned(event) + 127u);
	// A shuffled 16-route cycle visits even the short peripheral branches.
	const auto &trace = traces[(unsigned(event) * 5u + (randomBits(event / 16 + 4u) & 15)) % traces.size()];
	const float onset = 1.47f * event + .35f * ((variation & 255) / 255.f);
	const float speed = 55.f + 17.f * ((variation >> 8) & 255) / 255.f;
	const float head = (time - onset) * speed;
	if (head <= 0 || head >= trace.length + 17) return;
	const float envelope = smooth(head / 4.5f) * smooth((trace.length + 17 - head) / 12.f);
	const bool inward = (variation >> 28) == 0;
	int x0 = 32767, y0 = 32767, x1 = -1, y1 = -1;
	// Scan just one thin registered branch. Upload only the moving packet's
	// tightly bounded subrectangle, not an entire transparent playfield layer.
	for (const auto &site : trace.sites)
	{
		const float trail = head - (inward ? trace.length - site.distance : site.distance);
		if (trail < -4 || trail > 17 || owners[site.y * stride + site.x] != Board) continue;
		x0 = std::min(x0, int(site.x));
		y0 = std::min(y0, int(site.y));
		x1 = std::max(x1, int(site.x));
		y1 = std::max(y1, int(site.y));
	}
	if (x1 < x0) return;
	--x0;
	--y0;
	++x1;
	++y1;
	SDL_Rect src{0, 0, x1 - x0 + 1, y1 - y0 + 1};
	if (src.w > PatchSize || src.h > PatchSize) return;
	void *raw;
	int pitch;
	if (SDL_LockTexture(patch, &src, &raw, &pitch) != 0) return;
	for (int y = 0; y < src.h; ++y)
		memset(static_cast<uint8_t *>(raw) + y * pitch, 0, src.w * 4);
	for (const auto &site : trace.sites)
	{
		if (site.x < x0 || site.y < y0 || site.x > x1 || site.y > y1 || owners[site.y * stride + site.x] != Board)
			continue;
		const float d = head - (inward ? trace.length - site.distance : site.distance);
		if (d < -4 || d > 17) continue;
		const float front = expf(-.20f * d * d),
					tail = d >= 0 ? .48f * expf(-.15f * d) * smooth((17.f - d) / 5.f) : 0.f;
		const float weight = envelope * site.ink;
		const int r = int((61 * front + 14 * tail) * weight), g = int((100 * front + 65 * tail) * weight),
				  b = int((105 * front + 96 * tail) * weight);
		auto row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(raw) + (site.y - y0) * pitch);
		row[site.x - x0] = 0xff000000u | (r << 16) | (g << 8) | b;
	}
	SDL_UnlockTexture(patch);
	const SDL_FRect out{dest.x + x0 * sx / S, dest.y + y0 * sy / S, src.w * sx / S, src.h * sy / S};
	SDL_RenderCopyF(winmain::Renderer, patch, &src, &out);
}
} // namespace

void FieldTraces::Init(const SDL_Surface *board)
{
	Reset();
	if (!board || board->w != 365 * S || board->h != 470 * S) return;
	for (const auto &path : branches)
		traces.push_back(registerTrace(board, path));
	patch = makeTexture(PatchSize, PatchSize);
	registerStarStrips(board);
	DomeVeins::Init(board);
	// Use the same 4x print texel for glass color and optical coverage. The
	// fine boundary against the opaque center insert must stay registered.
	lens = makeTexture(LensW * S, LensH * S);
	if (lens) SDL_SetTextureBlendMode(lens, SDL_BLENDMODE_BLEND);
	lensPixels.resize(LensW * S * LensH * S);
	for (int y = 0; y < LensH * S; ++y)
		for (int x = 0; x < LensW * S; ++x)
		{
			const float u = 2.f*(x+.5f)/(LensW*S)-1.f, v = 2.f*(y+.5f)/(LensH*S)-1.f;
			const auto glass = color(board,LensX*S+x,LensY*S+y);
			const float ink = blueInk(glass);
			const int alpha = int(255 * smooth(1.f - u * u - v * v) * ink);
			if (!alpha) continue;
			auto exposure = [&](float r, float g, float b, int er, int eg, int eb) {
				auto c = [&](int shift, float gain, int emission) {
					return std::min(255u, unsigned(((glass >> shift) & 255) * gain + emission + .5f));
				};
				return (c(16, r, er) << 16) | (c(8, g, eg) << 8) | c(0, b, eb);
			};
			lensSites.push_back({uint16_t(x), uint16_t(y), uint8_t(alpha),
								 exposure(.96f, .96f, .97f, 1, 2, 3),
								 exposure(1.19f, 1.29f, 1.22f, 9, 15, 17)});
		}
}

void FieldTraces::Draw(float seconds, const SDL_FRect &dest, int width, int height, const uint16_t *owners,
					   int stride)
{
	DomeVeins::ClearHighlight();
	if (traces.empty() || diagnosticBypass) return;
	const float sx = dest.w / width, sy = dest.h / height;
	drawStarStrips(seconds, dest, sx, sy, owners, stride, height);
	if (lens)
	{
        // Keep the diffuser's etched texture and central halo lit at minimum.
        // Only the small additional exposure breathes above its idle level.
		const float breath = .5f - .5f * cosf(seconds * (2.f * float(M_PI) / 10.8f));
		const unsigned weight = unsigned(256.f * breath + .5f);
		for (const auto &site : lensSites)
		{
			const int y = LensY * S + site.y, x = LensX * S + site.x;
			const uint32_t alpha = owners[y * stride + x] == Board ? site.alpha : 0;
			auto channel = [&](int shift) {
				const auto dim = (site.dim >> shift) & 255, lit = (site.lit >> shift) & 255;
				return ((256 - weight) * dim + weight * lit + 128) >> 8;
			};
			lensPixels[site.y * LensW * S + site.x] =
				(alpha << 24) | (channel(16) << 16) | (channel(8) << 8) | channel(0);
		}
		SDL_UpdateTexture(lens, nullptr, lensPixels.data(), LensW * S * 4);
		const SDL_FRect out{dest.x + LensX * sx, dest.y + LensY * sy, LensW * sx, LensH * sy};
		SDL_RenderCopyF(winmain::Renderer, lens, nullptr, &out);
	}
	DomeVeins::Draw(seconds, dest, sx, sy, owners, stride);
	const int slot = int(floorf(seconds / 1.47f));
	drawPacket(slot - 1, seconds, dest, sx, sy, owners, stride);
	drawPacket(slot, seconds, dest, sx, sy, owners, stride);
}
void FieldTraces::Reset()
{
	DomeVeins::Reset();
	if (patch) SDL_DestroyTexture(patch);
	if (lens) SDL_DestroyTexture(lens);
	if (starStrips) SDL_DestroyTexture(starStrips);
	starStrips = nullptr;
	stripSites.clear();
	stripPixels.clear();
	patch = lens = nullptr;
	traces.clear();
	lensSites.clear();
	lensPixels.clear();
	diagnosticBypass = false;
}
void FieldTraces::SetDiagnosticBypass(bool bypass)
{
	diagnosticBypass = bypass;
}
