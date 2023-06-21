## ffmpeg_player_demo

### Features

+ **Get stream infos with `av_dump_format`(default running mode 0)**
+ **Dump H.264/265 and acc streams from mp4/vls file(mode 1)**
+ **Dump yuv data of video stream from mp4/vls file and play with SDL2(mode 2)**
+ **Dump pcm data of audio stream from mp4/vls file and play with SDL2(mode 3)**
+ **Play mp4/vls file with SDL2, like every media player does(mode 4)**

### Platforms

+ **MacOS**
+ **Linux**
+ **Windows**

### Build Prerequisites

+ **Installed ffmpeg dev lib**
+ **Installed SDL2 + SDL2_ttf support lib**
+ **Set `FFMPEG_DIR` and `SDL_DIR` properly to their respective installation paths**

### Usage

***Supported options:***

```bash
PS D:\Projects\CPP\ffmpeg_player_demo\build> .\ffmpeg_player_demo --help
Usage:
  ffmpeg_player_demo.exe [options] [arguments...]
Available options:
  -?, --help  print this help screen
  -m, --mode  player mode
```

***Get stream infos:***

```bash
PS D:\Projects\CPP\ffmpeg_player_demo\build> .\ffmpeg_player_demo --mode 0 ..\trash_can\out.mp4
[INFO] Input file: ..\trash_can\out.mp4
[INFO] Running mode: 0
[INFO] Player task: get stream infos
[INFO] Get stream infos for file: ..\trash_can\out.mp4
Input #0, mov,mp4,m4a,3gp,3g2,mj2, from '..\trash_can\out.mp4':
  Metadata:
    major_brand     : isom
    minor_version   : 512
    compatible_brands: isomiso2avc1mp41
    encoder         : Lavf60.6.100
  Duration: 00:04:24.28, start: 0.000000, bitrate: 2551 kb/s
  Stream #0:0[0x1](eng): Video: h264 (High) (avc1 / 0x31637661), yuv420p(tv, bt709, progressive), 3840x2160 [SAR 1:1 DAR 16:9], 2412 kb/s, 30 fps, 30 tbr, 15360 tbn (default)
    Metadata:
      handler_name    : VideoHandler
      vendor_id       : [0][0][0][0]
      encoder         : Lavc60.17.100 libx264
  Stream #0:1[0x2](eng): Audio: aac (LC) (mp4a / 0x6134706D), 48000 Hz, stereo, fltp, 130 kb/s (default)
    Metadata:
      handler_name    : SoundHandler
      vendor_id       : [0][0][0][0]
```

***Dump H.264/265 and acc streams:***

```bash
PS D:\Projects\CPP\ffmpeg_player_demo\build> .\ffmpeg_player_demo --mode 1 ..\trash_can\out.mp4
[INFO] Input file: ..\trash_can\out.mp4
[INFO] Running mode: 1
[INFO] Player task: dump H.264/265 and acc streams from mp4/vls file
[INFO] Dump H.264/265 and acc streams for file: ..\trash_can\out.mp4
[INFO] Dumped video stream to file: ..\trash_can\out.h264
[INFO] Dumped audio stream to file: ..\trash_can\out.aac
```

***Play video stream & dump YUV data while playing:***

```bash
PS D:\Projects\CPP\ffmpeg_player_demo\build> .\ffmpeg_player_demo --mode 2 ..\trash_can\out.mp4
[INFO] Input file: ..\trash_can\out.mp4
[INFO] Running mode: 2
[INFO] Player task: dump yuv data of video stream from mp4/vls file and play with SDL2
[INFO] Dump video yuv data for file: ..\trash_can\out.mp4
[]      >>>>  []
[INFO] Dumped yuv data to file: ..\trash_can\out.yuv
```

***Play audio stream & dump PCM data while playing:***

```bash
PS D:\Projects\CPP\ffmpeg_player_demo\build> .\ffmpeg_player_demo --mode 3 ..\trash_can\out.mp4
[INFO] Input file: ..\trash_can\out.mp4
[INFO] Running mode: 3
[INFO] Player task: dump pcm data of audio stream from mp4/vls file and play with SDL2
[INFO] Dump audio pcm data for file: ..\trash_can\out.mp4
[]>>>>        []
[INFO] Dumped pcm data to file: ..\trash_can\out.pcm
```

***Play media file:***

```bash
PS D:\Projects\CPP\ffmpeg_player_demo\build> .\ffmpeg_player_demo --mode 4 ..\trash_can\out.mp4
[INFO] Input file: ..\trash_can\out.mp4
[INFO] Running mode: 4
[INFO] Player task: play mp4/vls file with SDL2
[INFO] Playing: ..\trash_can\out.mp4
```

