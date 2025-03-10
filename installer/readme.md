# blur installer

## requirements

```
/dependencies
└ /lib
  │ blur.py (-> blur/src/blur.py)
  |
  ├─ /blur
  │  └> src/vapoursynth/blur
  |
  ├─ /ffmpeg
  │  └ ffmpeg.exe (https://www.gyan.dev/ffmpeg/builds)
  |
  └─ /vapoursynth
     │ ...vapoursynth portable files (x64, optionally removed docs & sdk)
     │ ...python embeddable package (x64)
     | ...extra plugins from vsrepo (adjust)
     |
     └ /vapoursynth64
       └ /plugins
         | akarin (https://github.com/AkarinVS/vapoursynth-plugin)
         | BestSource (x64) (https://github.com/vapoursynth/bestsource)
         | libmvtools (x64) (https://github.com/dubhater/vapoursynth-mvtools)
         └ svpflow1 & svpflow2 (x64) (https://web.archive.org/web/20190322064557/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip)

/redist
└ Visual C++ Redistributable (x64) (https://learn.microsoft.com/en-US/cpp/windows/latest-supported-vc-redist)

/resources
| blur.exe
└ blur-cli.exe
```
