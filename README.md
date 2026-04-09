# Bobby Carrot - Native PC Port (C++/SDL2)

🎮 **Порт классической J2ME игры Bobby Carrot на Windows/Linux/Mac**

Восстановлено из оригинального байткода.  
Использует **SDL2** для графики и **SDL2_mixer** для музыки.

## ✨ Особенности

- ✅ Полная механика из оригинала (ak-счётчик, конвейеры, яйца)
- ✅ Поддержка 50 уровней (normal01-30, egg01-20)
- ✅ MIDI музыка
- ✅ Пиксель-арт рендеринг без сглаживания
- ✅ Управление: стрелки/WASD, R - рестарт, N - след. уровень

## 🛠 Сборка

### Windows (MinGW)
```bash
g++ -o bobby.exe main.cpp -I"SDL2/include" -L"SDL2/lib" -lmingw32 -lSDL2main -lSDL2 -lSDL2_image -lSDL2_mixer
### LINUX 
g++ -o bobby main.cpp `pkg-config --cflags --libs sdl2 SDL2_image SDL2_mixer`
