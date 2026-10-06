# MultiLive

MultiLive is a Windows desktop live-streaming client built with Qt and FFmpeg. It captures a camera or the desktop, captures microphone audio, encodes H.264/AAC, and publishes to multiple RTMP or SRT outputs.

## Features

- Camera and Windows desktop capture
- Microphone capture
- H.264 video and AAC audio encoding
- Multiple simultaneous RTMP outputs using FLV
- Multiple simultaneous SRT outputs using MPEG-TS
- Reconnection with keyframe recovery
- Configurable resolution, frame rate, bit rate, and x264 preset
- Live output state, bit rate, FPS, queue, drop, and A/V timestamp statistics

## Pipeline

```text
Camera / Desktop -> Video queue -> H.264 encoder --+
                                                     +-> MultiPublisher -> RTMP / SRT
Microphone       -> Audio queue -> AAC encoder -----+
```

## Requirements

- Windows 10 or Windows 11
- Qt 5.14.2 with a MinGW 64-bit kit
- FFmpeg development libraries built with libx264 and SRT support
- C++17 compiler

## Build

1. Clone the repository.
2. Copy `config.pri.example` to `config.pri`.
3. Set `FFMPEG_ROOT` in `config.pri` to the local FFmpeg directory.
4. Open `MultiLive.pro` in Qt Creator.
5. Run qmake and build the project.
6. Put the required FFmpeg DLLs beside the executable or add the FFmpeg `bin` directory to `PATH`.

Example local configuration:

```qmake
FFMPEG_ROOT = D:/dev/ffmpeg
```

## Usage

1. Select a camera or desktop capture.
2. Select a microphone.
3. Configure video and audio encoding settings.
4. Add one or more RTMP or SRT output URLs.
5. Start streaming and monitor each output in the statistics table.

Example URLs:

```text
rtmp://server/live/stream-key
srt://127.0.0.1:9000?mode=caller
```

Never commit real stream keys to the repository.

## Roadmap

- NVENC hardware encoding
- H.265 encoding
- Adaptive bit rate
- WebRTC output
