#!/usr/bin/env python3
"""avsync-measure.py REC.mkv - A/V offset from a screen+sound recording of test/av-probe.html.
Record the box region of the emulator window and the emulator's sound with ONE ffmpeg:
  ffmpeg -use_wallclock_as_timestamps 1 -f x11grab -framerate 50 -video_size 160x160 -t 40 -i :93+X,Y \
         -f pulse -fragment_size 1024 -t 40 -i SINK.monitor -copyts -c:v ffv1 -c:a pcm_s16le REC.mkv
(-copyts keeps both inputs on the wall clock, so -t goes before each -i; the pulse input stamps wall clock minus
its own latency.) Rig check with xsetroot flashes + pacat beeps on an idle display: -20..0 ms.
Video onsets: mean brightness of the region crosses 50 %. Audio onsets: the beep envelope stays above a threshold for 50 ms.
Prints every pair and the median: positive = sound AFTER the picture (sound late)."""
import subprocess, sys, statistics, array
f = sys.argv[1]
def out(cmd): return subprocess.run(cmd, capture_output=True, check=True).stdout
def start(sel): return float(out(["ffprobe", "-v", "error", "-select_streams", sel, "-show_entries", "stream=start_time", "-of", "csv=p=0", f]).decode().split()[0])
# video: per-frame mean brightness with the frame's own (wall clock) timestamp
rows = out(["ffprobe", "-v", "error", "-f", "lavfi", "-i", "movie=%s,scale=1:1:flags=area,signalstats" % f,
            "-show_entries", "frame=pts_time:frame_tags=lavfi.signalstats.YAVG", "-of", "csv=p=0"]).decode().split()
vf = [(float(t), float(y)) for t, y in (r.split(",")[:2] for r in rows if "," in r)]
von = [vf[i][0] for i in range(1, len(vf)) if vf[i][1] >= 128 > vf[i - 1][1]]
# audio: 10 ms envelope of the 1 kHz beeps from the stream's first sample time
AR = 8000; a0 = start("a:0")
a = array.array("h", out(["ffmpeg", "-loglevel", "error", "-i", f, "-map", "0:a", "-ac", "1", "-ar", str(AR), "-f", "s16le", "-"]))
win = AR // 100
env = [max(abs(x) for x in a[i:i + win]) for i in range(0, len(a) - win, win)]
thr = max(env) * 0.3 if env else 1
aon, last = [], -9.0
for i, e in enumerate(env):
    t = a0 + i * 0.01
    # a beep: loud for 50 ms on end (clicks from buffer jumps are shorter), at least 1.5 s after the previous one
    if e >= thr and (i == 0 or env[i - 1] < thr) and t - last > 1.5 and min(env[i:i + 5]) >= thr: aon.append(t); last = t
d = []
for t in von:
    near = [x - t for x in aon if abs(x - t) < 1.0]
    if near: d.append(min(near, key=abs) * 1000)
print("video onsets %d, audio onsets %d, pairs %d (video start %.3f, audio start %.3f)" % (len(von), len(aon), len(d), start("v:0"), a0))
print("offsets ms:", " ".join("%+.0f" % x for x in d))
if d: print("MEDIAN %+.0f ms (positive = sound late)" % statistics.median(d))
