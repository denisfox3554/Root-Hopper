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

## 🛠 Assembly

### Windows (MinGW)
```bash
g++ -o bobby.exe main.cpp -I"SDL2/include" -L"SDL2/lib" -lmingw32 -lSDL2main -lSDL2 -lSDL2_image -lSDL2_mixer

### LINUX
g++ -o bobby main.cpp `pkg-config --cflags --libs sdl2 SDL2_image SDL2_mixer`
