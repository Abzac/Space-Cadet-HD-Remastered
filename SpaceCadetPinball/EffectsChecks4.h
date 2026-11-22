#pragma once
#include "FieldTraces.h"
#include "VisualChecks.h"

namespace effectsChecks4
{
struct Difference
{
	int pixels{};
	float cx{}, cy{};
	unsigned maximum{};
};
inline Difference difference(const std::string &first, const std::string &second, SDL_Rect area)
{
	Difference result;
	auto a = SDL_LoadBMP(first.c_str()), b = SDL_LoadBMP(second.c_str());
	if (a && b && a->w == b->w && a->h == b->h && a->format->BytesPerPixel == b->format->BytesPerPixel)
	{
		const int bytes = a->format->BytesPerPixel;
		for (int y = area.y; y < area.y + area.h; ++y)
			for (int x = area.x; x < area.x + area.w; ++x)
			{
				const auto p = static_cast<const Uint8 *>(a->pixels) + y * a->pitch + x * bytes;
				const auto q = static_cast<const Uint8 *>(b->pixels) + y * b->pitch + x * bytes;
				unsigned change = 0;
				for (int c = 0; c < 3; ++c)
					change = std::max(change, unsigned(abs(int(p[c]) - int(q[c]))));
				result.maximum = std::max(result.maximum, change);
				if (change >= 3)
				{
					++result.pixels;
					result.cx += x;
					result.cy += y;
				}
			}
		if (result.pixels)
		{
			result.cx /= result.pixels;
			result.cy /= result.pixels;
		}
	}
	SDL_FreeSurface(a);
	SDL_FreeSurface(b);
	return result;
}
// Register the actual rendered star patterns in the printed disk's plane.
// Subtract each annulus' mean so that a stationary breathing core cannot
// pass as a rotation of the arms.
inline int rotation(const std::string &before, const std::string &after, float x, float y, float rx, float ry,
					float tilt, float &confidence)
{
	constexpr int count = 180, rings = 5;
	float samples[2][rings][count]{};
	double power[2]{};
	bool valid = true;
	for (int f = 0; f < 2; ++f)
	{
		auto image = SDL_LoadBMP((f ? after : before).c_str());
		if (!image) return 0;
		for (int ring = 0; ring < rings; ++ring)
		{
			const float radius = .32f + .095f * ring;
			float mean = 0;
			for (int i = 0; i < count; ++i)
			{
				const float angle = float(2 * M_PI * i / count), u = rx * radius * cosf(angle),
							v = ry * radius * sinf(angle);
				const float px = x + u * cosf(tilt) - v * sinf(tilt) - .5f,
							py = y + u * sinf(tilt) + v * cosf(tilt) - .5f;
				const int ix = int(floorf(px)), iy = int(floorf(py));
				if (ix < 0 || iy < 0 || ix + 1 >= image->w || iy + 1 >= image->h)
				{
					valid = false;
					continue;
				}
				const float fx = px - ix, fy = py - iy;
				float l = 0;
				for (int j = 0; j < 2; ++j)
					for (int k = 0; k < 2; ++k)
					{
						const Uint8 *p = static_cast<const Uint8 *>(image->pixels) + (iy + j) * image->pitch +
										 (ix + k) * image->format->BytesPerPixel;
						l += (p[0] + p[1] + p[2]) * (k ? fx : 1 - fx) * (j ? fy : 1 - fy);
					}
				samples[f][ring][i] = l;
				mean += l;
			}
			for (auto &sample : samples[f][ring])
			{
				sample -= mean / count;
				power[f] += sample * sample;
			}
		}
		SDL_FreeSurface(image);
	}
	if (!valid || !power[0] || !power[1]) return 0;
	int best = 0;
	confidence = -1.f;
	for (int shift = -count / 2; shift < count / 2; ++shift)
	{
		double dot = 0;
		for (int ring = 0; ring < rings; ++ring)
			for (int i = 0; i < count; ++i)
				dot += samples[0][ring][(i + count - shift) % count] * samples[1][ring][i];
		const float coefficient = float(dot / sqrt(power[0] * power[1]));
		if (coefficient > confidence)
		{
			best = shift;
			confidence = coefficient;
		}
	}
	return best * 360 / count;
}
inline bool domeFloor(FILE *report, const std::string &name, SDL_Rect area)
{
	auto image=SDL_LoadBMP(name.c_str());
	if (!image) return false;
	double glow[3]{}, textureEnergy=0.;
	int counts[3]{}, textureSites=0;
	const int bytes=image->format->BytesPerPixel;
	for (int y=5;y<image->h-5;++y)
		for (int x=5;x<image->w-5;++x)
		{
			const auto p=static_cast<const Uint8*>(image->pixels)+y*image->pitch+x*bytes;
			if (p[0]<=p[2] || p[1]<p[2]+12) continue;
			const float nx=area.x+(x+.5f)/4.f-183.0f, ny=(area.y+(y+.5f)/4.f-268.f)*1.05f;
			const float radius=hypotf(nx,ny);
			const int band=radius>=6 && radius<=11 ? 0 :
				radius>=19 && radius<=24 ? 1 : radius>=31 && radius<=33 ? 2 : -1;
			if (band>=0) { glow[band]+=p[1]; ++counts[band]; }
			if (radius<12 || radius>23) continue;
			// Etched print at a fixed 1.25-native-pixel scale. Omit the
			// hardware to prevent a lens edge from registering as texture.
			const int jumps[]={5*bytes,-5*bytes,5*image->pitch,-5*image->pitch};
			int sum=0, valid=0;
			for (int len : jumps) {
				const auto q=p+len;
				if (q[0]<=q[2] || q[1]<q[2]+12) continue;
				sum+=q[1]; ++valid;
			}
			if (valid==4) {
				const auto residual=p[1]-.25*sum;
				textureEnergy+=residual*residual; ++textureSites;
			}
		}
	SDL_FreeSurface(image);
	if (!counts[0]||!counts[1]||!counts[2]||!textureSites) return false;
	for (int i=0;i<3;++i) glow[i]/=counts[i];
	const double relief=sqrt(textureEnergy/textureSites);
	fprintf(report,"Dim-phase diffuser, mean green near insert / 19..24 native pixels / outer glass: %.1f / %.1f / %.1f; local print RMS %.2f.\n",
		glow[0],glow[1],glow[2],relief);
	return glow[0]>110 && glow[1]>64 && glow[0]>glow[1]+24 && glow[1]>glow[2]+17 && relief>1.4;
}
inline bool run(FILE *report, const std::string &prefix)
{
	// Freeze one real table state while sampling its presentation at known
	// phases. Neither gameplay timers nor the scoring RNG are advanced here.
	const float savedTime = pb::time_now;
	const auto score = pb::MainTable->CurScore;
	bool good = true;
	const SDL_Rect branches{161, 151, 40, 73}, center{146, 234, 74, 70}, pair{239, 11, 46, 30};
	uint32_t dummy{}, initial{}, repeated{};
	Difference waves[2];
	for (int i = 0; i < 2; ++i)
	{
		pb::time_now = i ? 5.36f : 4.88f;
		const std::string name = prefix + "vein4-" + std::to_string(i);
		good &= visualChecks::capture(name + ".bmp", branches, initial);
		good &= visualChecks::capture(name + "-paused.bmp", branches, repeated);
		good &= initial == repeated;
		FieldTraces::SetDiagnosticBypass(true);
		good &= visualChecks::capture(name + "-base.bmp", branches, dummy);
		FieldTraces::SetDiagnosticBypass(false);
		waves[i] = difference(name + ".bmp", name + "-base.bmp", {0, 0, branches.w * 4, branches.h * 4});
		good &= waves[i].pixels > 20 && waves[i].maximum > 20 && waves[i].pixels < branches.w * branches.h;
	}
	good &= hypotf(waves[1].cx - waves[0].cx, waves[1].cy - waves[0].cy) > 6 * 4;
	fprintf(report,
			"Printed trace packet at 4.88 / 5.36 s: %d / %d bright HD texels, native centroids (%.2f,%.2f) / "
			"(%.2f,%.2f), peaks %u / %u levels. Frozen frames repeat.\n",
			waves[0].pixels, waves[1].pixels, branches.x + waves[0].cx / 4, branches.y + waves[0].cy / 4,
			branches.x + waves[1].cx / 4, branches.y + waves[1].cy / 4, waves[0].maximum, waves[1].maximum);
	pb::time_now = 0.f;
	good &= visualChecks::capture(prefix + "lens6-dim.bmp", center, initial);
	pb::time_now = 10.8f;
	good &= visualChecks::capture(prefix + "lens6-cycle.bmp", center, repeated);
	good &= initial == repeated;
	pb::time_now = 5.4f;
	good &= visualChecks::capture(prefix + "lens4-lit.bmp", center, dummy);
	FieldTraces::SetDiagnosticBypass(true);
	good &= visualChecks::capture(prefix + "lens4-base.bmp", center, dummy);
	FieldTraces::SetDiagnosticBypass(false);
	auto lens =
		difference(prefix + "lens4-lit.bmp", prefix + "lens4-base.bmp", {0, 0, center.w * 4, center.h * 4});
	const auto breath =
		difference(prefix + "lens6-dim.bmp", prefix + "lens4-lit.bmp", {0, 0, center.w * 4, center.h * 4});
	good &= lens.pixels > 1000 && lens.maximum >= 38 && lens.maximum <= 78;
	good &= breath.maximum >= 42 && breath.maximum <= 82 && breath.pixels > 12000;
	good &= domeFloor(report,prefix+"lens6-dim.bmp",center);
	good &= difference(prefix+"lens6-dim.bmp",prefix+"lens4-lit.bmp",
		{(181-center.x)*4,(266-center.y)*4,4*4,4*4}).maximum==0;
	fprintf(report,
			"Central dome, 10.8 s breathing cycle: peak above print %u levels, trough-to-peak %u over %d HD "
			"texels.\n",
			lens.maximum, breath.maximum, breath.pixels);
	for (int i = 0; i < 2; ++i)
	{
		pb::time_now = i ? 7.f : 0.f;
		good &= visualChecks::capture(prefix + "galaxy4-pair-" + std::to_string(i) + ".bmp", pair, dummy);
	}
	const auto before = prefix + "galaxy4-pair-0.bmp", after = prefix + "galaxy4-pair-1.bmp";
	const auto primary = difference(before, after, {(251 - pair.x) * 4, (17 - pair.y) * 4, 21 * 4, 11 * 4});
	const auto satellite = difference(before, after, {(270 - pair.x) * 4, (30 - pair.y) * 4, 9 * 4, 7 * 4});
	const auto edge = difference(before, after, {0, 0, 46 * 4, 4});
	good &= primary.pixels > 100 && satellite.pixels > 80 && edge.maximum == 0;
	float c0 = 0, c1 = 0;
	const int r0 =
		rotation(before, after, (262.25f - pair.x) * 4, (22.1f - pair.y) * 4, 17.4f * 4, 10.05f * 4, -.112f, c0);
	const int r1 =
		rotation(before, after, (274.38f - pair.x) * 4, (33.35f - pair.y) * 4, 8.1f * 4, 5.7f * 4, 0, c1);
	good &= abs(r0-20)<=2 && abs(r1+34)<=2 && c0 > .90f && c1 > .90f;
	fprintf(
		report,
		"Registered rotation of the two printed star patterns: %+d / %+d degrees; correlations %.3f / %.3f.\n",
		r0, r1, c0, c1);
	fprintf(report,
			"Upper galaxy pair: independent primary/satellite patches changed by 7 s (%d/%d texels); rail delta "
			"%u.\n",
			primary.pixels, satellite.pixels, edge.maximum);
	const SDL_Rect minis[]={{200,10,22,14},{63,46,25,22}};
	for (int i = 0; i < 2; ++i)
	{
		const auto &area = minis[i];
		const auto stem = prefix + "mini6-" + std::to_string(i);
		pb::time_now = 0;
		good &= visualChecks::capture(stem + "-0.bmp", area, dummy);
		pb::time_now = 7;
		good &= visualChecks::capture(stem + "-7.bmp", area, dummy);
		float correlation = 0;
		const int angle = rotation(stem + "-0.bmp", stem + "-7.bmp", ((i ? 76.12f : 211.2f) - area.x) * 4,
								   ((i ? 54.65f : 15.92f) - area.y) * 4, (i ? 6.38f : 5.65f) * 4,
								   (i ? 4.52f : 3.27f) * 4, i ? -.37f : .13f, correlation);
		good &= abs(angle - (i ? -26 : 35)) <= 4 && correlation > .77f;
		if (i)
		{
			// The red rubber slopes across the upper-right corner of the sky patch.
			// At x=82 on row 52, the print still contains moving sky below its edge.
			const SDL_Rect guard[]={{64,50,4,11},{78,58,5,1},{80,59,4,1},{81,50,2,1},{83,52,1,1}};
			unsigned delta=0;
			for (const auto& part : guard)
				delta=std::max(delta,difference(stem+"-0.bmp",stem+"-7.bmp",
					{(part.x-area.x)*4,(part.y-area.y)*4,part.w*4,part.h*4}).maximum);
			good &= !delta;
			fprintf(report,"Left galaxy, side frame / unlit arrow / rubber guard: %u maximum RGB change.\n",delta);
		}
		fprintf(report, "Miniature galaxy %d: %+d degrees in seven seconds, correlation %.3f.\n", i, angle,
				correlation);
	}
	const SDL_Rect accents[] = {{260, 151, 50, 46}, {386 + 175, 12 + 61, 19, 29}, {386 + 9, 12 + 9, 183, 95}};
	const float times[] = {34.44f, 7.69f, 63.83f};
	for (int i = 0; i < 3; ++i)
	{
		const auto stem = prefix + "accent6-" + std::to_string(i);
		pb::time_now = times[i];
		good &= visualChecks::capture(stem + "-on.bmp", accents[i], initial);
		good &= visualChecks::capture(stem + "-paused.bmp", accents[i], repeated);
		good &= initial == repeated;
		CabinetEffects::SetCelestialAccentDiagnosticBypass(true);
		good &= visualChecks::capture(stem + "-off.bmp", accents[i], dummy);
		CabinetEffects::SetCelestialAccentDiagnosticBypass(false);
		const auto change =
			difference(stem + "-on.bmp", stem + "-off.bmp", {0, 0, accents[i].w * 4, accents[i].h * 4});
		good &= change.maximum >= 16 && change.maximum <= 170 && change.pixels >= 7 &&
				change.pixels < (i == 0 ? 600 : 11000);
		fprintf(report, "Celestial accent %d at %.2f s: %d HD texels, peak %u.\n", i, times[i], change.pixels,
				change.maximum);
		if (i == 2)
			good &=
				difference(stem + "-on.bmp", stem + "-off.bmp", {(125 - 9) * 4, (61 - 9) * 4, 27 * 4, 37 * 4})
					.maximum == 0;
	}
	pb::time_now = 2;
	good &= visualChecks::capture(prefix + "poster6-quiet.bmp", accents[2], initial);
	CabinetEffects::SetCelestialAccentDiagnosticBypass(true);
	good &= visualChecks::capture(prefix + "poster6-quiet-base.bmp", accents[2], repeated);
	CabinetEffects::SetCelestialAccentDiagnosticBypass(false);
	good &= initial == repeated;
	if (SDL_GetHintBoolean("SPACECADET_CAPTURE_HD6_SKY", SDL_FALSE))
		for (int i = 0; i < 21; ++i)
		{
			const auto frame = std::to_string(100 + i) + ".bmp";
			pb::time_now = i * .54f;
			good &= visualChecks::capture(prefix + "disk6-" + frame, {124, 213, 117, 108}, dummy);
			for (int part = 0; part < 3; ++part)
			{
				pb::time_now = (part == 0 ? 33.9f : part == 1 ? 6.9f : 62.9f) + i * .1f;
				const SDL_Rect crop = part ? SDL_Rect{394, 20, 185, 165} : accents[0];
				good &= visualChecks::capture(prefix + "sky6-" + std::to_string(part) + "-" + frame, crop, dummy);
			}
		}
	if (const auto sequence = SDL_getenv("SPACECADET_CAPTURE_SEQUENCE"))
		if (sequence[0] == '1')
			for (int i = 0; i < 32; ++i)
			{
				pb::time_now = 4.60f + i / 24.f;
				good &= visualChecks::capture(prefix + "vein4-seq-" + std::to_string(i) + ".bmp",
											  {151, 145, 75, 85}, dummy);
				pb::time_now = i * .20f;
				good &= visualChecks::capture(prefix + "galaxy4-seq-" + std::to_string(i) + ".bmp", pair, dummy);
			}
	pb::time_now = savedTime;
	return good && score == pb::MainTable->CurScore;
}
} // namespace effectsChecks4
