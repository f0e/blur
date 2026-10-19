"""Inputs with awkward timing. Each flashes white for a frame at the moments a tone starts."""

from collections.abc import Callable
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path

from blur_testing.tools import FFMPEG, run

# irregular gaps, so a wrong alignment only ever lines up one or two events
EVENTS = [0.35, 0.80, 1.75, 2.35, 3.50, 4.00, 4.80, 5.50, 6.55, 7.10]
DURATION = 8.0
TONE_LENGTH = 0.06

X264 = ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p"]
X265 = ["-c:v", "libx265", "-preset", "fast", "-x265-params", "log-level=none", "-tag:v", "hvc1"]
VP9 = ["-c:v", "libvpx-vp9", "-deadline", "realtime", "-cpu-used", "8", "-crf", "20", "-b:v", "0"]
AAC = ["-c:a", "aac", "-b:a", "160k"]
FRAGMENTED = ["-movflags", "+frag_keyframe+empty_moov+default_base_moof"]


def ffmpeg(*args):
    run([FFMPEG, "-y", "-v", "error", *args])


def sources(fps: str, rate: int, video_duration: float, audio_duration: float) -> list:
    fps_value = Fraction(fps)
    frames = [round(t * fps_value) for t in EVENTS]
    flashes = "+".join(f"eq(n,{n})" for n in frames)

    # the tone starts on the flash frame's timestamp, whatever the framerate rounds the event to
    onsets = [float(n / fps_value) for n in frames]
    tones = "+".join(f"between(t,{t:.6f},{t + TONE_LENGTH:.6f})" for t in onsets)

    return [
        *("-f", "lavfi", "-i"),
        f"color=c=0x303030:s=320x240:r={fps}:d={video_duration},drawbox=c=white:t=fill:enable='{flashes}'",
        *("-f", "lavfi", "-i"),
        f"aevalsrc='0.8*sin(2*PI*1000*t)*({tones})':s={rate}:d={audio_duration}:c=stereo",
    ]


def encode(out: Path, *args, fps="60", rate=48000, video=X264, audio=AAC, video_duration=DURATION, audio_duration=None):
    ffmpeg(*sources(fps, rate, video_duration, audio_duration or video_duration), *video, *audio, *args, out)


def encoded(*args, **kwargs) -> Callable[[Path, Path], None]:
    return lambda out, tmp: encode(out, *args, **kwargs)


def remuxed(
    *args, seek: float | None = None, master_args=(), master_ext="mp4", **kwargs
) -> Callable[[Path, Path], None]:
    """Encodes a plain input with `master_args` and `kwargs`, then makes the case from it with `args`."""

    def make(out: Path, tmp: Path):
        master = tmp / f"master.{master_ext}"
        encode(master, *master_args, **kwargs)
        ffmpeg(*(("-ss", seek) if seek is not None else ()), "-i", master, *args, out)

    return make


def late_audio(start: float) -> list:
    return ["-map", "0", "-c:v", "copy", "-af", f"atrim=start={start}", *AAC]


def late_video(start: float) -> list:
    return ["-map", "0", "-vf", f"trim=start={start}", *X264, "-c:a", "copy"]


def vfr(vf: str) -> list:
    return ["-map", "0", "-vf", vf, "-fps_mode", "passthrough", *X264, "-c:a", "copy"]


def negative_cut_late_audio(out: Path, tmp: Path):
    master = tmp / "master.mp4"
    late = tmp / "late.mp4"
    encode(master, "-g", "60")
    ffmpeg("-i", master, *late_audio(1.6), late)
    ffmpeg("-ss", "1.3", "-i", late, "-c", "copy", out)


def cover_art_first(out: Path, tmp: Path):
    master = tmp / "master.mp4"
    cover = tmp / "cover.png"
    encode(master)
    ffmpeg("-f", "lavfi", "-i", "color=c=red:s=64x64:d=0.1", "-frames:v", "1", cover)
    ffmpeg(
        *("-i", cover, "-i", master, "-map", "0", "-map", "1:v", "-map", "1:a"),
        *("-c", "copy", "-c:v:0", "mjpeg", "-disposition:v:0", "attached_pic", out),
    )


def subtitle_stream(out: Path, tmp: Path):
    master = tmp / "master.mp4"
    subs = tmp / "subs.srt"
    encode(master)
    subs.write_text("1\n00:00:01,000 --> 00:00:03,000\nhello\n")
    ffmpeg("-i", master, "-i", subs, "-map", "0", "-map", "1", "-c", "copy", "-c:s", "mov_text", out)


@dataclass(frozen=True)
class Case:
    name: str
    make: Callable[[Path, Path], None]
    ext: str = "mp4"
    trim: bool = False  # also render trimmed
    copyable: bool = False  # the output container takes its audio as is
    extra: bool = False  # also render with the extra configs
    slack: float = 0.0  # extra seconds a flash can land either way


CASES = {
    c.name: c
    for c in [
        Case("baseline", encoded("-g", "60"), trim=True, copyable=True, extra=True),
        Case("no_bframes", encoded("-bf", "0"), copyable=True),
        Case("ntsc_2997_44k", encoded(fps="30000/1001", rate=44100), trim=True, copyable=True),
        Case("ntsc_5994", encoded(fps="60000/1001"), copyable=True),
        Case("hevc_bframes", encoded(video=X265), copyable=True),
        # 60fps in milliseconds, so frame durations alternate 16/17ms
        Case("coarse_timebase", encoded("-video_track_timescale", "1000"), copyable=True),
        # audio packets begin 0.5s in, which mp4 stores as an empty edit
        Case("audio_starts_late", remuxed(*late_audio(0.5)), trim=True, copyable=True, extra=True),
        Case("audio_starts_late_mkv", remuxed(*late_audio(0.5)), ext="mkv", trim=True, copyable=True),
        Case("video_starts_late", remuxed(*late_video(0.5)), trim=True, copyable=True, extra=True),
        Case("video_starts_late_small_mkv", remuxed(*late_video(0.05)), ext="mkv", copyable=True),
        # copying from mid gop leaves the packets before the cut at negative timestamps, hidden by an edit list
        Case(
            "negative_cut",
            remuxed("-c", "copy", seek=1.3, master_args=("-g", "60")),
            trim=True,
            copyable=True,
            extra=True,
        ),
        Case(
            "negative_cut_no_bframes",
            remuxed("-c", "copy", seek=1.3, master_args=("-g", "60", "-bf", "0")),
            copyable=True,
        ),
        Case("negative_cut_hevc", remuxed("-c", "copy", seek=1.3, master_args=("-g", "60"), video=X265), copyable=True),
        # no keyframe between the start and the cut, so there's 6s of preroll
        Case(
            "negative_cut_long_preroll",
            remuxed("-c", "copy", seek=6.0, master_args=("-g", "1000", "-sc_threshold", "0")),
            copyable=True,
        ),
        Case(
            "negative_cut_mkv",
            remuxed("-c", "copy", seek=1.3, master_args=("-g", "60")),
            ext="mkv",
            trim=True,
            copyable=True,
        ),
        Case("negative_cut_late_audio", negative_cut_late_audio, copyable=True),
        Case("offset_mkv", remuxed("-c", "copy", "-output_ts_offset", "3"), ext="mkv", copyable=True),
        Case("offset_mp4", remuxed("-c", "copy", "-output_ts_offset", "10"), trim=True, copyable=True),
        Case("mpegts", remuxed("-c", "copy"), ext="ts", trim=True, copyable=True, extra=True),
        Case("mpegts_audio_leads", remuxed(*late_video(0.4)), ext="ts", copyable=True),
        Case("mov_pcm", encoded(audio=["-c:a", "pcm_s16le"]), ext="mov"),
        Case("mkv_opus", encoded(audio=["-c:a", "libopus", "-b:a", "128k"]), ext="mkv", copyable=True),
        Case("mp4_opus", encoded(audio=["-c:a", "libopus", "-b:a", "128k"])),
        Case("mp4_mp3", encoded(rate=44100, audio=["-c:a", "libmp3lame", "-b:a", "192k"]), copyable=True),
        Case("mkv_flac_44k", encoded(rate=44100, audio=["-c:a", "flac"]), ext="mkv"),
        Case("webm_vp9_opus", encoded(video=VP9, audio=["-c:a", "libopus"]), ext="webm"),
        Case("aac_22k_mono", encoded("-ac", "1", rate=22050), copyable=True),
        # the second track starts later and at another sample rate
        Case(
            "multi_audio",
            remuxed(
                *("-map", "0:v", "-map", "0:a", "-map", "0:a", "-c:v", "copy", "-c:a:0", "copy"),
                *("-filter:a:1", "atrim=start=0.3,aresample=44100", "-c:a:1", "aac"),
            ),
            trim=True,
            copyable=True,
            extra=True,
        ),
        Case("audio_longer", encoded(video_duration=7.5, audio_duration=10.0), trim=True, copyable=True),
        Case("audio_shorter", encoded(audio_duration=5.0), trim=True, copyable=True),
        # a stretch of audio is missing, like a capture that dropped some, so later packets jump ahead
        Case(
            "audio_gap_mkv",
            remuxed("-map", "0", "-c:v", "copy", "-af", "aselect='not(between(t,2.5,3.2))'", *AAC),
            ext="mkv",
            trim=True,
        ),
        # a stretch of video frames is missing, so the timestamps jump
        Case(
            "vfr_dropped_frames",
            remuxed(*vfr("select='not(between(n,130,170))'"), master_ext="mkv"),
            ext="mkv",
            trim=True,
            extra=True,
        ),
        Case("vfr_dropped_frames_mp4", remuxed(*vfr("select='not(between(n,130,170))'")), trim=True),
        # containers
        Case("flv", encoded(), ext="flv", copyable=True),
        # asf timestamps carry a preroll that the demuxer takes back off
        Case(
            "asf_wmv", encoded(video=["-c:v", "wmv2", "-q:v", "3"], audio=["-c:a", "wmav2", "-b:a", "128k"]), ext="wmv"
        ),
        Case(
            "avi_mjpeg_pcm",
            encoded(video=["-c:v", "mjpeg", "-q:v", "3", "-pix_fmt", "yuvj420p"], audio=["-c:a", "pcm_s16le"]),
            ext="avi",
        ),
        # no b-frames. avi has no presentation timestamps, and both bestsource and lsmas lose or misplace frames when
        # mpeg-4 b-frames are reordered in it
        Case(
            "avi_xvid_mp3",
            encoded(
                rate=44100,
                video=["-c:v", "libxvid", "-bf", "0", "-q:v", "3"],
                audio=["-c:a", "libmp3lame", "-b:a", "192k"],
            ),
            ext="avi",
        ),
        Case("fragmented_mp4", encoded("-g", "60", *FRAGMENTED), trim=True, copyable=True),
        Case(
            "fragmented_mp4_negative_cut",
            remuxed("-c", "copy", *FRAGMENTED, seek=1.3, master_args=("-g", "60")),
            copyable=True,
        ),
        Case("nut", encoded(), ext="nut"),
        Case("mov_timecode_track", encoded("-timecode", "01:00:00:00"), ext="mov", copyable=True),
        # audio codecs
        Case("mkv_ac3", encoded(audio=["-c:a", "ac3", "-b:a", "192k"]), ext="mkv", copyable=True),
        Case("mp4_eac3", encoded(audio=["-c:a", "eac3", "-b:a", "192k"]), copyable=True),
        Case("mov_alac_44k", encoded(rate=44100, audio=["-c:a", "alac"]), ext="mov", copyable=True),
        Case("mov_pcm24_96k", encoded(rate=96000, audio=["-c:a", "pcm_s24le"]), ext="mov"),
        Case("webm_vorbis", encoded(video=VP9, audio=["-c:a", "libvorbis", "-q:a", "5"]), ext="webm"),
        Case("mpegts_mp2", encoded(audio=["-c:a", "mp2", "-b:a", "192k"]), ext="ts", copyable=True),
        Case("aac_51", encoded("-ac", "6"), copyable=True),
        Case("aac_8k_mono", encoded("-ac", "1", rate=8000, audio=["-c:a", "aac", "-b:a", "24k"]), copyable=True),
        Case(
            "prores_pcm",
            encoded(
                video=["-c:v", "prores_ks", "-profile:v", "0", "-pix_fmt", "yuv422p10le"], audio=["-c:a", "pcm_s24le"]
            ),
            ext="mov",
        ),
        # framerates
        # millisecond timestamps can't hold 1001/24000 frames exactly
        Case("fps_23976_mkv", encoded(fps="24000/1001"), ext="mkv", trim=True, copyable=True, extra=True),
        Case("fps_25", encoded(fps="25"), copyable=True),
        Case("fps_50", encoded(fps="50"), copyable=True),
        Case("fps_144", encoded(fps="144"), trim=True, copyable=True, extra=True),
        Case("fps_240", encoded(fps="240"), copyable=True),
        # timestamps
        # mpegts timestamps wrap at 2^33 / 90khz, which this crosses about 2.7s in
        Case(
            "mpegts_pts_wrap",
            remuxed("-c", "copy", "-output_ts_offset", "95441"),
            ext="ts",
            trim=True,
            copyable=True,
        ),
        # nothing says to hide the packets before the cut
        Case(
            "negative_cut_no_editlist",
            remuxed("-c", "copy", "-use_editlist", "0", seek=1.3, master_args=("-g", "60")),
            copyable=True,
        ),
        Case("aac_priming_no_editlist", encoded("-use_editlist", "0"), copyable=True),
        Case("video_starts_3s_late", remuxed(*late_video(3)), trim=True, copyable=True),
        Case("mpegts_audio_starts_2s_late", remuxed(*late_audio(2)), ext="ts", trim=True, copyable=True),
        # every other frame is missing for a while, like a game dropping to 30fps. bestsource makes it constant with
        # the frame nearest each slot rather than the one showing, so a frame shown for two slots can land a slot early
        Case(
            "vfr_half_rate_section",
            remuxed(*vfr("select='not(between(n,200,320)*mod(n,2))'"), master_ext="mkv"),
            ext="mkv",
            trim=True,
            slack=1 / 60,
        ),
        # each frame a few ms off its slot, like a phone recording
        Case(
            "vfr_jittered_timestamps",
            remuxed(*vfr("setpts='(N/60+0.003*sin(N*1.7))/TB'"), master_ext="mkv"),
            ext="mkv",
            trim=True,
        ),
        # stream layouts
        Case("cover_art_first", cover_art_first, copyable=True),
        Case("audio_stream_first", remuxed("-map", "0:a", "-map", "0:v", "-c", "copy"), copyable=True),
        Case("subtitle_stream", subtitle_stream, copyable=True),
        Case(
            "second_video_stream",
            remuxed(
                *("-f", "lavfi", "-i", f"color=c=blue:s=160x120:r=30:d={DURATION}"),
                *("-map", "0:v", "-map", "0:a", "-map", "1:v", "-c:v:0", "copy", "-c:v:1", "libx264", "-c:a", "copy"),
            ),
            ext="mkv",
            copyable=True,
        ),
    ]
}
