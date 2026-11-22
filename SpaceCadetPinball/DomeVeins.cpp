#include "pch.h"
#include "DomeVeins.h"
#include "HdRenderer.h"
#include "render.h"
#include "winmain.h"
#include <array>

namespace
{
constexpr int S = HdRenderer::Scale, PatchSize = 38 * S;
constexpr uint16_t BoardOwner = 108;
struct Point
{
	float x, y;
};
struct Site
{
	int x, y;
	float distance, coverage;
};
struct Route
{
	float length;
	int left, top;
	std::vector<Site> sites;
};
std::vector<Route> routes;
SDL_Texture* texture{};
bool bypass{}, hdrBypass{}, hasHighlight{};
Point highlight{};
constexpr float HighlightRadius = 1.95f;
EmissiveRegion highlightMask{};

// Landmarks along the irregular grooves in the original diffuser print, in
// native table coordinates. Do not rotate them into a symmetric rosette.
const std::vector<std::vector<Point>> guides = {
    {{176.62f, 276.38f},
     {175.00f, 278.12f},
     {173.62f, 280.50f},
     {171.62f, 282.75f},
     {170.12f, 284.00f},
     {167.25f, 285.75f},
     {164.75f, 287.25f},
     {163.38f, 288.88f},
     {162.50f, 290.88f},
     {161.75f, 292.50f}},
    {{183.00f, 278.12f},
     {184.62f, 280.75f},
     {186.38f, 282.75f},
     {188.38f, 285.12f},
     {187.88f, 286.88f},
     {186.38f, 288.75f},
     {186.38f, 289.88f},
     {187.62f, 292.00f},
     {186.62f, 293.62f},
     {184.88f, 294.75f},
     {184.38f, 297.25f},
     {183.25f, 299.25f}},
    {{189.25f, 272.75f},
     {192.50f, 272.88f},
     {194.38f, 274.00f},
     {197.25f, 274.38f},
     {199.75f, 275.38f},
     {201.38f, 277.25f},
     {203.38f, 279.00f},
     {204.88f, 281.38f},
     {206.00f, 284.50f},
     {207.12f, 287.25f},
     {209.12f, 289.38f}},
    {{189.62f, 266.38f},
     {192.12f, 265.12f},
     {195.50f, 264.38f},
     {199.25f, 264.62f},
     {202.00f, 264.88f},
     {204.25f, 265.75f},
     {207.00f, 267.25f},
     {209.38f, 268.25f},
     {211.62f, 268.25f},
     {214.38f, 267.75f},
     {216.62f, 268.00f}},
    {{185.75f, 261.25f},
     {187.12f, 259.00f},
     {188.12f, 257.12f},
     {189.38f, 254.75f},
     {190.50f, 252.62f},
     {192.00f, 250.25f},
     {193.00f, 249.25f}},
    {{176.88f, 261.38f},
     {174.50f, 260.88f},
     {172.75f, 259.62f},
     {170.75f, 258.50f},
     {169.00f, 257.12f},
     {168.50f, 255.75f},
     {167.75f, 254.12f},
     {168.12f, 252.75f},
     {168.62f, 251.00f}},
    {{172.50f, 261.75f},
     {170.00f, 260.88f},
     {167.12f, 260.25f},
     {164.62f, 260.00f},
     {162.12f, 259.50f},
     {159.50f, 258.62f},
     {157.38f, 257.75f},
     {156.25f, 256.50f},
     {154.62f, 255.38f},
     {152.88f, 254.50f}},
    {{172.38f, 272.25f},
     {169.25f, 272.88f},
     {167.62f, 273.50f},
     {166.50f, 274.62f},
     {165.00f, 275.50f},
     {163.50f, 276.38f},
     {162.50f, 276.50f},
     {161.12f, 276.00f},
     {159.50f, 276.50f},
     {158.38f, 277.62f},
     {157.25f, 278.88f}},
    {{191.88f, 278.25f}, {192.88f, 281.12f}, {194.12f, 283.12f}, {195.75f, 285.25f}, {197.12f, 286.12f}}};
float unit(float v)
{
	return std::max(0.f, std::min(1.f, v));
}
float ease(float v)
{
	v = unit(v);
	return v * v * (3.f - 2.f * v);
}
uint32_t texel(const SDL_Surface* board, int x, int y)
{
	const auto row = static_cast<const uint8_t*>(board->pixels) + y * board->pitch;
	return reinterpret_cast<const uint32_t*>(row)[x];
}
float green(const SDL_Surface* board, float x, float y)
{
	x = x * S - .5f;
	y = y * S - .5f;
	const int i = int(x), j = int(y);
	const float a = x - i, b = y - j;
	auto at = [&](int u, int v) { return float((texel(board, u, v) >> 8) & 255); };
	return (1 - b) * ((1 - a) * at(i, j) + a * at(i + 1, j)) + b * ((1 - a) * at(i, j + 1) + a * at(i + 1, j + 1));
}
Route registerRoute(const SDL_Surface* board, const std::vector<Point>& guide)
{
	std::vector<Point> path;
	std::vector<float> lengths{0.f};
	// Fit each landmark to the local trough between the two lips of the
	// engraving. Sampling both sides cancels the dome's broad light gradient.
	for (size_t i = 0; i < guide.size(); ++i)
	{
		const auto p = guide[i], a = guide[i ? i - 1 : 0], b = guide[std::min(i + 1, guide.size() - 1)];
		const float length = hypotf(b.x - a.x, b.y - a.y), nx = (a.y - b.y) / length, ny = (b.x - a.x) / length;
		float offset = 0, best = -1000.f;
		for (int k = -6; k <= 6; ++k)
		{
			const float u = k * .20f, x = p.x + u * nx, y = p.y + u * ny;
			const float groove =
			    .5f * (green(board, x + nx, y + ny) + green(board, x - nx, y - ny)) - green(board, x, y);
			const float score = groove - 1.75f * u * u;
			if (score > best)
			{
				best = score;
				offset = u;
			}
		}
		path.push_back({p.x + offset * nx, p.y + offset * ny});
		if (i)
			lengths.push_back(lengths.back() + hypotf(path[i].x - path[i - 1].x, path[i].y - path[i - 1].y));
	}
	float x0 = path[0].x, x1 = x0, y0 = path[0].y, y1 = y0;
	for (auto p : path)
	{
		x0 = std::min(x0, p.x);
		x1 = std::max(x1, p.x);
		y0 = std::min(y0, p.y);
		y1 = std::max(y1, p.y);
	}
	constexpr float spread = 2.1f;
	Route route{lengths.back(), int((x0-spread)*S), int((y0-spread)*S), {}};
	for (int y = route.top; y <= (y1 + spread) * S; ++y)
		for (int x = route.left; x <= (x1 + spread) * S; ++x)
		{
			const auto rgb = texel(board, x, y);
			const int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
			if (g < r + 12 || b < g + 6)
				continue;
			const float u = (x + .5f) / S, v = (y + .5f) / S;
			float best = spread * spread, travel = -1.f;
			for (size_t i = 1; i < path.size(); ++i)
			{
				const auto a = path[i - 1], end = path[i];
				const float dx = end.x - a.x, dy = end.y - a.y, len = lengths[i] - lengths[i - 1];
				const float t = unit(((u - a.x) * dx + (v - a.y) * dy) / (len * len));
				const float ex = u - a.x - t * dx, ey = v - a.y - t * dy, d = ex * ex + ey * ey;
				if (d < best)
				{
					best = d;
					travel = lengths[i - 1] + t * len;
				}
			}
			if (travel < 0)
				continue;
			const float width = .72f * expf(-best / .55f) + .28f * expf(-best / 1.7f);
			const float relief = .25f * (green(board, u - 1, v) + green(board, u + 1, v) + green(board, u, v - 1) +
			                             green(board, u, v + 1)) -
			                     g;
			route.sites.push_back({x, y, travel, width * (.78f + .22f * unit((relief + .4f) / 4.f))});
		}
	return route;
}

template <class Hidden> void coverHighlight(Hidden hidden)
{
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x)
		{
			const int u = int((highlight.x + ((x + .5f) / 8.f - 1.f) * HighlightRadius) * S);
			const int v = int((highlight.y + ((y + .5f) / 8.f - 1.f) * HighlightRadius) * S);
			if (hidden(u, v))
				highlightMask.covered[y / 2] |= uint32_t(1) << (x + 16 * (y % 2));
		}
}
} // namespace

void DomeVeins::Init(const SDL_Surface* board)
{
	for (const auto& guide : guides)
		routes.push_back(registerRoute(board, guide));
	texture = SDL_CreateTexture(winmain::Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
	                            PatchSize, PatchSize);
	if (texture)
	{
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
		SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
	}
}
void DomeVeins::Reset()
{
	if (texture)
		SDL_DestroyTexture(texture);
	texture = nullptr;
	routes.clear();
	hasHighlight = false;
	bypass = false;
	hdrBypass = false;
}
void DomeVeins::ClearHighlight()
{
	hasHighlight = false;
}
void DomeVeins::SetDiagnosticBypass(bool enabled)
{
	bypass = enabled;
}
void DomeVeins::SetDiagnosticHdrBypass(bool enabled)
{
	hdrBypass = enabled;
}
void DomeVeins::Draw(float seconds, const SDL_FRect& dest, float sx, float sy, const uint16_t* owners, int stride)
{
	if (!texture || !owners || bypass || seconds < 0)
		return;
	constexpr float interval = 1.8f;
	const unsigned slot = unsigned(seconds / interval);
	const auto& route = routes[(slot * 5u) % routes.size()];
	const float elapsed = seconds - slot * interval - .10f;
	constexpr float travelTime = .38f, decayTime = .68f;
	if (elapsed <= 0.f || elapsed >= travelTime + decayTime)
		return;
	const int left = route.left, top = route.top;
	// Light the groove progressively, then let each part discharge in place.
	// The luminous length can span the whole branch while the leading edge moves.
	float strongest = 0;
	std::array<uint32_t, PatchSize * PatchSize> rgba{};
	for (const auto& site : route.sites)
	{
		const int x = site.x - left, y = site.y - top;
		if (x < 0 || y < 0 || x >= PatchSize || y >= PatchSize || owners[site.y * stride + site.x] != BoardOwner)
			continue;
		const float age = elapsed - travelTime * site.distance / route.length;
		if (age <= 0.f || age >= decayTime)
			continue;
		const float charge = ease(age / .045f);
		const float residual = charge * ease((decayTime - age) / .54f);
		const float crest = charge * ease((.23f - age) / .15f);
		const float e = site.coverage;
		const int red = int((25 * crest + 13 * residual) * e);
		const int green = int((34 * crest + 79 * residual) * e);
		const int blue = int((25 * crest + 114 * residual) * e);
		rgba[y * PatchSize + x] = 0xff000000u | (red << 16) | (green << 8) | blue;
		const float intensity = e * (.4f * crest + .6f * residual);
		if (intensity > strongest)
		{
			strongest = intensity;
			highlight = {(site.x + .5f) / S, (site.y + .5f) / S};
		}
	}
	if (!strongest) return;
	SDL_UpdateTexture(texture, nullptr, rgba.data(), PatchSize * sizeof(uint32_t));
	const SDL_FRect out{dest.x + left * sx / S, dest.y + top * sy / S, PatchSize * sx / S, PatchSize * sy / S};
	SDL_RenderCopyF(winmain::Renderer, texture, nullptr, &out);

	// Use the cabinet's existing masked FP16 lamp pass only on the luminous
	// core. A nearby insert or the blue glass between branches cannot emit.
	if (strongest < .22f)
		return;
	hasHighlight = true;
	highlightMask = {};
	coverHighlight(
	    [&](int u, int v)
	    {
		    const int x = u - left, y = v - top;
		    return x < 0 || y < 0 || x >= PatchSize || y >= PatchSize || (rgba[y * PatchSize + x] & 255) < 26;
	    });
}
void DomeVeins::CoverWithBall(const uint32_t* pixels, int side, int left, int top)
{
	if (!hasHighlight || (highlight.x + HighlightRadius) * S <= left ||
	    (highlight.y + HighlightRadius) * S <= top || (highlight.x - HighlightRadius) * S >= left + side ||
	    (highlight.y - HighlightRadius) * S >= top + side)
		return;
	coverHighlight(
	    [&](int u, int v)
	    {
		    const int x = u - left, y = v - top;
		    return x >= 0 && y >= 0 && x < side && y < side && (pixels[y * side + x] >> 24) > 64;
	    });
}
void DomeVeins::PublishHighlight(const SDL_FRect& dest, float sx, float sy, int w, int h)
{
	if (!hasHighlight || hdrBypass)
		return;
	highlightMask.x = (dest.x + highlight.x * sx) / w;
	highlightMask.y = (dest.y + highlight.y * sy) / h;
	highlightMask.rx = HighlightRadius * sx / w;
	highlightMask.ry = HighlightRadius * sy / h;
	render::EmissiveRegions.push_back(highlightMask);
}
