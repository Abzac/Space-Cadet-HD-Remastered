"""Render a packaged soundtrack from the original MIDI and a supported SoundFont.

Requires 64-bit Python 3 and the Windows FluidSynth 2.6.2 release's bin directory.
No audio device, third-party Python modules, or network connection is used.
"""
import argparse
import array
import collections
import ctypes as C
import hashlib
import math
import os
from pathlib import Path
import struct
import tempfile

RATE = 48000
MIDI_SHA = "b030f95b0d3b7b582a8c030ce548e11ab9457e2281f615af1e291401ffb0be41"
BANK_SHA = "9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe"
# Fixed gain in dB for each pinned bank, measured against the GeneralUser GS
# loop at approximately -18.8 LUFS. A single gain preserves its dynamics.
BANK_GAIN_DB = {
    "9a57fb3b6714e69dda12390e351b087e81fc3b1eca15c6b4bbe172799f4cf3cd": 6.46,
    "74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0": 8.60,
    "c5378b62028c920cb11e4803327983fee2f2cdff5dc89c708e39da417e51c854": 9.12,
}


def variable_length(data, offset):
    value = 0
    for _ in range(4):
        byte = data[offset]
        offset += 1
        value = (value << 7) | (byte & 127)
        if not byte & 128:
            return value, offset
    raise ValueError("Bad SMF variable length")


def read_midi(filename):
    data = Path(filename).read_bytes()
    if data[:4] != b"MThd":
        raise ValueError("Expected Standard MIDI File")
    header_size, fmt, tracks, ppqn = struct.unpack_from(">IHHH", data, 4)
    if fmt != 1 or not 0 < ppqn < 32768:
        raise ValueError("Expected a multitrack SMF with metrical timing")
    offset = 8 + header_size
    events = []
    for track in range(tracks):
        if data[offset:offset+4] != b"MTrk":
            raise ValueError("Missing SMF track")
        end = offset + 8 + struct.unpack_from(">I", data, offset+4)[0]
        offset += 8
        tick = running = 0
        while offset < end:
            delta, offset = variable_length(data, offset)
            tick += delta
            status = data[offset]
            if status & 128:
                offset += 1
                running = status if status < 240 else 0
            else:
                status = running
            if status == 255:
                kind = data[offset]
                length, offset = variable_length(data, offset+1)
            elif status in (240, 247):
                kind = status
                length, offset = variable_length(data, offset)
            elif 128 <= status < 240:
                kind = status >> 4
                length = 1 if kind in (12, 13) else 2
            else:
                raise ValueError("Unknown SMF event")
            events.append((tick, track, status, kind, data[offset:offset+length]))
            offset += length
        if offset != end:
            raise ValueError("SMF event overruns track")
    # Python's stable sort preserves on-track order at shared ticks.
    return ppqn, sorted(events, key=lambda e: e[0])


def inspect_midi(events):
    program = collections.defaultdict(int)
    usage = collections.Counter()
    controls = collections.defaultdict(set)
    effects = collections.Counter()
    for tick, track, status, kind, values in events:
        channel = status & 15
        if status == 255:
            if kind in (3, 6, 47, 81, 88):
                print("track", track, "tick", tick, "meta", kind, values)
        elif status >= 240:
            print("system event", tick, status, values.hex())
        elif kind == 12:
            program[channel] = values[0]
        elif kind == 9 and values[1]:
            usage[channel+1, program[channel]] += 1
        elif kind == 11:
            controls[channel+1, values[0]].add(values[1])
        else:
            effects[kind] += 1
    print("Sounding notes by (MIDI channel 1-16, program 0-127)", sorted(usage.items()))
    for source, values in sorted(controls.items()):
        print("Channel, CC", source, "range", min(values), max(values), "distinct", len(values))
    print("Other MIDI messages", effects)


def original_loop(ppqn, events):
    markers = [(tick, int.from_bytes(values, "big")) for tick, _, status, kind, values in events
               if status == 255 and kind == 81]
    start, tempo = markers[0]
    ticks = markers[1][0] - start
    end = max(e[0] for e in events)
    if markers != [(start+i*ticks, tempo) for i in range(9)] or end != start+9*ticks:
        raise ValueError("Original nine-phrase tempo map has changed")
    messages = [(tick, status, values) for tick, _, status, _, values in events if status < 240]
    setup = [(tick, status, data) for tick, status, data in messages if tick < start]
    phrases = [[(tick-first, status, data) for tick, status, data in messages if first <= tick < first+ticks]
               for first, _ in markers]
    if any(p != phrases[0] for p in phrases[1:]) or any(tick >= end for tick, _, _ in messages):
        raise ValueError("The score contains variations; refusing to truncate them")
    def to_frame(tick, usec):
        denominator = 1_000_000 * ppqn
        return (tick * usec * RATE + denominator // 2) // denominator
    intro = [(to_frame(t, 500000), s, d) for t, s, d in setup]
    body = [(to_frame(t, tempo), s, d) for t, s, d in phrases[0]]
    return (intro, to_frame(start, 500000)), (body, to_frame(ticks, tempo))


def api(dll, name, result, *args):
    fn = getattr(dll, name)
    fn.restype = result
    fn.argtypes = list(args)
    return fn


def synthesize(bin_dir, sf2, intro, loop):
    lib = C.CDLL(str(bin_dir / "libfluidsynth-3.dll"))
    p, i, s = C.c_void_p, C.c_int, C.c_char_p
    settings = api(lib, "new_fluid_settings", p)()
    synth = None
    try:
        setnum = api(lib, "fluid_settings_setnum", i, p, s, C.c_double)
        setint = api(lib, "fluid_settings_setint", i, p, s, i)
        for name, value in [(b"synth.sample-rate", RATE), (b"synth.gain", 0.2)]:
            if setnum(settings, name, value) != 0:
                raise RuntimeError(name)
        for name, value in [(b"synth.cpu-cores", 1), (b"synth.polyphony", 256),
                            (b"synth.reverb.active", 1), (b"synth.chorus.active", 1)]:
            if setint(settings, name, value) != 0:
                raise RuntimeError(name)
        synth = api(lib, "new_fluid_synth", p, p)(settings)
        if not synth or api(lib, "fluid_synth_sfload", i, p, s, i)(synth, str(sf2).encode("utf8"), 1) < 0:
            raise RuntimeError("Cannot load the synthesizer or its SoundFont")
        if api(lib, "fluid_synth_set_interp_method", i, p, i, i)(synth, -1, 7) != 0:
            raise RuntimeError("Seventh-order interpolation unavailable")
        controls = {8: ("noteoff", 1), 9: ("noteon", 2), 10: ("key_pressure", 2),
                    11: ("cc", 2), 12: ("program_change", 1), 13: ("channel_pressure", 1),
                    14: ("pitch_bend", 1)}
        send = {kind: api(lib, "fluid_synth_"+name, i, p, i, *([i]*n)) for kind, (name, n) in controls.items()}
        write = api(lib, "fluid_synth_write_float", i, p, i, p, i, i, p, i, i)
        buf = (C.c_float * (8192*2))()
        def render(part):
            events, frames = part
            pcm = array.array("f")
            pos = 0
            for frame, status, data in events + [(frames, 0, b"")]:
                while pos < frame:
                    n = min(frame-pos, len(buf)//2)
                    if write(synth, n, buf, 0, 2, buf, 1, 2) != 0:
                        raise RuntimeError("Synthesis failed")
                    pcm.frombytes(C.string_at(buf, n*8))
                    pos += n
                if status:
                    kind, channel = status >> 4, status & 15
                    if kind == 14:
                        args = [data[0] | (data[1] << 7)]
                    elif kind == 8 or (kind == 9 and data[1] == 0):
                        kind, args = 8, [data[0]]
                    else:
                        args = data
                    # A note-off for a previously choked GM drum may return FLUID_FAILED.
                    send[kind](synth, channel, *args)
            return pcm
        render(intro)
        tail_frames = 960
        previous = render(loop)[-tail_frames*2:]
        pcm = render(loop)
        # Loop into the real attack with its original overlapping effects tails.
        # The last note-off precedes this transition by more than 20 ms.
        for frame in range(tail_frames):
            blend = 0.5 - 0.5 * math.cos(math.pi*frame/(tail_frames-1))
            for channel in range(2):
                idx = -tail_frames*2+frame*2+channel
                pcm[idx] += (previous[idx]-pcm[idx])*blend
        return pcm
    finally:
        if synth:
            api(lib, "delete_fluid_synth", None, p)(synth)
        api(lib, "delete_fluid_settings", None, p)(settings)


def encode(bin_dir, destination, pcm):
    class Info(C.Structure):
        _fields_ = [("frames", C.c_int64), ("samplerate", C.c_int), ("channels", C.c_int),
                    ("format", C.c_int), ("sections", C.c_int), ("seekable", C.c_int)]
    lib = C.CDLL(str(bin_dir / "sndfile.dll"))
    info = Info(0, RATE, 2, 0x200000 | 0x0060, 0, 0)  # Ogg / Vorbis
    handle = api(lib, "sf_wchar_open", C.c_void_p, C.c_wchar_p, C.c_int, C.POINTER(Info))(
        str(destination), 0x20, C.byref(info))
    if not handle:
        raise RuntimeError("Cannot open output for encoding")
    try:
        quality = C.c_double(0.72)
        command = api(lib, "sf_command", C.c_int, C.c_void_p, C.c_int, C.c_void_p, C.c_int)
        if command(handle, 0x1300, C.byref(quality), C.sizeof(quality)) != 1:
            raise RuntimeError("Vorbis variable quality selection failed")
        write = api(lib, "sf_writef_float", C.c_int64, C.c_void_p, C.c_void_p, C.c_int64)
        data = (C.c_float*len(pcm)).from_buffer(pcm)
        for first in range(0, len(pcm)//2, 4096):
            frames = min(4096, len(pcm)//2-first)
            if write(handle, C.byref(data, first*8), frames) != frames:
                raise RuntimeError("Encoded fewer than the requested sample frames")
    finally:
        if api(lib, "sf_close", C.c_int, C.c_void_p)(handle):
            raise RuntimeError("Failed to finish Ogg stream")


def stable_serial(filename):
    # libogg chooses a random serial; repaginate only the serial/CRC fields for reproducible bytes.
    data = bytearray(filename.read_bytes())
    table = []
    for value in range(256):
        crc = value << 24
        for _ in range(8):
            crc = (crc << 1 ^ (0x04c11db7 if crc & 0x80000000 else 0)) & 0xffffffff
        table.append(crc)
    pos = 0
    while pos < len(data):
        if data[pos:pos+4] != b"OggS":
            raise ValueError("Invalid Ogg page")
        header_end = pos+27+data[pos+26]
        end = header_end+sum(data[pos+27:header_end])
        data[pos+14:pos+18] = b"SCP7"
        data[pos+22:pos+26] = bytes(4)
        crc = 0
        for ch in data[pos:end]:
            crc = ((crc << 8) & 0xffffffff) ^ table[(crc >> 24) ^ ch]
        struct.pack_into("<I", data, pos+22, crc)
        pos = end
    filename.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--midi", required=True, type=Path)
    parser.add_argument("--sf2", required=True, type=Path)
    parser.add_argument("--bin", required=True, type=Path, help="FluidSynth release bin directory")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--inspect", action="store_true", help="Also print source MIDI event summary")
    args = parser.parse_args()
    if hashlib.sha256(args.midi.read_bytes()).hexdigest() != MIDI_SHA:
        raise ValueError("Unexpected MIDI score: " + str(args.midi))
    bank_hash = hashlib.sha256(args.sf2.read_bytes()).hexdigest()
    if bank_hash != BANK_SHA and bank_hash not in BANK_GAIN_DB:
        raise ValueError("Unrecognised SoundFont: " + str(args.sf2))
    ppqn, events = read_midi(args.midi)
    if args.inspect:
        inspect_midi(events)
    intro, loop = original_loop(ppqn, events)
    bin_dir = args.bin.resolve()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with os.add_dll_directory(str(bin_dir)):
        pcm = synthesize(bin_dir, args.sf2.resolve(), intro, loop)
        peak = max(map(abs, pcm))
        if bank_hash == BANK_SHA:
            gain = (10 ** (-3/20)) / peak
        else:
            gain = C.c_float(10 ** (BANK_GAIN_DB[bank_hash] / 20)).value
        for k in range(len(pcm)):
            pcm[k] *= gain
        with tempfile.TemporaryDirectory(dir=args.out.parent) as tmpdir:
            pending = Path(tmpdir).resolve() / "render.ogg"
            encode(bin_dir, pending, pcm)
            stable_serial(pending)
            os.replace(pending, args.out)
    print(f"{len(pcm)//2} frames at {RATE} Hz; gain {20*math.log10(gain):+.3f} dB; PCM peak {20*math.log10(peak*gain):.2f} dBFS")
    print("SHA-256", hashlib.sha256(args.out.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
