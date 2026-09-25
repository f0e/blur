# Config files explained

All config files are stored in your config folder:

- Windows: `%APPDATA%\blur`
- macOS: `~/Library/Application Support/blur`
- Linux: `~/.config/blur` (or `$XDG_CONFIG_HOME/blur`)

| File                                           | Purpose                                        |
| ---------------------------------------------- | ---------------------------------------------- |
| [`configs/*.cfg`](#configscfg)                 | Blur configs, the settings used when rendering |
| [`rules.cfg`](#rulescfg)                       | Which blur config to use for each video        |
| [`encoding-presets.cfg`](#encoding-presetscfg) | FFmpeg arguments for each encode preset        |
| [`app.cfg`](#appcfg)                           | App and PC-specific settings                   |

## configs/\*.cfg

Each file in the `configs` folder is a separate blur config.

### Blur

- blur - Whether or not the output video file will have motion blur
- blur amount - If blur is enabled, this is the amount of motion blur (0 = no blur, 1 = fully blend every frame together, 1+ = more blur/ghosting)
- blur output fps - If blur is enabled, this is the FPS the output video will be
- blur weighting - Weighting function to use when blending frames. Options are listed below. Also: [View weighting comparison graphs](../tests/plot_weighting_functions/weighting_functions.pdf)
  - equal - Each frame is blended equally
  - gaussian_sym
  - vegas
  - pyramid
  - gaussian
  - ascending
  - descending
  - gaussian_reverse
  - custom weights - Custom comma-separated frame weights, e.g. 5, 3, 3, 2, 1. Higher numbers indicate frames being more visible when blending, lower numbers mean they are less so.
- preserve brightness - Blends frames by how much light they hold rather than averaging the stored values. Produces a more natural-looking blur
- bloom - Makes bright areas glow, brightening the pixels around them
- bloom threshold - How bright something has to be before it glows (0 = everything glows, 1 = only the very brightest)
- bloom strength - How bright the glow is

### Interpolation

- interpolate - Whether or not the input video file will be interpolated to a higher FPS
- interpolated fps - If interpolate is enabled, this is the FPS that the input file will be interpolated to (before blurring). Can be a set FPS number or a multiplier (append x to end e.g. `5x`)
- interpolation method - Method used for interpolation (`svp`, `rife`, `rife (tensorrt)`, `mvtools`):
  - Quality: rife = rife (tensorrt) > svp
  - Speed: svp >> rife (tensorrt) > rife
  - `rife (tensorrt)` is only available with a supported NVIDIA GPU
  - Note: On macOS, SVP requires SVP Manager to be open or a red border will appear. It provides a 30-day trial, but then costs $24.99 for a lifetime license. RIFE can always be used however, but it is slower than SVP.

### Pre-interpolation

- pre-interpolate - Enable pre-interpolation using a more accurate but slower AI model before main interpolation
- pre-interpolated fps - FPS to pre-interpolate the input video to (before main interpolation and blurring). Can be a set FPS number or a multiplier (append x to end e.g. `5x`)
- pre-interpolation method - Method used for pre-interpolation (`rife`, `rife (tensorrt)`):
  - Quality: rife = rife (tensorrt)
  - Speed: rife (tensorrt) > rife

### Deduplication

- deduplicate - Removes duplicate frames and generates new interpolated frames to take their place. Fixes 'unsmooth' looking output caused by stuttering in recordings
- deduplicate method - Method used to replace duplicate frames. Only used when interpolation is off, otherwise duplicates are filled by the interpolation method:
  - Quality: rife = rife (tensorrt) > svp
  - Speed: old > svp >>> rife

### Masking

Masks protect regions of the frame (like a HUD) from interpolation and deduplication. Black is protected, white is interpolated as normal.

- mask - Image in the `masks` folder (inside your [config folder](#config-files-explained)) to use as a mask, empty for none
- auto mask - Generates an extra mask per video from the parts that never move, applied on top of `mask`. Tuned in the advanced section

### Rendering

- encode preset - Encoding preset to use (e.g. `h264`, `h265`, `av1`). Presets are defined in [`encoding-presets.cfg`](#encoding-presetscfg), and the ones available depend on whether GPU encoding is on and the `gpu type` in [`app.cfg`](#appcfg)
- quality - Quality of the output video. Lower = better quality (e.g. [CRF](https://trac.ffmpeg.org/wiki/Encode/H.264#crf) for CPU encoding, 0 = lossless quality, 51 = really bad). The range depends on the encoder
- preview - Opens a render preview window
- detailed filenames - Adds blur settings to generated filenames
- copy dates - Copies over the modified date from the input file to the output file
- upscale - Upscales to 4K using nearest-neighbour interpolation

### RIFE models

- rife model - Model used by `rife`. Models are read from the `models` folder where blur is installed (`lib/models` on Windows), drop new ones in to use them
- rife (tensorrt) model - Model used by `rife (tensorrt)`

### GPU acceleration

- gpu decoding - Uses GPU when decoding. Can cause issues
- gpu interpolation - Uses GPU when interpolating
- gpu encoding - Uses GPU when rendering. The GPU type used is set in [`app.cfg`](#appcfg)

### Timescale

- timescale - Enable video timescale manipulation
- input timescale - Timescale of the input video file (will be sped up/slowed down accordingly)
- output timescale - Timescale of the output video file
- adjust timescaled audio pitch - Will pitch shift audio when sped up/slowed down

### Filters

- filters - Enable video filters
- brightness - Brightness of the output video
- saturation - Saturation of the output video
- contrast - Contrast of the output video

### Advanced

- advanced - Enables the advanced settings below. When disabled, their defaults are used

### Advanced deduplication

- deduplicate range - How far apart two frames can be and still have frames generated between them (-1 = infinite). Make it higher if your footage is at a lower FPS than it should be (e.g. choppy 120fps gameplay recorded at 240fps), lower it if your blurred footage starts blurring static elements such as menu screens
- deduplicate threshold - Threshold of movement that triggers deduplication. Turn on debug and render a video to embed text showing the movement in each frame
- deduplicate real frame - Which frame in a run of duplicates is the real one:
  - first - The first frame (default)
  - last - The last frame
  - center - The middle frame
  - surrounding - Ignores all the duplicates and uses the frames either side of them. Gives the smoothest result but can lead to a lot of artifacting
- deduplicate max future checks - How many following runs of duplicates `surrounding` can skip over (limited by deduplicate range)

### Advanced frame timing

- use frame timing logs - For use with the [obs-frame-timing-recorder](https://github.com/f0e/obs-frame-timing-recorder) OBS plugin. Uses the `.frametiming` files it generates to work out when each frame was really drawn, resulting in much smoother blur. Replaces deduplication, as it's more accurate

### Advanced rendering

- video container - The output video container format (e.g. `mp4`, `mkv`, `avi`)
- custom ffmpeg filters - Custom FFmpeg filters to be used when rendering (replaces GPU encoding, encode preset & quality options)
- debug - Logs FFmpeg & vspipe commands, and adds a text overlay displaying frame similarity onto duplicate frames
- source plugin - VapourSynth plugin used to read the input video (`LWLibavSource` or `BestSource`)

### Advanced masking

- auto mask samples - How many frames are sampled to find the parts that never move
- auto mask stillness - How much of the time a pixel has to stay static
- auto mask fill - How far the mask fills into a flat area from the detail around it
- auto mask padding - How far the mask spreads past what was found
- auto mask feather - How far the edge of the mask fades out

### Advanced blur

- blur weighting gaussian std dev - Standard deviation used in the gaussian weighting
- blur weighting gaussian mean - Mean used in the gaussian weighting
- blur weighting gaussian bound - Bound used in the gaussian weighting

### Advanced interpolation

- svp interpolation preset - Preset used for framerate interpolation when using SVP, one of:
  - weak (default) - _[Explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - film - _[Explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - smooth - _[Explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - animation - _[Explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - default _(Default SVP settings)_
  - test
- svp interpolation algorithm - Algorithm used for framerate interpolation when using SVP, one of:
  - 13 - Best overall quality and smoothness (default) - _[Explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - 23 - Sometimes smoother than 13, but can result in smearing - _[Explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - 1 - _[Explained further here](https://www.svp-team.com/wiki/Manual:SVPflow)_
  - 2 - _[Explained further here](https://www.spirton.com/uploads/InterFrame/InterFrame2.html)_
  - 11 - _[Explained further here](https://www.svp-team.com/wiki/Manual:SVPflow)_
  - 21 - _[Explained further here](https://www.svp-team.com/wiki/Manual:SVPflow)_

- interpolation block size - Block size used for framerate interpolation. Higher block size = less accurate blur, will result in spaces around non-moving objects of the frame, also renders faster. Lower block size = more accurate blur, but can result in artifacting, also slower. For higher framerate input videos lower block size can be better. Options:
  - 4
  - 8 (default)
  - 16
  - 32

- interpolation mask area - Mask amount used when interpolating. Higher values can mean static objects are blurred less, but can also result in less smooth output (moving parts of the image can be mistaken for static parts and don't get blurred)

### Manual SVP override

You can customise the SVP interpolation settings even further by manually defining JSON parameters. [See here](https://www.svp-team.com/wiki/Manual:SVPflow) for explanations on settings.

- manual svp: Enables manual SVP settings, true/false
- super string: JSON string used as input in [SVSuper](https://www.svp-team.com/wiki/Manual:SVPflow#SVSuper.28source.2C_params_string.29)
- vectors string: JSON string used as input in [SVAnalyse](https://www.svp-team.com/wiki/Manual:SVPflow#SVAnalyse.28super.2C_params_string.2C_.5Bsrc.5D:_clip.29)
- smooth string: JSON string used as input in [SVSmoothFps](https://www.svp-team.com/wiki/Manual:SVPflow#SVSmoothFps.28source.2C_super.2C_vectors.2C_params_string.2C_.5Bsar.5D:_float.2C_.5Bmt.5D:_integer.29)

## rules.cfg

- default config - Blur config used when no rule matches. Empty to pick one in the queue each time
- rule - Picks a blur config for videos whose path matches a pattern. Rules are checked in order and the first match wins
  - config - Name of the blur config to use
  - pattern - Matched against the video's full path, case insensitive. `*` matches anything and `?` matches any single character, e.g. `D:/clips/csgo/*`. Without wildcards, matches any path containing the pattern

## encoding-presets.cfg

Presets are grouped by encoder (`nvidia`, `amd`, `intel`, `mac`, `cpu`), each a `name: ffmpeg arguments` line. `{quality}` is replaced with the config's `quality`.

- Built-in presets are marked with `*` and can't be modified
- Add your own presets under the relevant group. They need a video codec (`-c:v`) to show up as an encode preset

## app.cfg

### PC-specific blur settings

- output prefix - Folder rendered videos are saved to, relative to the input video's folder
- gpu type (nvidia/amd/intel) - GPU used for GPU encoding, picks which group of encode presets is used
- rife gpu - GPU used by `rife` (auto = fastest)
- rife (tensorrt) gpu - GPU used by `rife (tensorrt)` (auto = fastest)

### GUI

- window width - Width of the app window
- window height - Height of the app window
- dpi scale (0 = auto) - UI scale. 0 uses the OS scale
- blur amount tied to fps - Scales blur amount with output FPS when output FPS is changed, keeping the same amount of blur
- skip queue - Starts rendering videos as soon as they're added instead of queueing them up

### Preview

- preview volume - Volume of videos previewed in the queue
- hardware accelerated preview - Decodes previewed videos (in the queue and config preview) on the GPU. Will usually be faster, but not always. Try toggling it if the preview is choppy
- sample video path - Video used to preview config changes
- config preview seek - Where in the sample video to preview (0 = start, 1 = end)

### Desktop notifications

- render success notifications - Sends a desktop notification when a render finishes
- render failure notifications - Sends a desktop notification when a render fails
- taskbar progress - Shows how far along the current render is on the app's taskbar icon

### Updates

- check for updates - Checks for new versions on launch
- include beta updates - Also notifies about beta versions
- dismissed update version - Update you dismissed, so you won't be notified about it again

### Linux

- vapoursynth lib path - Path to your VapourSynth libraries, used if they aren't in the default location
