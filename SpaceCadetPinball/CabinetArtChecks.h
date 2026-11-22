#pragma once

#include "GroupData.h"
#include "SpinnerTrace.h"
#include "TFlagSpinner.h"
#include "TPlunger.h"
#include "TPopupTarget.h"
#include "VisualChecks.h"
#include "loader.h"
#include "timer.h"

namespace cabinetArtChecks
{
inline SDL_Rect rect(const rectangle_type& r)
{
	return {r.XPosition, r.YPosition, r.Width, r.Height};
}

inline SDL_Rect pad(SDL_Rect r, int x, int y)
{
	return {r.x - x, r.y - y, r.w + 2 * x, r.h + 2 * y};
}

struct Mount
{
	SDL_Rect area;
	std::vector<int> reference;
	double similarity;
};

// Compare the direction and strength of the rendered edges at their original
// screen coordinates. A lamp can change exposure or add a local highlight.
// A displaced or missing post moves the actual edges of its shank and foot.
inline double compareMount(const SDL_Surface* image, SDL_Rect crop, Mount& mount)
{
	constexpr int s = HdRenderer::Scale;
	const auto p = mount.area;
	const int x0 = (p.x - crop.x) * s, y0 = (p.y - crop.y) * s;
	const int x1 = x0 + p.w * s, y1 = y0 + p.h * s;
	if (x0 < 1 || y0 < 1 || x1 >= image->w || y1 >= image->h) return 0;
	auto luma = [image](int x, int y) {
		const auto row = static_cast<const uint8_t*>(image->pixels) + y * image->pitch;
		const auto c = reinterpret_cast<const uint32_t*>(row)[x];
		return int((c >> 16) & 255) + 2 * int((c >> 8) & 255) + int(c & 255);
	};
	std::vector<int> edges;
	edges.reserve(2 * (x1 - x0) * (y1 - y0));
	for (int y = y0; y < y1; ++y)
		for (int x = x0; x < x1; ++x)
		{
			edges.push_back(luma(x + 1, y) - luma(x - 1, y));
			edges.push_back(luma(x, y + 1) - luma(x, y - 1));
		}
	if (mount.reference.empty()) mount.reference = edges;
	double dot = 0, base = 0, current = 0;
	for (size_t i = 0; i < edges.size(); ++i)
	{
		const int a = mount.reference[i], b = edges[i];
		dot += a * b;
		base += a * a;
		current += b * b;
	}
	// Require visible metal/background contrast even in the reference frame.
	const double minEnergy = 24.0 * 24.0 * edges.size();
	return base > minEnergy && current > minEnergy ? dot / std::sqrt(base * current) : 0;
}

// Hash the requested table-space regions of the saved, composited pixels.
// A blinking lamp nearby must not pass a test for a stationary or missing part.
inline bool shot(FILE* report, const std::string& prefix, const std::string& name, SDL_Rect crop,
                 const std::vector<SDL_Rect>& probes, std::vector<uint32_t>& samples,
                 std::vector<Mount>* mounts = nullptr)
{
	const auto path = prefix + name + ".bmp";
	uint32_t whole{};
	render::update();
	if (!visualChecks::capture(path, crop, whole)) return false;
	auto file = SDL_LoadBMP(path.c_str());
	if (!file) return false;
	auto image = SDL_ConvertSurfaceFormat(file, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(file);
	if (!image) return false;
	constexpr int s = HdRenderer::Scale;
	bool valid = image->w == crop.w * s && image->h == crop.h * s;
	samples.clear();
	fprintf(report, "%s: table crop (%d,%d %dx%d), frame %08x; probes", name.c_str(), crop.x, crop.y,
	        crop.w, crop.h, whole);
	for (const auto& p : probes)
	{
		uint32_t hash = 2166136261u;
		if (p.w < 1 || p.h < 1 || p.x < crop.x || p.y < crop.y ||
		    p.x + p.w > crop.x + crop.w || p.y + p.h > crop.y + crop.h)
			valid = false;
		else
		{
			auto data = static_cast<const uint8_t*>(image->pixels);
			for (int y = (p.y - crop.y) * s; y < (p.y + p.h - crop.y) * s; ++y)
				for (int x = (p.x - crop.x) * s * 4; x < (p.x + p.w - crop.x) * s * 4; ++x)
					hash = (hash ^ data[y * image->pitch + x]) * 16777619u;
		}
		samples.push_back(hash);
		fprintf(report, " %08x", hash);
	}
	if (mounts)
		for (auto& mount : *mounts)
		{
			mount.similarity = compareMount(image, crop, mount);
			fprintf(report, ", post edges %.5f", mount.similarity);
		}
	fputc('\n', report);
	SDL_FreeSurface(image);
	return valid;
}

inline void hit(TCollisionComponent* part, TBall* ball)
{
	vector2 position{(part->AABB.XMin + part->AABB.XMax) * .5f,
	                 (part->AABB.YMin + part->AABB.YMax) * .5f};
	vector2 normal{0, -1};
	ball->Direction = {0, 1, 0};
	ball->Speed = 25;
	part->Collision(ball, &position, &normal, 0, part->EdgeList.front());
}

inline void tick()
{
	++pb::time_ticks;
	timer::check();
}

inline bool spring(FILE* report, const std::string& prefix, TPlunger* part)
{
	if (!part || !part->ListBitmap || part->ListBitmap->size() != 8 || !part->RenderSprite) return false;
	const auto bounds = rect(part->RenderSprite->BoundingRect);
	const auto crop = pad(bounds, 8, 5);
	const SDL_Rect head = rect(part->RenderSprite->BmpRect);
	std::vector<uint32_t> previous;
	bool good = true;
	for (int percent : {0, 50, 100})
	{
		const float amount = percent * .01f;
		const int phase = static_cast<int>(std::round(amount * 7));
		good &= part->SetAnalogPull(amount);
		good &= part->RenderSprite->Bmp == pb::record_table->GetBitmap(473 + phase) &&
		        std::fabs(part->Boost - part->MaxPullback * amount) < .001f;
		std::vector<uint32_t> sample;
		if (!shot(report, prefix, "cabinet-spring-" + std::to_string(percent), crop, {head}, sample))
		{
			good = false;
			break;
		}
		good &= std::find(previous.begin(), previous.end(), sample[0]) == previous.end();
		previous.push_back(sample[0]);
		fprintf(report, "  plunger %d%% DAT %d, boost %.3f\n", percent, 473 + phase, part->Boost);
	}
	part->CancelAnalog();
	return good && part->RenderSprite->Bmp == pb::record_table->GetBitmap(473);
}

inline bool bank(FILE* report, const std::string& prefix, int number)
{
	auto table = pb::MainTable;
	TPopupTarget* targets[3]{};
	std::vector<SDL_Rect> boxes;
	SDL_Rect bounds{};
	const int first = (number - 1) * 3;
	for (int i = 0; i < 3; ++i)
	{
		const auto name = "a_targ" + std::to_string(first + i + 1);
		auto p = dynamic_cast<TPopupTarget*>(table->find_component(name.c_str()));
		if (!p || !p->RenderSprite || !p->ActiveFlag || p->EdgeList.empty() ||
		    p->RenderSprite->Bmp != pb::record_table->GetBitmap(432 + first + i)) return false;
		targets[i] = p;
		boxes.push_back(rect(p->RenderSprite->BmpRect));
		SDL_UnionRect(&bounds, &boxes.back(), &bounds);
	}
	const auto crop = pad(bounds, 6, 6);
	bool good = true;
	std::vector<uint32_t> previous;
	for (int fallen = 0; fallen <= 3; ++fallen)
	{
		if (fallen) hit(targets[fallen - 1], table->BallList.front());
		for (int i = 0; i < 3; ++i)
		{
			const bool up = i >= fallen;
			good &= !!targets[i]->ActiveFlag == up &&
			        targets[i]->RenderSprite->Bmp == (up ? pb::record_table->GetBitmap(432 + first + i) : nullptr);
		}
		std::vector<uint32_t> samples;
		if (!shot(report, prefix, "cabinet-bank-" + std::to_string(number) + "-up-" + std::to_string(3 - fallen),
		          crop, boxes, samples)) return false;
		if (fallen) good &= previous[fallen - 1] != samples[fallen - 1];
		previous = samples;
	}
	// The third contact arms the physical reset timer of every target. Verify
	// that none springs up early, then observe all three timer callbacks.
	const int start = pb::time_ticks;
	for (auto p : targets) good &= p->Timer != 0;
	const int delay = static_cast<int>(targets[0]->TimerTime * 1000);
	for (int i = 1; i < delay; ++i) tick();
	for (auto p : targets) good &= !p->ActiveFlag && !p->RenderSprite->Bmp;
	for (int i = 0; i < 4; ++i) tick();
	for (int i = 0; i < 3; ++i)
		good &= targets[i]->ActiveFlag && !targets[i]->Timer &&
		        targets[i]->RenderSprite->Bmp == pb::record_table->GetBitmap(432 + first + i);
	fprintf(report, "  bank %d DAT %d..%d, native reset delay %d ms, observed by %d ms\n",
	        number, 432 + first, 434 + first, delay, pb::time_ticks - start);
	return good;
}

inline bool flag(FILE* report, const std::string& prefix, int number)
{
	auto table = pb::MainTable;
	const auto name = "a_flag" + std::to_string(number);
	auto p = dynamic_cast<TFlagSpinner*>(table->find_component(name.c_str()));
	if (!p || !p->RenderSprite || !p->ListBitmap || p->ListBitmap->size() != 8 ||
	    p->EdgeList.empty() || p->BmpIndex != 0) return false;
	const auto bounds = rect(p->RenderSprite->BoundingRect);
	const auto root = rect(p->RenderSprite->BmpRect);
	const auto crop = pad(bounds, 6, 6);
	// The feet are clear of the moving blade in every phase. Include their
	// visible outline and the field on both sides in the fixed comparison.
	const SDL_Rect left = number == 1 ? SDL_Rect{root.x + 1, root.y + 12, 5, 3}
	                                  : SDL_Rect{root.x, root.y + 5, 4, 6};
	const SDL_Rect right = number == 1 ? SDL_Rect{root.x + 17, root.y + 10, 5, 4}
	                                   : SDL_Rect{root.x + 20, root.y + 16, 5, 6};
	const SDL_Rect blade{root.x + (number == 1 ? 6 : 10), root.y + (number == 1 ? 2 : 4), 7, 8};
	std::vector<Mount> mounts{{left, {}, 0}, {right, {}, 0}};
	std::vector<uint32_t> blades;
	bool good = true;
	for (int frame = 0; frame < 8; ++frame)
	{
		if (frame == 1) hit(p, table->BallList.front());
		else if (frame > 1)
			for (int ms = 0; ms < 1000 && p->BmpIndex == frame - 1; ++ms) tick();
		const int id = 481 + 8 * (number - 1) + frame;
		good &= p->BmpIndex == frame && p->RenderSprite->Bmp == pb::record_table->GetBitmap(id);
		std::vector<uint32_t> samples;
		if (!shot(report, prefix, "cabinet-flag-" + std::to_string(number) + "-frame-" + std::to_string(frame),
		          crop, {blade}, samples, &mounts)) return false;
		for (const auto& mount : mounts) good &= mount.similarity >= .97;
		// Opposite sides of the plain white paddle can give the same silhouette.
		// Every transition around a revolution still has to change the blade.
		if (frame) good &= samples[0] != blades.back();
		blades.push_back(samples[0]);
		fprintf(report, "  spinner %d native frame %d DAT %d at %d ms\n", number, p->BmpIndex, id, pb::time_ticks);
	}
	for (int ms = 0; ms < 1000 && p->BmpIndex == 7; ++ms) tick();
	return good && blades.back() != blades.front() && p->BmpIndex == 0 &&
	       p->RenderSprite->Bmp == pb::record_table->GetBitmap(481 + 8 * (number - 1));
}

inline bool smoothFlag(FILE* report, const std::string& prefix, int number)
{
	spinnerTrace::freshTable();
	auto table = pb::MainTable;
	auto p = static_cast<TFlagSpinner*>(table->find_component(("a_flag"+std::to_string(number)).c_str()));
	if (!p || p->RenderSprite->Spinner != p) return false;
	const auto crop = pad(rect(p->RenderSprite->BoundingRect), 7, 7);
	const auto r = rect(p->RenderSprite->BmpRect);
	const SDL_Rect blade{r.x + (number == 1 ? 6 : 10), r.y + (number == 1 ? 2 : 4), 7, 8};
	std::vector<Mount> mounts{
		{number == 1 ? SDL_Rect{r.x+1,r.y+12,5,3} : SDL_Rect{r.x,r.y+5,4,6}, {}, 0},
		{number == 1 ? SDL_Rect{r.x+17,r.y+10,5,4} : SDL_Rect{r.x+20,r.y+16,5,6}, {}, 0}
	};
	const bool reverse = number == 2;
	spinnerTrace::cross(p,reverse,10.f);
	bool good = true, seen[32]{};
	uint32_t bladeHashes[32]{};
	int captured = 0;
	for (int t = 0; t < 1200 && captured < 32; ++t)
	{
		render::update();
		const int phase = p->VisualPhase();
		if (phase < 0 || phase >= 32) return false;
		if (!seen[phase])
		{
			std::vector<uint32_t> samples;
			const auto name = "spinner-smooth-"+std::to_string(number)+"-"+std::to_string(phase);
			if (!shot(report,prefix,name,crop,{blade},samples,&mounts)) return false;
			bladeHashes[phase] = samples.front();
			for (const auto& mount : mounts) good &= mount.similarity >= .97;
			seen[phase] = true;
			++captured;
		}
		tick();
	}
	for (int i=0;i<32;++i)
		good &= seen[i] && bladeHashes[i]!=bladeHashes[(i+1)%32];
	// Same simulation tick, regardless of how many times the paused menu is drawn.
	render::update();
	const int paused = p->VisualPhase();
	render::update();
	good &= paused==p->VisualPhase();
	// Retire overlapping turns on reset. None may score or turn the new game.
	spinnerTrace::cross(p,!reverse,13.f);
	spinnerTrace::cross(p,reverse,9.f);
	p->Message(MessageCode::Reset,0);
	const auto resetScore = table->CurScore;
	for (int i=0;i<1200;++i) tick();
	render::update();
	good &= p->VisualPhase()==0 && p->BmpIndex==0 && resetScore==table->CurScore;
	fprintf(report,"Spinner %d: %d/32 rendered poses, direction %d, original axle alignment and reset checked.\n",
	        number,captured,reverse ? -1 : 1);
	return good;
}

inline bool recordSpin(FILE* report, const std::string& prefix, int number)
{
	spinnerTrace::freshTable();
	auto table = pb::MainTable;
	auto p = static_cast<TFlagSpinner*>(table->find_component(("a_flag"+std::to_string(number)).c_str()));
	auto edge = static_cast<TLine*>(number==1 ? p->EdgeList.front() : p->PrevCollider);
	auto ball = table->BallList.front();
	const auto normal = edge->Line.PerpendicularC;
	const float lead = ball->Radius*1.8f;
	ball->Position = {(edge->X0+edge->X1)*.5f + normal.X*lead,(edge->Y0+edge->Y1)*.5f + normal.Y*lead,ball->Radius};
	ball->Direction = {-normal.X,-normal.Y,0};
	ball->Speed = 10;
	ball->ActiveFlag = 1;
	ball->CollisionComp = nullptr;
	ball->CollisionFlag = 0;
	ball->CollisionDisabledFlag = false;
	ball->EdgeCollisionCount = 0;
	ball->CollisionMask = edge->CollisionGroup;
	const auto crop = pad(rect(p->RenderSprite->BoundingRect), 17, 15);
	bool good = true;
	int turned = 0, nativeChanges = 0, previous = 0, previousNative = 0;
	for (int i=0;i<52;++i)
	{
		pb::frame(1000.f/120); pb::frame(1000.f/120);
		turned += previous!=p->VisualPhase(); previous = p->VisualPhase();
		nativeChanges += previousNative!=p->BmpIndex; previousNative = p->BmpIndex;
		uint32_t hash;
		const auto path = prefix+"spinner-play-"+std::to_string(number)+"-"+std::to_string(i)+".bmp";
		good &= visualChecks::capture(path,crop,hash);
	}
	fprintf(report,"Live crossing of spinner %d at 60 fps: %d visual changes for %d native changes in 52 frames.\n",
	        number,turned,nativeChanges);
	return good && nativeChanges>=3 && turned>nativeChanges;
}

inline bool run(FILE* report, const std::string& prefix)
{
	auto table = pb::MainTable;
	if (!render::EnhancedGraphics || !table || table->BallList.empty()) return false;
	bool passed = true;
	auto check = [&](bool value, const char* part, int id) {
		fprintf(report, "%s  Cabinet art: %s %d\n", value ? "PASS" : "FAIL", part, id);
		fflush(report);
		passed &= value;
	};
	check(spring(report, prefix, table->Plunger), "spring", 1);
	for (int n = 1; n <= 3; ++n) check(bank(report, prefix, n), "drop-target bank", n);
	for (int n = 1; n <= 2; ++n) check(flag(report, prefix, n), "spinner", n);
	for (int n = 1; n <= 2; ++n) check(smoothFlag(report,prefix,n), "interpolated spinner", n);
	check(spinnerTrace::run(prefix+"spinner-native.csv"),"native spinner event trace",1);
	const auto sequence = SDL_getenv("SPACECADET_CAPTURE_SEQUENCE");
	if (sequence && sequence[0]=='1')
		for (int n = 1; n <= 2; ++n) check(recordSpin(report,prefix,n),"live spinner crossing",n);
	return passed;
}
}
