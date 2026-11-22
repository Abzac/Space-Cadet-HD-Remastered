#pragma once

#include "Sound.h"
#include "StereoPeakLimiter.h"
#include "midi.h"
#include "options.h"
#include "pb.h"
#include "TBall.h"
#include "TPinballTable.h"
#include <set>

namespace audioChecks70
{
struct Meter
{
	float peakBefore{}, peak{}, rightPeak{}, stereoDifference{}, dryError{};
	float delayed[StereoPeakLimiter::DelayFrames]{};
	int cursor{};
	SDL_atomic_t frames{};
};

inline void SDLCALL postmix(void* user, Uint8* pcm, int bytes)
{
	auto& meter = *static_cast<Meter*>(user);
	auto samples = reinterpret_cast<float*>(pcm);
	const int frames = bytes / (2 * sizeof(float));
	// Run the production callback while collecting both sides of the limiter.
	for (int i = 0; i < frames; ++i)
	{
		auto frame = samples + 2 * i;
		meter.peakBefore = std::max(meter.peakBefore, std::abs(frame[0]));
		const float reference = meter.delayed[meter.cursor];
		meter.delayed[meter.cursor] = frame[0];
		meter.cursor = (meter.cursor + 1) % StereoPeakLimiter::DelayFrames;
		Sound::LimitOutput(nullptr, reinterpret_cast<Uint8*>(frame), 2 * sizeof(float));
		meter.peak = std::max(meter.peak, std::abs(frame[0]));
		meter.rightPeak = std::max(meter.rightPeak, std::abs(frame[1]));
		meter.stereoDifference = std::max(meter.stereoDifference, std::abs(frame[0] - frame[1]));
		meter.dryError = std::max(meter.dryError, std::abs(frame[0] - reference));
	}
	SDL_AtomicAdd(&meter.frames, frames);
}

inline bool checkPosition(FILE* out, int hz)
{
	struct Source : TPinballComponent
	{
		Source() : TPinballComponent(nullptr, -1, false) {}
		vector2 position{};
		vector2 get_coordinates() override { return position; }
	} source;
	const auto oldStereo = options::Options.SoundStereo.V;
	Sound::SetChannels(1);
	std::vector<float> signal(2 * hz, .25f);
	auto tone = Mix_QuickLoad_RAW(reinterpret_cast<Uint8*>(signal.data()), Uint32(signal.size() * sizeof(float)));
	if (!tone) return false;
	auto capture = [&](float x, float y, bool positional = true)
	{
		options::Options.SoundStereo = positional;
		source.position = {x,y};
		Sound::PlaySound(tone, &source, "position check");
		// Leave the single long voice playing so the next source must reuse it.
		SDL_Delay(60);
		Meter meter{};
		Mix_SetPostMix(postmix,&meter);
		const auto start=SDL_GetTicks64();
		while (SDL_AtomicGet(&meter.frames)<hz/25 && SDL_GetTicks64()-start<1500) SDL_Delay(2);
		Mix_SetPostMix(Sound::LimitOutput,nullptr);
		return vector2{meter.peak,meter.rightPeak};
	};
	const auto frontLeft = capture(.2f,.3f), frontRight = capture(.8f,.3f);
	const auto rearLeft = capture(.2f,-.3f), rearRight = capture(.8f,-.3f);
	const auto center = capture(.5f,.3f), far = capture(.5f,1.f);
	const auto dry = capture(.05f,.3f,false);
	auto near = [](float a, float b){ return std::abs(a-b) < .002f; };
	const bool mirrored = near(frontLeft.X,frontRight.Y) && near(frontLeft.Y,frontRight.X) &&
		frontLeft.X > frontLeft.Y * 1.9f && frontRight.Y > frontRight.X * 1.9f;
	const bool depth = near(frontLeft.X,rearLeft.X) && near(frontLeft.Y,rearLeft.Y) &&
		near(frontRight.X,rearRight.X) && near(frontRight.Y,rearRight.Y) &&
		near(center.X,center.Y) && near(far.X,far.Y) && far.X < .85f*center.X;
	fprintf(out,"Real positional postmix L/R: front-left %.4f/%.4f, front-right %.4f/%.4f, rear-left %.4f/%.4f, rear-right %.4f/%.4f; center near/far %.4f/%.4f; same channel unpositioned %.4f/%.4f.\n",
		frontLeft.X,frontLeft.Y,frontRight.X,frontRight.Y,rearLeft.X,rearLeft.Y,rearRight.X,rearRight.Y,center.X,far.X,dry.X,dry.Y);
	Mix_HaltChannel(0);
	Mix_FreeChunk(tone);
	options::Options.SoundStereo = oldStereo;
	return mirrored && depth && near(dry.X,.25f) && near(dry.Y,.25f);
}

inline bool checkRelease(FILE* out, int hz)
{
	StereoPeakLimiter limiter(hz);
	const int end = hz / 20;
	const int total = end + hz / 5;
	std::vector<float> samples(2 * total);
	for (int i = 0; i < total; ++i)
	{
		samples[2 * i] = i >= hz / 40 && i < end ? 3.f : .25f;
		samples[2 * i + 1] = -.5f * samples[2 * i];
	}
	limiter.Process(samples.data(), total);
	int recovered = end + StereoPeakLimiter::DelayFrames;
	while (recovered < total && samples[2 * recovered] < .24999f) ++recovered;
	float peak = 0.f, stereoError = 0.f;
	for (int i = 0; i < total; ++i)
	{
		peak = std::max(peak, std::abs(samples[2 * i]));
		stereoError = std::max(stereoError, std::abs(samples[2 * i + 1] + .5f * samples[2 * i]));
	}
	const float recoveryMs = (recovered - end - StereoPeakLimiter::DelayFrames) * 1000.f / hz;
	fprintf(out, "3.0 FS asymmetric stereo test: peak %.5f FS, stereo ratio error %.8f, recovery %.2f ms, latency %d frames.\n",
		peak, stereoError, recoveryMs, StereoPeakLimiter::DelayFrames);
	return peak <= 1.f && peak >= .99f && !stereoError && recoveryMs > 30 && recoveryMs < 85;
}

// Replay source events on the simulation clock, preserving the actual length
// of each decoded WAV. Accelerating a game test must not shorten the interval
// between sounds relative to their natural playback duration.
struct VoiceBudget
{
	explicit VoiceBudget(unsigned limit) : limit(limit) {}
	struct Voice { double end; };
	unsigned limit, cuts{};
	double cutSeconds{};
	std::vector<Voice> voices;

	void start(int begin, double duration)
	{
		const double now = begin / 120.;
		voices.erase(std::remove_if(voices.begin(), voices.end(), [now](const Voice& v) { return v.end <= now; }), voices.end());
		if (voices.size() == limit)
		{
			++cuts;
			cutSeconds += voices.front().end - now;
			voices.erase(voices.begin());
		}
		voices.push_back({now + duration});
	}
};

inline bool trace(FILE* out, int hz, int seed, bool busy)
{
	VoiceBudget unlimited(10000), eight(8), twelve(12), sixteen(16), thirtyTwo(32);
	const int bytesPerFrame = 2 * sizeof(float);
	const int bytesPerSecond = hz * bytesPerFrame;
	// Reference headroom with every source at full volume, centered.
	std::vector<float> sum(180*hz,0.f);
	std::set<Mix_Chunk*> effects;
	auto table = pb::MainTable;
	table->Message(MessageCode::Reset, 0);
	std::srand(seed);
	Mix_HaltChannel(-1);
	Uint64 last = 0;
	for (const auto& channel : Sound::Channels) last = std::max(last, channel.StartOrder);
	unsigned peak = 0, starts = 0;
	int movingTicks = 0;
	bool complete = true;
	pb::replay_level(true);
	for (int tick = 0; tick < 180 * 120; ++tick)
	{
		const auto ball = table->BallList.front();
		if (ball->ActiveFlag && ball->Speed > 3.f) ++movingTicks;
		// Compare native autoplay to the same mechanism with short bouts
		// of both flippers. Each bat cycles four times/s for 1.5 s in 9 s.
		if (busy && tick % 1080 < 180 && ball->ActiveFlag)
		{
			if (tick % 30 == 0) table->Message(MessageCode::LeftFlipperInputPressed, pb::time_next);
			if (tick % 30 == 12) table->Message(MessageCode::LeftFlipperInputReleased, pb::time_next);
			if (tick % 30 == 4) table->Message(MessageCode::RightFlipperInputPressed, pb::time_next);
			if (tick % 30 == 16) table->Message(MessageCode::RightFlipperInputReleased, pb::time_next);
		}
		pb::frame(1000.f / 120.f);
		std::vector<std::pair<Uint64, Mix_Chunk*>> started;
		for (size_t i = 0; i < Sound::Channels.size(); ++i)
			if (Sound::Channels[i].StartOrder > last)
				started.emplace_back(Sound::Channels[i].StartOrder, Mix_GetChunk(int(i)));
		std::sort(started.begin(), started.end());
		for (const auto& entry : started)
		{
			complete &= entry.first == last + 1 && entry.second != nullptr;
			last = entry.first;
			if (!entry.second) continue;
			const double duration = double(entry.second->alen) / bytesPerSecond;
			const size_t offset=size_t(tick)*hz/120;
			const auto pcm=reinterpret_cast<const float*>(entry.second->abuf);
			const size_t frames=std::min(size_t(entry.second->alen/bytesPerFrame),sum.size()-offset);
			for (size_t k=0; k<frames; ++k) sum[offset+k]+=pcm[k*2];
			for (auto budget : {&unlimited, &eight, &twelve, &sixteen, &thirtyTwo}) budget->start(tick, duration);
			peak = std::max(peak, unsigned(unlimited.voices.size()));
			++starts;
			effects.insert(entry.second);
		}
	}
	size_t over=0;
	float peakSample=0;
	for (const auto sample:sum)
	{
		if (std::abs(sample)>1.f) ++over;
		peakSample=std::max(peakSample,std::abs(sample));
	}
	fprintf(out, "%s seed %d: 180 s, %d moving ticks, %u effects (%zu different), max %u concurrent; "
		"cuts with 8/12/16/32 voices: %u/%u/%u/%u, cut tails at 8 %.2f s; complete event stream %d.\n",
		busy ? "Flipper bursts" : "Native autoplay",seed,movingTicks,starts,effects.size(),peak,
		eight.cuts,twelve.cuts,sixteen.cuts,thirtyTwo.cuts,eight.cutSeconds,complete);
	fprintf(out,"Full-volume reference sum: peak %.2f FS; %zu/%zu mono-equivalent frames exceed full scale (%.5f%%).\n",
		peakSample,over,sum.size(),100.*over/sum.size());
	return complete && movingTicks>240 && starts>30 && effects.size()>5;
}

inline bool run(FILE* out, const std::string&, bool opened)
{
	int hz{}, channels{};
	Uint16 format{};
	if (!opened || !Mix_QuerySpec(&hz,&format,&channels) || format!=AUDIO_F32SYS || channels!=2 || hz<=0) return false;
	fprintf(out,"Opened SDL mixer: %d Hz, float32, %d output channels, %d SFX voices, driver %s.\n",
		hz,channels,Mix_AllocateChannels(-1),SDL_GetCurrentAudioDriver());
	midi::music_stop();
	Mix_HaltChannel(-1);
	auto wave=Sound::LoadWaveFile(pb::make_path_name("SOUND112.WAV"));
	if (!wave) return false;
	const int originalPool=Mix_AllocateChannels(-1);
	Sound::SetChannels(32);
	Sound::SetVolume(MIX_MAX_VOLUME);
	bool ok=checkRelease(out,hz);
	for (int voices : {1,32})
	{
		// Let the two lookahead blocks and gain envelope empty between samples.
		SDL_Delay(120);
		Meter meter{};
		Mix_SetPostMix(postmix,&meter);
		for (int i=0; i<voices; ++i) ok &= Mix_PlayChannel(i,wave,-1)==i;
		const auto start=SDL_GetTicks64();
		while (SDL_AtomicGet(&meter.frames)<hz/5 && SDL_GetTicks64()-start<2000) SDL_Delay(2);
		Mix_SetPostMix(Sound::LimitOutput,nullptr);
		Mix_HaltChannel(-1);
		ok &= meter.peak>0.1f && !meter.stereoDifference && SDL_AtomicGet(&meter.frames)>=hz/5;
		if (voices==1) ok &= meter.dryError<=1.f/32768;
		else ok &= meter.peakBefore>1.f && meter.peak<=1.f;
		fprintf(out,"Real postmix, %d original SOUND112 voices: %d frames, raw/limited %.6f/%.6f FS, L/R difference %.6f, delta from delayed dry mix %.6f.\n",
			voices,SDL_AtomicGet(&meter.frames),meter.peakBefore,meter.peak,meter.stereoDifference,meter.dryError);
	}
	Sound::FreeSound(wave);
	ok &= checkPosition(out,hz);
	Sound::SetChannels(32);
	auto longWave=Sound::LoadWaveFile(pb::make_path_name("SOUND1.WAV"));
	if (!longWave) return false;
	Mix_VolumeChunk(longWave,0);
	for (int i=0; i<32; ++i) Sound::PlaySound(longWave,nullptr,"voice test");
	const bool allVoicesLive=Mix_Playing(-1)==32;
	const auto order=Sound::Channels;
	for (int i=0; i<3; ++i) Sound::PlaySound(longWave,nullptr,"voice test");
	std::vector<Uint64> old;
	for (const auto& channel:order) old.push_back(channel.StartOrder);
	std::sort(old.begin(),old.end());
	bool oldestFirst=old.size()==32;
	for (size_t i=0; oldestFirst && i<order.size(); ++i)
		oldestFirst &= (Sound::Channels[i].StartOrder!=order[i].StartOrder)==(order[i].StartOrder<=old[2]);
	fprintf(out,"32 concurrently playing SDL channels %d; same-tick starts 33-35 displace the three oldest %d.\n",
		allVoicesLive,oldestFirst);
	ok &= allVoicesLive && oldestFirst;
	Mix_HaltChannel(-1);
	Sound::FreeSound(longWave);
	for (bool busy : {false,true})
		for (int seed : {71903,71904,71905}) ok &= trace(out,hz,seed,busy);
	Mix_HaltChannel(-1);
	Sound::SetChannels(originalPool);
	Sound::SetVolume(options::Options.SoundVolume);
	return ok;
}
}
