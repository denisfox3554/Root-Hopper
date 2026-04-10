# Bobby Carrot - Native PC Port (C++/SDL2)

🎮 **Port of the classic J2ME game Bobby Carrot to Windows/Linux/Mac**

Rebuilt from the original bytecode.

Uses **SDL2** for graphics and **SDL2_mixer** for music.

## ✨ Features

- ✅ Complete mechanics from the original (AK counter, conveyors, eggs)
- ✅ Support for 50 levels (normal01-30, egg01-20)
- ✅ MIDI music
- ✅ Pixel art rendering without antialiasing
- ✅ Controls: Arrows/WASD, R - restart, N - next level

![image alt](https://github.com/denisfox3554/Root-Hopper/blob/a62eae994e4dd8d0d297585385b17cb4e8a28222/image.png)
## 🛠 Assembly

### Windows (MinGW)
```bash
g++ -o bobby.exe bobby_carrot.cpp -I"SDL2/include" -L"SDL2/lib" -lmingw32 -lSDL2main -lSDL2 -lSDL2_image -lSDL2_mixer

### LINUX
g++ -o bobby bobby_carrot.cpp `pkg-config --cflags --libs sdl2 SDL2_image SDL2_mixer`
