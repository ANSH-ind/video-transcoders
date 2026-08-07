# video-transcoders (Enterprise Video Engine)

A high-performance, multi-threaded C++ video transcoding engine built on top of FFmpeg. This engine converts standard video files (`.mp4`, `.mov`, etc.) into highly optimized, Adaptive Bitrate (ABR) HTTP Live Streaming (HLS) formats and also for other formats.

It is designed for production backend environments, featuring real-time AES-128 encryption, broadcast-standard audio normalization, and seamless integration with Python via `pybind11`.

---

## Key Features

* **Multi-Core ABR Encoding:** Spawns isolated threads to simultaneously transcode video into multiple resolutions (e.g., 1080p, 720p, 480p, 360p) to maximize CPU utilization.
* **Content-Aware Scaling:** Automatically detects if a video is horizontal (16:9) or vertical (9:16 for Shorts/Reels) and adjusts the output dimensions dynamically.
* **AES-128 DRM Encryption:** Encrypts video chunks on the fly to prevent unauthorized downloading and piracy.
* **EBU R 128 Audio Normalization:** Analyzes and normalizes audio loudness to standard broadcast levels (`-14 LUFS`) using FFmpeg's `loudnorm` filter graph.
* **Thread-Safe Logger:** Built-in mutex-locked logging system to prevent terminal text corruption during multi-threaded execution.
* **Python Native Binding:** Architected to be compiled as a `.so` module, allowing you to orchestrate the C++ engine natively from Python scripts.

---

## Prerequisites

You must have the FFmpeg development libraries and a C++ compiler installed on your system.

**Ubuntu / Debian:**
```bash
sudo apt update
sudo apt install g++ libavformat-dev libavcodec-dev libswscale-dev libavutil-dev libavfilter-dev libswresample-dev
```

# Architecture
* The project is cleanly separated into two main components:
* hls_engine.cpp: The pure C++ core logic containing all FFmpeg memory management, multithreading, and video processing.
* ​wrapper.cpp: The lightweight pybind11 bridge that exposes the C++ engine to Python.

##how to use

> Step 1: Generate Encryption Keys
* Before running the engine, you must generate an AES-128 key. The engine looks for a file named enc.keyinfo to encrypt the stream.
​Run this in your terminal:

```bash
# 1. Generate a 16-byte hex key
openssl rand 16 > enc.key

# 2. Create the keyinfo file for FFmpeg
echo "enc.key" > enc.keyinfo
echo "enc.key" >> enc.keyinfo
```

- Note: In a real production environment, the first line of enc.keyinfo should be the public URL where your web player can fetch the key

#api uses
```python
from vedio_transcoders import  hls_encoder

#The engine will automatically pad missing bitrates using production defaults
#and detect if the video is Vertical or Horizontal.

hls_encoder(
    input="user_upload.mp4",
    output="master.m3u8",
    resolution=["1080p", "720p", "480p"],
    bitrate=[5000000, 2500000],          #480p will auto-default to 1,000,000
    key="enc.keyinfo"                    # Path to your DRM keyinfo file
)

print("Video successfully packaged for HLS delivery!")

