#pragma once
#include "maths.h"
#include "TPinballComponent.h"

struct ChannelInfo
{
	Uint64 StartOrder{};
	vector2 Position;
	bool Positioned{};
};

class Sound
{
public:
	static std::vector<ChannelInfo> Channels;

	static void Init(bool mixOpen, int channels, bool enableFlag, int volume);
	static void Enable(bool enableFlag);
	static void Activate();
	static void Deactivate();
	static void Close();
	static void PlaySound(Mix_Chunk* wavePtr, TPinballComponent *soundSource, const char* info);
	static Mix_Chunk* LoadWaveFile(const std::string& lpName);
	static void FreeSound(Mix_Chunk* wave);
	static void SetChannels(int channels);
	static void SetVolume(int volume);
	static void SDLCALL LimitOutput(void*, Uint8* pcm, int bytes);
private:
	static bool enabled_flag;
	static int Volume;
	static bool MixOpen;
};
