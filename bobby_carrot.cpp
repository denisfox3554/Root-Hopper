// Bobby Carrot - C++/SDL2 Port
// Восстановлено из оригинального J2ME байткода (a.class)
// 
// ТОЧНЫЙ МАППИНГ ТАЙЛОВ (из метода l()V байткода):
//   19 = МОРКОВКА (normal levels), R++ при загрузке
//   21 = СПАВН игрока, g=col*16+8, C=row*16+8
//   44 = ВЫХОД (открывается когда R==0)
//   45 = ЯЙЦО (egg levels), R++ при загрузке
//
// МЕХАНИКА ДВИЖЕНИЯ (из методов b()Z, m()V, K()V):
//   ak = счётчик пикселей движения (0..16)
//   При нажатии: ak=16, T/ac меняются
//   К()V вызывается при ak==8 (на полпути к тайлу)
//   Перерисовка: Bobby рисуется в позиции lerp(from, to, (16-ak)/16)
//
// GAME LOOP: 60fps через SDL vsync (оригинал ~30fps Nokia S40)
// Скорость движения: 130ms/тайл (как в оригинале)

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>

// ============================================================
// Константы
// ============================================================
static const int SCREEN_W    = 512;
static const int SCREEN_H    = 512;
static const int TILE_SIZE   = 16;
static const int MAP_COLS    = 16;
static const int MAP_ROWS    = 16;
static const int SCALE       = 3;
static const int SCALED_TILE = TILE_SIZE * SCALE;

// Тайминг движения - как в оригинале (130ms на клетку)
static const float STEP_TIME  = 0.130f;
// Анимация ходьбы - 9 кадров за STEP_TIME
static const float ANIM_SPD   = STEP_TIME / 9.0f;
// Конвейерная анимация
static const float CONV_SPD   = 0.10f;
// Смерть анимация
static const float DEATH_SPD  = 0.08f;
// Idle анимация через 5 секунд бездействия
static const float IDLE_TIME  = 5.0f;

// ============================================================
// ТАЙЛЫ (точно из байткода)
// ============================================================
// Из l()V:
static const uint8_t T_CARROT  = 21;  // морковка (1 шт на уровень)
static const uint8_t T_SPAWN   = 44;  // спавн игрока -> пол
static const uint8_t T_EXIT    = 44;  // выход (когда R==0)
static const uint8_t T_EGG     = 45;  // яйцо (egg mode) - пока как морковка
// Из K()V:
static const uint8_t T_EMPTY   = 18;  // пол после подбора морковки
static const uint8_t T_FLOOR   = 18;  // пол

// Реальная логика: непроходимые тайлы определяем визуально:
// 0=void, 1-8=бордюры, 9-17=заборы/стены -> SOLID
// 18=пол, 19=морковка, 20=пустая морковка, 21=спавн -> walkable
// 44=выход -> walkable
// Трава/стена = тайл в tileset который выглядит как стена

// SOLID тайлы (непроходимые)
static bool tile_solid(uint8_t t) {
    if (t == 0)  return true;   // void
    // Бордюры и декорации по краям
    if (t >= 1  && t <= 8)  return true;
    // Заборы и стены (9-17 + 19 = трава-стена)
    if (t >= 9  && t <= 17) return true;
    if (t == 19) return true;   // трава/стена
    return false;
}

// Конвейеры (из K()V и данных уровней)
static bool tile_conveyor(uint8_t t) {
    // Из K()V: тайл 22 -> T()V, тайлы 24-29, 38
    // Из egg уровней: 22-43 содержат конвейеры
    return (t >= 22 && t <= 43);
}

// Направление конвейера (из b()Z: 40=conv_up, 41=conv_down, 42=conv_left, 43=conv_right)
static void conveyor_delta(uint8_t t, int& dx, int& dy) {
    dx = dy = 0;
    // Из b()Z - точные тайл ID конвейеров:
    switch (t) {
        // Правые конвейеры
        case 22: case 26: case 36: case 38: dx=1;  break;
        // Левые конвейеры  
        case 23: case 27: case 37: case 39: dx=-1; break;
        // Верхние конвейеры
        case 24: case 28: case 34: case 40: dy=-1; break;
        // Нижние конвейеры
        case 25: case 29: case 35: case 41: dy=1;  break;
        // Комбинированные (из egg уровней)
        case 30: case 32: dx=1;  break;
        case 31: case 33: dx=-1; break;
        case 42: dy=-1; break;
        case 43: dy=1;  break;
    }
}

// ============================================================
enum GameState { STATE_TITLE, STATE_PLAYING, STATE_CLEAR, STATE_DEAD, STATE_WIN };

struct Level {
    uint8_t tiles[MAP_ROWS][MAP_COLS];
    int     R;          // счётчик морковок (как в оригинале)
    int     spawn_x, spawn_y;
    bool    egg_mode;   // true = egg уровень
};

struct Player {
    // Позиция в тайлах (= T и ac в оригинале)
    int   tx, ty;
    // Пиксельная позиция центра (g и C в оригинале, *16+8)
    int   px, py;
    // Предыдущая пиксельная позиция (для интерполяции)
    int   from_px, from_py;
    // ak = счётчик движения 16->0
    int   ak;
    float ak_timer;     // накопитель времени для ak
    // Направление лица (0=up,1=down,2=left,3=right, как в оригинале)
    int   face;
    // Анимация ходьбы
    int   walk_frame;
    float walk_timer;
    // Idle
    float idle_timer;
    bool  idle_anim;
    // Смерть
    bool  dead;
    int   death_frame;
    float death_timer;
    // Клавиши (как в оригинале - флаги n,r,p,b = left,right,up,down)
    bool  key_left, key_right, key_up, key_down;
};

struct Game {
    GameState state;
    Level     level;
    Player    player;
    int       current_level;
    int       total_levels;
    std::vector<std::string> level_files;
    float     state_timer;
    float     conv_anim_timer;
    int       conv_anim_frame;
    float     finish_anim_timer;
    int       finish_anim_frame;
};
static Game g_game;

// SDL
static SDL_Window*   g_win = nullptr;
static SDL_Renderer* g_ren = nullptr;
static SDL_Texture  *tex_tiles, *tex_up, *tex_down, *tex_left, *tex_right;
static SDL_Texture  *tex_idle, *tex_death;
static SDL_Texture  *tex_cv_up, *tex_cv_down, *tex_cv_left, *tex_cv_right;
static SDL_Texture  *tex_finish, *tex_title, *tex_end, *tex_cleared, *tex_numbers;
static Mix_Music    *mus_title, *mus_clear, *mus_death, *mus_end;

// ============================================================
static SDL_Texture* load_tex(const char* p) {
    SDL_Surface* s = IMG_Load(p);
    if (!s) { fprintf(stderr,"IMG: %s %s\n",p,IMG_GetError()); return nullptr; }
    SDL_SetColorKey(s,SDL_TRUE,SDL_MapRGB(s->format,255,0,255));
    SDL_Texture* t = SDL_CreateTextureFromSurface(g_ren, s);
    SDL_FreeSurface(s);
    if (t) SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);
    return t;
}

static void draw_tile(int id, int sx, int sy) {
    if (!tex_tiles||id<0||id>47) return;
    SDL_Rect src={(id%8)*TILE_SIZE,(id/8)*TILE_SIZE,TILE_SIZE,TILE_SIZE};
    SDL_Rect dst={sx,sy,SCALED_TILE,SCALED_TILE};
    SDL_RenderCopy(g_ren,tex_tiles,&src,&dst);
}

static void draw_num(int n, int x, int y) {
    if (!tex_numbers) return;
    char buf[16]; snprintf(buf,16,"%d",n);
    for (int i=0;buf[i];i++) {
        SDL_Rect src={(buf[i]-'0')*5,0,5,8};
        SDL_Rect dst={x+i*(5*SCALE+1),y,5*SCALE,8*SCALE};
        SDL_RenderCopy(g_ren,tex_numbers,&src,&dst);
    }
}

// ============================================================
// Загрузка уровня - точно как l()V в байткоде
// ============================================================
static bool load_level(Game& g, int idx) {
    FILE* f = fopen(g.level_files[idx].c_str(),"rb");
    if (!f) return false;
    uint8_t buf[260]; fread(buf,1,260,f); fclose(f);

    Level& lv = g.level;
    memset(&lv,0,sizeof(lv));
    lv.egg_mode = (g.level_files[idx].find("egg") != std::string::npos);
    const uint8_t* raw = buf+4;

    // Двойной цикл как в l()V
    for (int row=0;row<MAP_ROWS;row++) {
        for (int col=0;col<MAP_COLS;col++) {
            uint8_t t = raw[row*MAP_COLS+col];
            lv.tiles[row][col] = t;

            // if tile == 21: сохранить позицию спавна (из l()V offset 30-68)
            if (t == T_SPAWN) {
                lv.spawn_x = col;
                lv.spawn_y = row;
                lv.tiles[row][col] = T_FLOOR; // спавн становится полом
            }
            // if tile == 21: морковка
            else if (t == T_CARROT) {  // тайл 21 = морковка
                lv.R++;
            }
            // if egg_mode && tile == 45: R++ (яйцо)
            else if (lv.egg_mode && t == T_EGG) {
                lv.R++;
            }
        }
    }
    return true;
}

// ============================================================
// Инициализация игрока (позиция = spawn * 16 + 8, как в оригинале)
// ============================================================
static void init_player(Game& g) {
    Player& p = g.player;
    p.tx = g.level.spawn_x;
    p.ty = g.level.spawn_y;
    // g = col*16+8, C = row*16+8 (центр тайла) - из l()V
    p.px = p.tx * TILE_SIZE + 8;
    p.py = p.ty * TILE_SIZE + 8;
    p.from_px = p.px; p.from_py = p.py;
    p.ak = 0; p.ak_timer = 0;
    p.face = 1; // down
    p.walk_frame = 0; p.walk_timer = 0;
    p.idle_timer = 0; p.idle_anim = false;
    p.dead = false; p.death_frame = 0; p.death_timer = 0;
    p.key_left = p.key_right = p.key_up = p.key_down = false;
}

static void start_level(Game& g, int idx) {
    g.current_level = idx;
    load_level(g, idx);
    init_player(g);
    g.state = STATE_PLAYING; g.state_timer = 0;
    g.conv_anim_frame = 0; g.conv_anim_timer = 0;
    g.finish_anim_frame = 0; g.finish_anim_timer = 0;
}

// ============================================================
// K()V - обработка тайла при прибытии (на ak==8 в оригинале)
// ============================================================
static void on_arrive(Game& g) {
    Player& p = g.player;
    Level&  lv = g.level;
    uint8_t t = lv.tiles[p.ty][p.tx];

    // Морковка (тайл 19 в normal, 45 в egg)
    bool is_collectible = (t == T_CARROT) || // 21=морковка всегда
                          (t == T_EGG);  // 45=яйцо в egg уровнях
    if (is_collectible) {
        lv.tiles[p.ty][p.tx] = T_EMPTY; // 18 = пол
        lv.R--;
    }

    // Выход - тайл 44, когда R==0
    if (t == T_EXIT && lv.R == 0) {
        g.state = STATE_CLEAR; g.state_timer = 0;
        return;
    }

    // Конвейер
    if (!p.dead && p.ak == 0 && tile_conveyor(t)) {
        int dx, dy; conveyor_delta(t, dx, dy);
        if (dx || dy) {
            int nx = p.tx+dx, ny = p.ty+dy;
            if (nx>=0&&nx<MAP_COLS&&ny>=0&&ny<MAP_ROWS&&!tile_solid(lv.tiles[ny][nx])) {
                p.from_px = p.px; p.from_py = p.py;
                p.tx = nx; p.ty = ny;
                p.px = p.tx*TILE_SIZE+8; p.py = p.ty*TILE_SIZE+8;
                p.ak = 16; p.ak_timer = 0;
                if (dx>0) p.face=3; else if (dx<0) p.face=2;
                else if (dy<0) p.face=0; else p.face=1;
            }
        }
    }
}

// ============================================================
// b()Z - обработка ввода (из байткода)
// ============================================================
static bool handle_input(Game& g) {
    Player& p = g.player;
    Level&  lv = g.level;
    if (p.ak != 0 || p.dead) return false;

    int dx=0, dy=0;
    // Из b()Z: проверяем флаги n(left), r(right), p(up), b(down)
    // с учётом граничных тайлов (n=wall слева, r=wall справа)
    if      (p.key_up)    dy=-1;
    else if (p.key_down)  dy=1;
    else if (p.key_left)  dx=-1;
    else if (p.key_right) dx=1;
    else return false;

    int nx=p.tx+dx, ny=p.ty+dy;
    if (nx<0||nx>=MAP_COLS||ny<0||ny>=MAP_ROWS) return false;
    uint8_t dest = lv.tiles[ny][nx];
    if (tile_solid(dest)) return false;
    // Яйца блокируют проход (из описания: "Боб не может пройти сквозь яйца")
    if (lv.egg_mode && dest == T_EGG) return false;

    // Начинаем движение
    if (dx>0) p.face=3; else if (dx<0) p.face=2;
    else if (dy<0) p.face=0; else p.face=1;

    p.from_px = p.px; p.from_py = p.py;
    p.tx = nx; p.ty = ny;
    p.px = p.tx*TILE_SIZE+8; p.py = p.ty*TILE_SIZE+8;
    p.ak = 16; p.ak_timer = 0;
    p.idle_timer = 0; p.idle_anim = false;
    return true;
}

// ============================================================
// Update
// ============================================================
static void update(Game& g, float dt) {
    if (g.state==STATE_TITLE||g.state==STATE_WIN) return;
    if (g.state==STATE_CLEAR) {
        g.state_timer+=dt;
        if (g.state_timer>2.0f) {
            int n=g.current_level+1;
            if (n>=g.total_levels){g.state=STATE_WIN;g.state_timer=0;}
            else start_level(g,n);
        }
        return;
    }
    if (g.state==STATE_DEAD) {
        Player& p=g.player;
        p.death_timer+=dt;
        p.death_frame=(int)(p.death_timer/DEATH_SPD);
        if (p.death_frame>=10){
            g.state_timer+=dt;
            if (g.state_timer>0.8f) start_level(g,g.current_level);
        }
        return;
    }

    // Анимация конвейеров и выхода
    g.conv_anim_timer+=dt;
    if (g.conv_anim_timer>=CONV_SPD){g.conv_anim_timer=0;g.conv_anim_frame=(g.conv_anim_frame+1)%4;}
    g.finish_anim_timer+=dt;
    if (g.finish_anim_timer>=0.15f){g.finish_anim_timer=0;g.finish_anim_frame=(g.finish_anim_frame+1)%4;}

    Player& p=g.player;
    if (p.dead) return;

    // Обновляем ak (движение) - уменьшается со временем
    if (p.ak > 0) {
        p.ak_timer += dt;
        float steps = p.ak_timer / (STEP_TIME / 16.0f);
        int new_ak = 16 - (int)steps;
        if (new_ak < 0) new_ak = 0;

        // K()V вызывается когда ak достигает 0 (в оригинале при ak==8, но логика та же)
        if (new_ak == 0 && p.ak > 0) {
            p.ak = 0;
            on_arrive(g);
        } else {
            p.ak = new_ak;
        }

        // Анимация ходьбы
        p.walk_timer += dt;
        if (p.walk_timer >= ANIM_SPD) { p.walk_timer=0; p.walk_frame=(p.walk_frame+1)%9; }
    }

    // Idle анимация
    if (p.ak == 0 && !p.dead) {
        p.idle_timer += dt;
        if (p.idle_timer >= IDLE_TIME) p.idle_anim = true;
    }

    // Обработка ввода (только если не движемся)
    if (p.ak == 0) {
        handle_input(g);
    }
}

// ============================================================
// Render
// ============================================================
static void render(Game& g) {
    SDL_SetRenderDrawColor(g_ren,0,0,0,255);
    SDL_RenderClear(g_ren);

    if (g.state==STATE_TITLE) {
        if (tex_title){SDL_Rect d={SCREEN_W/2-192,SCREEN_H/2-192,384,384};SDL_RenderCopy(g_ren,tex_title,nullptr,&d);}
        SDL_RenderPresent(g_ren); return;
    }
    if (g.state==STATE_WIN) {
        if (tex_end){SDL_Rect d={SCREEN_W/2-192,SCREEN_H/2-192,384,384};SDL_RenderCopy(g_ren,tex_end,nullptr,&d);}
        SDL_RenderPresent(g_ren); return;
    }

    Level& lv=g.level;
    Player& p=g.player;

    // Визуальная позиция игрока (интерполяция between from и to)
    float t_lerp = (p.ak > 0) ? (float)(16 - p.ak) / 16.0f : 1.0f;
    float vis_x = (p.from_px + (p.px - p.from_px) * t_lerp - 8); // -8 чтобы центрировать
    float vis_y = (p.from_py + (p.py - p.from_py) * t_lerp - 8);

    // Камера центрирована на игроке (float для плавности)
    float cx = vis_x * SCALE - SCREEN_W/2.0f + SCALED_TILE/2.0f;
    float cy = vis_y * SCALE - SCREEN_H/2.0f + SCALED_TILE/2.0f;
    int mpw=MAP_COLS*SCALED_TILE, mph=MAP_ROWS*SCALED_TILE;
    if (cx<0) cx=0; if (cy<0) cy=0;
    if (cx>mpw-SCREEN_W) cx=(float)(mpw-SCREEN_W);
    if (cy>mph-SCREEN_H) cy=(float)(mph-SCREEN_H);
    if (mpw<SCREEN_W) cx=(mpw-SCREEN_W)/2.0f;
    if (mph<SCREEN_H) cy=(mph-SCREEN_H)/2.0f;

    // Рисуем тайлы
    for (int row=0;row<MAP_ROWS;row++) {
        for (int col=0;col<MAP_COLS;col++) {
            int sx=(int)(col*SCALED_TILE - cx);
            int sy=(int)(row*SCALED_TILE - cy);
            if (sx+SCALED_TILE<0||sx>SCREEN_W||sy+SCALED_TILE<0||sy>SCREEN_H) continue;
            uint8_t tile = lv.tiles[row][col];
            if (tile==0) continue;

            // Пол под всем walkable
            if (!tile_solid(tile)) draw_tile(T_FLOOR, sx, sy);

            // Рисуем тайл
            if (tile==T_FLOOR || tile==T_EMPTY) {
                // уже нарисован пол
            } else if (tile==T_CARROT) {
                // Морковка: тайл 19 в данных, но в tileset это row=2,col=3 = tile index 19
                // Однако визуально морковка = tile 20 в tileset (из скрина)
                draw_tile(19, sx, sy); // tileset[19] = картинка морковки
            } else if (tile==T_EGG) {
                draw_tile(45, sx, sy); // яйцо
            } else if (tile==T_EXIT) {
                // Выход: анимированный если R==0, иначе закрытый
                if (lv.R == 0 && tex_finish) {
                    SDL_Rect src={g.finish_anim_frame*TILE_SIZE,0,TILE_SIZE,TILE_SIZE};
                    SDL_Rect dst={sx,sy,SCALED_TILE,SCALED_TILE};
                    SDL_RenderCopy(g_ren,tex_finish,&src,&dst);
                } else {
                    draw_tile(tile, sx, sy); // закрытый выход
                }
            } else if (tile_conveyor(tile)) {
                int cdx=0,cdy=0; conveyor_delta(tile,cdx,cdy);
                SDL_Texture* ct=nullptr;
                if (cdx>0) ct=tex_cv_right; else if (cdx<0) ct=tex_cv_left;
                else if (cdy<0) ct=tex_cv_up; else if (cdy>0) ct=tex_cv_down;
                if (ct) {
                    SDL_Rect src={g.conv_anim_frame*TILE_SIZE,0,TILE_SIZE,TILE_SIZE};
                    SDL_Rect dst={sx,sy,SCALED_TILE,SCALED_TILE};
                    SDL_RenderCopy(g_ren,ct,&src,&dst);
                } else draw_tile(tile,sx,sy);
            } else if (tile == 19) {
                // blm-19 = стена/трава, рисуем как tileset[1]
                draw_tile(1, sx, sy);
            } else {
                draw_tile(tile, sx, sy);
            }
        }
    }

    // Игрок
    int bx=(int)(vis_x*SCALE-cx);
    int by=(int)(vis_y*SCALE-cy);

    if (g.state==STATE_DEAD) {
        if (tex_death) {
            int fr=SDL_min(p.death_frame,10);
            SDL_Rect src={fr*16,0,16,27};
            SDL_Rect dst={bx,by-(27-TILE_SIZE)*SCALE,16*SCALE,27*SCALE};
            SDL_RenderCopy(g_ren,tex_death,&src,&dst);
        }
    } else {
        SDL_Texture* bt=nullptr; int fw=16,fh=25,fr=0;
        if (p.ak==0 && !p.idle_anim) {
            bt=tex_idle; fw=18; fh=25; fr=0;
        } else if (p.ak==0 && p.idle_anim) {
            bt=tex_idle; fw=18; fh=25; fr=1; // idle анимация
        } else {
            switch(p.face){
                case 0: bt=tex_up;    break;
                case 1: bt=tex_down;  break;
                case 2: bt=tex_left;  break;
                case 3: bt=tex_right; break;
            }
            fr=p.walk_frame%9;
        }
        if (bt) {
            SDL_Rect src={fr*fw,0,fw,fh};
            SDL_Rect dst={bx,by-(fh-TILE_SIZE)*SCALE,fw*SCALE,fh*SCALE};
            SDL_RenderCopy(g_ren,bt,&src,&dst);
        }
    }

    // HUD
    SDL_SetRenderDrawBlendMode(g_ren,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ren,0,0,0,170);
    SDL_Rect hb={0,0,SCREEN_W,30}; SDL_RenderFillRect(g_ren,&hb);
    SDL_SetRenderDrawBlendMode(g_ren,SDL_BLENDMODE_NONE);
    draw_num(g.current_level+1, 10, 8);
    draw_tile(19, SCREEN_W-70, 4);  // иконка морковки
    draw_num(lv.R, SCREEN_W-22, 8);

    if (g.state==STATE_CLEAR && tex_cleared) {
        int w=80*SCALE,h=19*SCALE;
        SDL_Rect d={SCREEN_W/2-w/2,SCREEN_H/2-h/2,w,h};
        SDL_RenderCopy(g_ren,tex_cleared,nullptr,&d);
    }

    SDL_RenderPresent(g_ren);
}

// ============================================================
static void build_levels(Game& g, const char* dir) {
    g.level_files.clear();
    char p[512];
    for (int i=1;i<=30;i++){snprintf(p,512,"%s/normal%02d.blm",dir,i);g.level_files.push_back(p);}
    for (int i=1;i<=20;i++){snprintf(p,512,"%s/egg%02d.blm",dir,i);   g.level_files.push_back(p);}
    g.total_levels=(int)g.level_files.size();
}

int main(int argc, char* argv[]) {
    const char* adir=(argc>1)?argv[1]:"assets";

    SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO);
    IMG_Init(IMG_INIT_PNG);
    Mix_Init(MIX_INIT_MID|MIX_INIT_OGG);
    Mix_OpenAudio(44100,MIX_DEFAULT_FORMAT,2,1024);
    Mix_VolumeMusic(MIX_MAX_VOLUME);

    g_win=SDL_CreateWindow("Bobby Carrot",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,SCREEN_W,SCREEN_H,SDL_WINDOW_SHOWN);
    g_ren=SDL_CreateRenderer(g_win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // пиксель-арт без сглаживания

    char p[512];
#define L(v,f) snprintf(p,512,"%s/" f,adir);v=load_tex(p);
    L(tex_tiles,"tileset.png")   L(tex_up,"bobby_up.png")
    L(tex_down,"bobby_down.png") L(tex_left,"bobby_left.png")
    L(tex_right,"bobby_right.png") L(tex_idle,"bobby_idle.png")
    L(tex_death,"bobby_death.png")
    L(tex_cv_up,"tile_conveyor_up.png") L(tex_cv_down,"tile_conveyor_down.png")
    L(tex_cv_left,"tile_conveyor_left.png") L(tex_cv_right,"tile_conveyor_right.png")
    L(tex_finish,"tile_finish.png") L(tex_title,"title.png")
    L(tex_end,"end.png") L(tex_cleared,"cleared.png") L(tex_numbers,"numbers.png")
#undef L

    snprintf(p,512,"%s/title.mid",adir);   mus_title=Mix_LoadMUS(p);
    snprintf(p,512,"%s/cleared.mid",adir); mus_clear=Mix_LoadMUS(p);
    snprintf(p,512,"%s/death.mid",adir);   mus_death=Mix_LoadMUS(p);
    snprintf(p,512,"%s/end.mid",adir);     mus_end  =Mix_LoadMUS(p);
    if (mus_title) Mix_PlayMusic(mus_title,-1);

    memset(&g_game,0,sizeof(g_game));
    build_levels(g_game,adir);
    g_game.state=STATE_TITLE;

    bool run=true;
    Uint32 last=SDL_GetTicks();
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type==SDL_QUIT) run=false;
            if (e.type==SDL_KEYDOWN||e.type==SDL_KEYUP) {
                bool down=(e.type==SDL_KEYDOWN);
                Player& pp=g_game.player;
                switch(e.key.keysym.sym) {
                    case SDLK_UP:    case SDLK_w: pp.key_up   =down; break;
                    case SDLK_DOWN:  case SDLK_s: pp.key_down =down; break;
                    case SDLK_LEFT:  case SDLK_a: pp.key_left =down; break;
                    case SDLK_RIGHT: case SDLK_d: pp.key_right=down; break;
                    case SDLK_ESCAPE: run=false; break;
                    case SDLK_r: if(down&&g_game.state==STATE_PLAYING) start_level(g_game,g_game.current_level); break;
                    case SDLK_n: if(down){int n=g_game.current_level+1;if(n<g_game.total_levels)start_level(g_game,n);} break;
                    case SDLK_m: if(down){Mix_PausedMusic()?Mix_ResumeMusic():Mix_PauseMusic();} break;
                    case SDLK_RETURN: case SDLK_SPACE:
                        if(down) {
                            if(g_game.state==STATE_TITLE) start_level(g_game,0);
                            else if(g_game.state==STATE_WIN){g_game.state=STATE_TITLE;Mix_HaltMusic();if(mus_title)Mix_PlayMusic(mus_title,-1);}
                        }
                        break;
                    default: break;
                }
            }
        }

        Uint32 now=SDL_GetTicks();
        float dt=(now-last)/1000.0f; if(dt>0.05f)dt=0.05f;
        last=now;
        update(g_game,dt);

        static GameState prev=(GameState)-1;
        if (g_game.state!=prev) {
            prev=g_game.state; Mix_HaltMusic();
            switch(g_game.state) {
                case STATE_TITLE: if(mus_title)Mix_PlayMusic(mus_title,-1); break;
                case STATE_CLEAR: if(mus_clear)Mix_PlayMusic(mus_clear,1);  break;
                case STATE_DEAD:  if(mus_death)Mix_PlayMusic(mus_death,1);  break;
                case STATE_WIN:   if(mus_end)  Mix_PlayMusic(mus_end,  -1); break;
                default: break;
            }
        }
        render(g_game);
    }

    Mix_HaltMusic();
    if(mus_title)Mix_FreeMusic(mus_title); if(mus_clear)Mix_FreeMusic(mus_clear);
    if(mus_death)Mix_FreeMusic(mus_death); if(mus_end)  Mix_FreeMusic(mus_end);
    Mix_CloseAudio(); Mix_Quit();
    SDL_DestroyRenderer(g_ren); SDL_DestroyWindow(g_win);
    IMG_Quit(); SDL_Quit();
    return 0;
}
