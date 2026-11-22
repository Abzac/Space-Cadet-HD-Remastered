#pragma once

#include <algorithm>
#include <cmath>

// Two short blocks give one complete block of lookahead for the gain ramp.
// The same gain on both channels preserves the stereo image.
class StereoPeakLimiter
{
public:
	static constexpr int BlockFrames = 32;
	static constexpr int DelayFrames = 2 * BlockFrames;
	explicit StereoPeakLimiter(int sampleRate = 44100)
		: releasePerBlock(BlockFrames / (.080f * sampleRate)) {}

	void Process(float* pcm, int frames)
	{
		for (int i = 0; i < frames; ++i, pcm += 2)
		{
			auto& previous = delayed[cursor];
			const auto left = pcm[0], right = pcm[1];
			inputPeak = std::max(inputPeak, std::max(std::abs(left), std::abs(right)));
			pcm[0] = previous.left * gain;
			pcm[1] = previous.right * gain;
			previous = {left, right};
			gain += gainStep;
			cursor = (cursor + 1) % DelayFrames;
			if (cursor % BlockFrames == 0)
			{
				const float nextCeiling = inputPeak > 1.f ? .9999f / inputPeak : 1.f;
				gain = target;
				target = std::min({gain + releasePerBlock, currentCeiling, nextCeiling});
				gainStep = (target - gain) / BlockFrames;
				currentCeiling = nextCeiling;
				inputPeak = 0.f;
			}
		}
	}

private:
	struct Frame { float left{}, right{}; };
	Frame delayed[DelayFrames]{};
	int cursor{};
	float releasePerBlock, inputPeak{}, currentCeiling{1.f}, gain{1.f}, target{1.f}, gainStep{};
};
