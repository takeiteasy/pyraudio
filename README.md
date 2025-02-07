# pal

> [!WARNING]
> Work in progress

> [!INFO]
> `pip install pyaudiolib==0.0.1`

```python
from signal import signal, SIGINT
from sys import exit
import pal

def hndl(_, __):
    exit(0)

pal.initialize()
assert(pal.is_ready())
music = pal.Music(path="country.mp3")
assert(music.is_ready())w
music.play()

signal(SIGINT, hndl)

print("Press CTRL+C to interrupt...")
while True:
    music.update()

pal.shutdown()
```

## TODO:

- [X] Implement update function for all classes
- [X] Access to AudioStream struct from Music+Sound
- [ ] AudioStream processor callbacks

## DEPENDENCIES:

- raudio.h     - A simple and easy-to-use audio library based on miniaudio (https://github.com/raysan5/raudio)
- miniaudio.h  - Audio device management lib (https://github.com/mackron/miniaudio)
- stb_vorbis.h - Ogg audio files loading (http://www.nothings.org/stb_vorbis/)
- dr_wav.h     - WAV audio files loading (http://github.com/mackron/dr_libs)
- dr_mp3.h     - MP3 audio file loading (https://github.com/mackron/dr_libs)
- dr_flac.h    - FLAC audio file loading (https://github.com/mackron/dr_libs)
- jar_xm.h     - XM module file loading
- jar_mod.h    - MOD audio file loading

## LICENSE
```
MIT License

Copyright (c) 2025 George Watson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
