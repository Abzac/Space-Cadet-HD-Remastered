#include "pch.h"
#include "options.h"
#include "Sound.h"
#include "maths.h"
#include "StereoPeakLimiter.h"

bool Sound::enabled_flag = false;
std::vector<ChannelInfo> Sound::Channels{};
int Sound::Volume = MIX_MAX_VOLUME;
bool Sound::MixOpen = false;
namespace
{
	Uint64 nextStartOrder = 0;
	StereoPeakLimiter outputLimiter;
	bool floatStereo = false;
}

void SDLCALL Sound::LimitOutput(void*, Uint8* pcm, int bytes)
{
	outputLimiter.Process(reinterpret_cast<float*>(pcm), bytes / (2 * sizeof(float)));
}

void Sound::Init(bool mixOpen, int channels, bool enableFlag, int volume)
{
	MixOpen = mixOpen;
	int frequency, outputs;
	Uint16 format;
	floatStereo = mixOpen && Mix_QuerySpec(&frequency, &format, &outputs) && format == AUDIO_F32SYS && outputs == 2;
	if (floatStereo)
	{
		outputLimiter = StereoPeakLimiter(frequency);
		Mix_SetPostMix(LimitOutput, nullptr);
	}
	Volume = volume;
	SetChannels(channels);
	Enable(enableFlag);
}

void Sound::Enable(bool enableFlag)
{
	enabled_flag = enableFlag;
	if (MixOpen && !enableFlag)
		Mix_HaltChannel(-1);
}

void Sound::Activate()
{
	if (MixOpen)
		Mix_Resume(-1);
}

void Sound::Deactivate()
{
	if (MixOpen)
		Mix_Pause(-1);
}

void Sound::Close()
{
	Enable(false);
	if (MixOpen)
		Mix_SetPostMix(nullptr, nullptr);
	Channels.clear();
}

void Sound::PlaySound(Mix_Chunk* wavePtr, TPinballComponent* soundSource, const char*)
{
	if (!MixOpen || !wavePtr || !enabled_flag || Channels.empty())
		return;
	auto channel = Mix_GroupAvailable(-1);
	if (channel == -1)
	{
		auto oldest = std::min_element(Channels.begin(), Channels.end(), [](const ChannelInfo& a, const ChannelInfo& b)
		{
			return a.StartOrder < b.StartOrder;
		});
		channel = static_cast<int>(std::distance(Channels.begin(), oldest));
		// Stopping releases the previous effect chain. Configure the new
		// source before its first audio callback, including on voice stealing.
		Mix_HaltChannel(channel);
	}

	auto& voice = Channels[channel];
	if (options::Options.SoundStereo)
	{
		// The listener is half a table-length above the middle of the apron.
		const auto position = soundSource ? soundSource->get_coordinates() : vector2{0.5f, 1.f};
		voice.Position = position;
		const vector3 direction{position.X - .5f, position.Y, -.5f};
		const float degrees = std::atan2(direction.X, direction.Y) * 180.f / Pi;
		const auto distance = static_cast<Uint8>(std::min(255.f, 100.f * maths::magnitude(direction)));
		if (floatStereo)
		{
			// SDL_mixer 2.8.1's float positional effect sends both sides to
			// the right. Its explicit stereo panning and distance effects
			// implement the same two-speaker projection without that inversion.
			const float angle = std::abs(degrees);
			const auto pan = static_cast<Uint8>(std::min(angle, 180.f - angle) * (255.f / 90.f));
			Mix_SetPanning(channel, degrees > 0 ? 255 - pan : 255, degrees < 0 ? 255 - pan : 255);
			Mix_SetDistance(channel, distance);
		}
		else
			Mix_SetPosition(channel, static_cast<Sint16>(degrees < 0.f ? degrees + 360.f : degrees), distance);
		voice.Positioned = true;
	}
	else if (voice.Positioned)
	{
		Mix_SetPosition(channel, 0, 0);
		voice.Positioned = false;
	}
	// Several sounds can start in the same simulation tick.
	if (Mix_PlayChannel(channel, wavePtr, 0) != -1)
		voice.StartOrder = ++nextStartOrder;
}

Mix_Chunk* Sound::LoadWaveFile(const std::string& lpName)
{
	if (!MixOpen)
		return nullptr;

	auto wavFile = fopenu(lpName.c_str(), "r");
	if (!wavFile)
		return nullptr;
	fclose(wavFile);

	return Mix_LoadWAV(lpName.c_str());
}

void Sound::FreeSound(Mix_Chunk* wave)
{
	if (MixOpen && wave)
		Mix_FreeChunk(wave);
}

void Sound::SetChannels(int channels)
{
	if (channels <= 0)
		channels = options::DefSoundChannels;

	const int allocated = MixOpen ? Mix_AllocateChannels(channels) : channels;
	Channels.resize(std::max(0, allocated));
	SetVolume(Volume);
}

void Sound::SetVolume(int volume)
{
	Volume = volume;
	if (MixOpen)
		Mix_Volume(-1, volume);
}
