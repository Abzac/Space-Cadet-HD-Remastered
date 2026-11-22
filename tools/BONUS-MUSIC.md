# The Space Cadet theme on GeneralUser GS

The packaged recording is `Game/Music/SpaceCadet-GS.ogg`. The original score, `Game/Data/PINBALL.MID`, is a type 1 Standard MIDI File with 480 ticks per quarter note. Its metadata lists the title **ROCK2.MID #3** and the instrument tag **X5DR**. The recording uses S. Christian Collins's GeneralUser GS v2.0.3.

## Rebuilding the track from the recovery archive

Open PowerShell in the extracted recovery directory containing `Game`, `Art` and `Source`. Use 64-bit Python 3.8 or later on Windows. The commands below use the bundled libraries, require no additional Python packages and write the result to a separate directory.

```powershell
Expand-Archive -LiteralPath Art/Music/fluidsynth-v2.6.2-win10-x64-cpp11.zip -DestinationPath RebuiltMusic/Synth -Force
python Source/tools/render_bonus_music.py --midi Game/Data/PINBALL.MID --sf2 Art/Music/GeneralUser-GS.sf2 --bin RebuiltMusic/Synth/fluidsynth-v2.6.2-win10-x64-cpp11/bin --out RebuiltMusic/SpaceCadet-GS.ogg
Get-FileHash RebuiltMusic/SpaceCadet-GS.ogg, Game/Music/SpaceCadet-GS.ogg -Algorithm SHA256
```

The script checks both input hashes and verifies that all nine repeats are identical. Use `--inspect` to print metadata, note counts and controller values. FluidSynth sends stereo audio directly to the encoder through libsndfile 1.2.2 from the same archive. Rendering requires neither an audio device nor FFmpeg.

| File | SHA-256 |
|---|---|
| PINBALL.MID | `b030f95b0d3b7b582a8c030ce548e11ab9457e2281f615af1e291401ffb0be41` |
| GeneralUser-GS.sf2 | `9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe` |
| fluidsynth-v2.6.2-win10-x64-cpp11.zip | `808295345d43c9c428f3fa87d29fbf97a9926f356df95559ff2689f3bd4072d7` |
| SpaceCadet-GS.ogg | `bcd08bdae97104654ce455c695d5a491ef9e03b724be5fa537f7004f8a2ba5dd` |

## Parts and performance

Channels are numbered 1–16 and programs 0–127. **Reverb** and **Chorus** show the original CC91 and CC93 send levels (0–127). **Pan** is CC10, with 64 at the center.

| Channel | GM instrument played | Notes per loop | Volume (CC7) | Pan | Reverb | Chorus |
|---|---|---:|---:|---:|---:|---:|
| 2 | 38 — Synth Bass 1 | 234 | 127 | 64 | 29 | 0 |
| 3 | 11 — Vibraphone (track name: Xyl) | 127 | 55 | 41 | 92 | 0 |
| 4 | 28 — Electric Guitar (muted) | 446 | 105 | 74 | 115 | 81 |
| 10 | Standard Kit, 11 drum keys | 764 | 100 | Default | 79 | 0 |

The bass uses 53 pitch-bend messages per phrase. Each note retains its original velocity. The vibraphone sits left of center and the guitar slightly to the right. All four parts use reverb, and the guitar has a pronounced chorus send. The render uses the sound bank's instrument sends and the FluidSynth 2.6.2 effects settings.

## Loop boundary and levels

The 527.912912-second source contains a two-second setup bar and nine identical phrases in 4/4. The tempo is exactly 521,739 microseconds per quarter note (about 115 BPM). Each phrase spans 28 bars, or 53,760 ticks, and lasts 58.434768 seconds. The Ogg starts on the downbeat. The renderer plays one pass to establish effect tails and saves the next. A 20 ms cosine crossfade joins the ending to the tail from the previous pass. The last note releases about 40 ms before the boundary, keeping new note attacks outside the crossfade.

The stereo render runs at 48,000 Hz with seventh-order interpolation, 256 voices and the built-in reverb and chorus enabled. Synth gain starts at 0.2. Before encoding, both channels receive the same +9.906 dB gain, bringing the peak to −3.00 dBFS. Ogg Vorbis encodes at VBR quality 0.72, averaging about 248 kbit/s.

The decoded recording contains 2,804,869 stereo frames and runs for 58.434771 seconds. Integrated loudness is −18.8 LUFS, loudness range is 2.9 LU and measured true peak is −3.1 dBTP. The step from the last sample to the first measures 0.0093 of full scale on the left and 0.0152 on the right. The file is 1,813,837 bytes. Two independent renders produced the same SHA-256 hash.

## Assets and credits

- The recovery kit includes the sound bank, its full license in `Art/Music/GeneralUser-license.txt`, the commit recorded in `Art/Music/generaluser.commit` and an archive of the binary tools. Tool licenses are in `Art/Music/tool-licenses`.
- GeneralUser GS v2.0.3 by S. Christian Collins, commit `684543d5e5efaef08d02be50dcda8d552478fa60`. [Sound bank repository](https://github.com/mrbumpy409/GeneralUser-GS/tree/684543d5e5efaef08d02be50dcda8d552478fa60)
- The official **FluidSynth 2.6.2 win10-x64-cpp11** build supplies `libfluidsynth-3.dll`, `sndfile.dll` and their dependencies. [FluidSynth release](https://github.com/FluidSynth/fluidsynth/releases/tag/v2.6.2) · [libsndfile source](https://github.com/libsndfile/libsndfile/tree/1.2.2)
