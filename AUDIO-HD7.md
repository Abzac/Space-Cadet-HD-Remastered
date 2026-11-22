# Space Cadet HD 7.0 audio

The mixer plays up to 32 simultaneous effects by default and outputs stereo. All 60 WAV files matched the installed Space Cadet originals byte for byte. SDL receives the original mono, unsigned 8-bit PCM at 11,025 Hz. Effects range from 58 ms to 5.02 seconds. Music and effects have separate volume controls.

## What you can hear

When flippers click and bumpers fire over a bonus cue, more sounds play to completion. Once all 32 voices are busy, the next effect replaces the oldest. Events that start on the same game tick retain their playback order.

In 6.3.2, the mixer summed decoded hits as 16-bit integers, so loud combinations could clip during mixing. In 7.0, effects and software-decoded music are summed in float32. A limiter applies the same gain to both channels when the signal exceeds full scale. It looks ahead 64 stereo frames and releases the gain reduction smoothly. This adds 1.45 ms at 44.1 kHz. The requested device block size remains 1,024 frames. If the float device cannot be opened, the game tries 16-bit stereo.

A single SOUND112 hit peaked at 0.164107 of full scale both before and after limiting. After compensating for the delay, every sample matched. In a stress test, 32 simultaneous copies of the same WAV peaked at 5.251430 of full scale at the limiter input and 0.999900 at the output. In a separate test with a pulse of amplitude 3.0, gain returned to unity in 53.76 ms, preserving the left-to-right level ratio.

Mono effects normally play in the center. With `Stereo Sound Effects` enabled in the profile, distance from the player sets the level and source position sets the stereo balance. In a float32 test of the bundled SDL_mixer 2.8.1, `Mix_SetPosition` placed the louder channel on the right for the same WAV at both +60° and −60°. S16 panned correctly. For float32, 7.0 sets the left and right gains and the distance separately before the first sample of a new voice. [SDL_mixer panning](https://wiki.libsdl.org/SDL2_mixer/Mix_SetPanning) · [SDL_mixer distance](https://wiki.libsdl.org/SDL2_mixer/Mix_SetDistance)

The built-in test fed identical left and right signals at 0.25 into the engine. The front-left source produced levels of 0.1863 / 0.0935 and the front-right source produced 0.0935 / 0.1863. Mirrored rear positions gave the corresponding stereo balance. Moving a centered source farther away reduced its level from 0.1931 to 0.1412. After positioning was disabled and the same busy voice was reused, both outputs returned to exactly 0.25.

The original port's effects path showed no added echo or room reverb. Overlaps and tails recorded in the WAV files play as recorded. The build uses SDL 2.32.10 and SDL_mixer 2.8.1.

## Table workload

Each scenario combines three automated 180-second games with seeds 71903, 71904 and 71905. The second scenario added bursts of four strokes per second on both flippers for 1.5 seconds every 9 seconds. Each WAV onset was logged with its game tick and actual decoded duration. Voice overlap and sample sums were reconstructed on the game's 120 Hz clock, avoiding the false overload that occurs when physics runs faster than the audio device clock.

| Scenario | Effects started | Peak simultaneous effects | Voices replaced at limits of 8 / 12 / 16 / 32 |
| --- | ---: | ---: | ---: |
| Normal demo play | 934 | 11 | 16 / 0 / 0 / 0 |
| Demo play with flipper bursts | 2,217 | 13 | 31 / 3 / 0 / 0 |
| Total over 18 minutes of play | 3,151 | 13 | 47 / 3 / 0 / 0 |

With every source centered at full volume, calculated peaks for individual games ranged from 3.04 to 3.55 times full scale. Without limiting, 0.35% to 0.68% of frames would have clipped. These figures assume fixed volume with music off. A separate test of the actual SDL audio callback confirmed 32-voice playback, peak limiting and replacement of the oldest sounds when all voices were occupied. Human play may produce more simultaneous events than this sample.

## Music and profile migration

PINBALL.MID is the original type 1 Standard MIDI File, with five tracks and 480 ticks per quarter note. PINBALL2.MID contains a bitmap font. For normal playback of the original MIDI on Windows, the game can use the WinMM system synthesizer, which has a separate output from SDL. The MIDI player controls its volume. Ogg music shares the stereo mixer with the effects.

The game remembers the voice limit. On first launch, a 6.x profile with the old default of 8 migrates to 32. Other saved values, such as 10, carry over. The old profile format recorded a manually selected 8 exactly like the default. After migration, explicitly saved values, including 8, persist across launches.

## Reproducing the tests

Run the commands below in PowerShell from the distribution directory. Use an empty, writable test profile directory. Both test devices are virtual. Verification disables WinMM and reads the MIDI through a software decoder with an empty bank. This checks score parsing and playback controls. It does not measure synthesizer timbre or the end-to-end latency of a physical device.

```powershell
$env:SPACECADET_VERIFY_DIR = (New-Item -ItemType Directory -Force ./audio-check).FullName
$env:SPACECADET_VERIFY_AUDIO = '1'
$env:SDL_VIDEODRIVER = 'dummy'
$env:SDL_AUDIODRIVER = 'dummy'
$game = Start-Process -FilePath ./SpaceCadetHD.exe -ArgumentList '-verify','-sw','-reset' -WindowStyle Hidden -Wait -PassThru
$game.ExitCode
Get-Content -LiteralPath (Join-Path $env:SPACECADET_VERIFY_DIR 'report.txt')
```

A passing test exits with code 0. On the same Windows system, an instrumented 6.3.2 build completed the full test using the software decoder. A previous run through the native MIDI path exited with 0xC0000374. This narrowed the failure to that configuration, but the cause of the memory error with the system synthesizer remains unknown.
