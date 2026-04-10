// Bobby Carrot - C++/SDL2 Port
// Восстановлено из оригинального J2ME байткода (a.class)
// 
// ТОЧНЫЙ МАППИНГ ТАЙЛОВ:
//   18 = ПОЛ
//   19 = МОРКОВКА
//   20 = ЯМА (пустота после сбора)
//   21 = ТЕЛЕПОРТ / СПАВН
//   24-31 = КОНВЕЙЕРЫ (обычные)
//   30-32 = КОНВЕЙЕРЫ-ШИПЫ (безопасны только по направлению)
//   40-43 = ЖЁЛТЫЕ КОНВЕЙЕРЫ
//   44 = ВЫХОД
//   45 = ЯЙЦО
//   46-48 = ОБЫЧНЫЕ ШИПЫ (смерть всегда)
//
// МЕХАНИКА:
//   - Конвейеры 30-32 убивают только при движении ПРОТИВ направления
//   - При движении ПО направлению конвейера - безопасно
//   - Обычные шипы 46-48 убивают всегда

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

static const float STEP_TIME  = 0.130f;
static const float ANIM_SPD   = STEP_TIME / 4.5f;
static const float CONV_SPD   = 0.10f;
static const float DEATH_SPD  = 0.08f;
static const float IDLE_TIME  = 5.0f;

// ============================================================
// ТАЙЛЫ
// ============================================================
static const uint8_t T_FLOOR   = 18;
static const uint8_t T_CARROT  = 19;
static const uint8_t T_EMPTY   = 20;
static const uint8_t T_TELEPORT= 21;
static const uint8_t T_SPAWN   = 21;
static const uint8_t T_EXIT    = 44;
static const uint8_t T_EGG     = 45;

// SOLID тайлы (непроходимые)
static bool tile_solid(uint8_t t) {
    if (t == 0)  return true;
    if (t >= 1  && t <= 8)  return true;
    if (t >= 9  && t <= 17) return true;
    if (t >= 22 && t <= 23) return true;   // кнопки
    if (t >= 32 && t <= 39) return true;   // замки
    return false;
}

// Конвейеры
static bool tile_conveyor(uint8_t t) {
    return (t >= 24 && t <= 31) || (t >= 40 && t <= 43);
}

// Направление конвейера
static void conveyor_delta(uint8_t t, int& dx, int& dy) {
    dx = dy = 0;
    switch (t) {
        case 24: case 28: case 40: dy = -1; break;  // вверх
        case 25: case 29: case 41: dy =  1; break;  // вниз
        case 26: case 42: dx = -1; break;           // влево
        case 27: case 31: case 43: dx =  1; break;  // вправо
        case 30: dx = 1; break;  // ID 30 = ВПРАВО
    }
}

// Проверка на смертельные тайлы С УЧЁТОМ НАПРАВЛЕНИЯ
static bool is_deadly_tile_with_dir(uint8_t t, int dx, int dy) {
    // Конвейеры-шипы (30, 31, 32)
    if (t == 30 || t == 31 || t == 32) {
        if (dx == 0 && dy == 0) return false;
        if (dy == -1) return false;
        if (dy == 1) return true;
        int cdx = 0, cdy = 0;
        conveyor_delta(t, cdx, cdy);
        if (dx == -1 && cdx == 1) return true;
        if (dx == 1 && cdx == 1) return false;
    }
    if (t >= 46 && t <= 48) return true;
    return false;
}

// ============================================================
enum GameState { STATE_TITLE, STATE_PLAYING, STATE_CLEAR, STATE_DEAD, STATE_WIN };

struct Level {
    uint8_t tiles[MAP_ROWS][MAP_COLS];
    int     R;
    int     spawn_x, spawn_y;
    bool    egg_mode;
};

struct Player {
    int   tx, ty;
    int   px, py;
    int   from_px, from_py;
    int   ak;
    float ak_timer;
    int   face;
    int   walk_frame;
    float walk_timer;
    float idle_timer;
    bool  idle_anim;
    bool  dead;
    int   death_frame;
    float death_timer;
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
    float     level_timer;
    int       best_time;
};
static Game g_game;

// SDL
static SDL_Window*   g_win = nullptr;
static SDL_Renderer* g_ren = nullptr;
static SDL_Texture  *tex_tiles, *tex_up, *tex_down, *tex_left, *tex_right;
static SDL_Texture  *tex_idle, *tex_death;
static SDL_Texture  *tex_cv_up, *tex_cv_down, *tex_cv_left, *tex_cv_right;
static SDL_Texture  *tex_finish, *tex_title, *tex_end, *tex_cleared, *tex_numbers;

// Музыка (WAV)
static Mix_Chunk *mus_title_chunk = nullptr;
static Mix_Chunk *mus_clear_chunk = nullptr;
static Mix_Chunk *mus_death_chunk = nullptr;
static Mix_Chunk *mus_end_chunk   = nullptr;
static int music_channel = -1;

// ============================================================
static void play_music(Mix_Chunk* chunk) {
    if (music_channel != -1) {
        Mix_HaltChannel(music_channel);
    }
    if (chunk) {
        music_channel = Mix_PlayChannel(-1, chunk, -1);
    }
}

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
static bool load_level(Game& g, int idx) {
    FILE* f = fopen(g.level_files[idx].c_str(),"rb");
    if (!f) return false;
    uint8_t buf[260]; fread(buf,1,260,f); fclose(f);

    Level& lv = g.level;
    memset(&lv,0,sizeof(lv));
    lv.egg_mode = (g.level_files[idx].find("egg") != std::string::npos);
    const uint8_t* raw = buf+4;

    for (int row=0;row<MAP_ROWS;row++) {
        for (int col=0;col<MAP_COLS;col++) {
            uint8_t t = raw[row*MAP_COLS+col];
            lv.tiles[row][col] = t;

            if (t == T_SPAWN) {
                lv.spawn_x = col;
                lv.spawn_y = row;
                lv.tiles[row][col] = T_FLOOR;
            }
            else if (t == T_CARROT) {
                lv.R++;
            }
            else if (lv.egg_mode && t == T_EGG) {
                lv.R++;
            }
        }
    }
    return true;
}

// ============================================================
static void init_player(Game& g) {
    Player& p = g.player;
    p.tx = g.level.spawn_x;
    p.ty = g.level.spawn_y;
    p.px = p.tx * TILE_SIZE + 8;
    p.py = p.ty * TILE_SIZE + 8;
    p.from_px = p.px; p.from_py = p.py;
    p.ak = 0; p.ak_timer = 0;
    p.face = 1;
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
    g.level_timer = 0;
}

// ============================================================
static bool try_teleport(Game& g) {
    Player& p = g.player;
    Level&  lv = g.level;
    
    for (int row = 0; row < MAP_ROWS; row++) {
        for (int col = 0; col < MAP_COLS; col++) {
            if (lv.tiles[row][col] == T_TELEPORT && (row != p.ty || col != p.tx)) {
                p.tx = col;
                p.ty = row;
                p.px = col * TILE_SIZE + 8;
                p.py = row * TILE_SIZE + 8;
                p.from_px = p.px;
                p.from_py = p.py;
                p.ak = 0;
                return true;
            }
        }
    }
    return false;
}

// ============================================================
static void on_arrive(Game& g) {
    Player& p = g.player;
    Level&  lv = g.level;
    uint8_t t = lv.tiles[p.ty][p.tx];

    int dx = 0, dy = 0;
    if (p.from_px != p.px || p.from_py != p.py) {
        dx = (p.px - p.from_px) / TILE_SIZE;
        dy = (p.py - p.from_py) / TILE_SIZE;
    }
    
    // ПРОВЕРКА НА СМЕРТЬ
    if (is_deadly_tile_with_dir(t, dx, dy)) {
        p.dead = true;
        p.death_timer = 0;
        g.state = STATE_DEAD;
        g.state_timer = 0;
        return;
    }

    // Телепорт
    if (t == T_TELEPORT) {
        if (try_teleport(g)) return;
    }

    // Морковка или яйцо
    if (t == T_CARROT || t == T_EGG) {
        lv.tiles[p.ty][p.tx] = T_EMPTY;
        lv.R--;
    }

    // Выход
    if (t == T_EXIT && lv.R == 0) {
        g.state = STATE_CLEAR; 
        g.state_timer = 0;
        int current_time = (int)g.level_timer;
        if (current_time < g.best_time || g.best_time == 0) {
            g.best_time = current_time;
        }
        return;
    }

    // Конвейер
    if (!p.dead && p.ak == 0 && tile_conveyor(t)) {
        int cdx, cdy; 
        conveyor_delta(t, cdx, cdy);
        if (cdx || cdy) {
            int nx = p.tx + cdx, ny = p.ty + cdy;
            if (nx >= 0 && nx < MAP_COLS && ny >= 0 && ny < MAP_ROWS && 
                !tile_solid(lv.tiles[ny][nx])) {
                p.from_px = p.px; 
                p.from_py = p.py;
                p.tx = nx; 
                p.ty = ny;
                p.px = p.tx * TILE_SIZE + 8; 
                p.py = p.ty * TILE_SIZE + 8;
                p.ak = 16; 
                p.ak_timer = 0;
                if (cdx > 0) p.face = 3; 
                else if (cdx < 0) p.face = 2;
                else if (cdy < 0) p.face = 0; 
                else p.face = 1;
            }
        }
    }
}

// ============================================================
static bool handle_input(Game& g) {
    Player& p = g.player;
    Level&  lv = g.level;
    if (p.ak != 0 || p.dead) return false;

    int dx=0, dy=0;
    if      (p.key_up)    dy=-1;
    else if (p.key_down)  dy=1;
    else if (p.key_left)  dx=-1;
    else if (p.key_right) dx=1;
    else return false;

    int nx=p.tx+dx, ny=p.ty+dy;
    if (nx<0||nx>=MAP_COLS||ny<0||ny>=MAP_ROWS) return false;
    uint8_t dest = lv.tiles[ny][nx];
    
    if (tile_solid(dest)) return false;
    if (dest == T_EGG) return false;
    
    if (dest >= 46 && dest <= 48) {
        p.dead = true;
        p.death_timer = 0;
        g.state = STATE_DEAD;
        g.state_timer = 0;
        return false;
    }

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
static void update(Game& g, float dt) {
    if (g.state == STATE_PLAYING) {
        g.level_timer += dt;
    }
    
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

    g.conv_anim_timer+=dt;
    if (g.conv_anim_timer>=CONV_SPD){g.conv_anim_timer=0;g.conv_anim_frame=(g.conv_anim_frame+1)%4;}
    g.finish_anim_timer+=dt;
    if (g.finish_anim_timer>=0.15f){g.finish_anim_timer=0;g.finish_anim_frame=(g.finish_anim_frame+1)%4;}

    Player& p=g.player;
    if (p.dead) return;

    if (p.ak > 0) {
        p.ak_timer += dt;
        float steps = p.ak_timer / (STEP_TIME / 16.0f);
        int new_ak = 16 - (int)steps;
        if (new_ak < 0) new_ak = 0;

        if (new_ak == 0 && p.ak > 0) {
            p.ak = 0;
            on_arrive(g);
        } else {
            p.ak = new_ak;
        }

        p.walk_timer += dt;
        if (p.walk_timer >= ANIM_SPD) { p.walk_timer=0; p.walk_frame=(p.walk_frame+1)%9; }
    }

    if (p.ak == 0 && !p.dead) {
        p.idle_timer += dt;
        if (p.idle_timer >= IDLE_TIME) p.idle_anim = true;
    }

    if (p.ak == 0) {
        handle_input(g);
    }
}

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

    float t_lerp = (p.ak > 0) ? (float)(16 - p.ak) / 16.0f : 1.0f;
    float vis_x = (p.from_px + (p.px - p.from_px) * t_lerp - 8);
    float vis_y = (p.from_py + (p.py - p.from_py) * t_lerp - 8);

    float cx = vis_x * SCALE - SCREEN_W/2.0f + SCALED_TILE/2.0f;
    float cy = vis_y * SCALE - SCREEN_H/2.0f + SCALED_TILE/2.0f;
    int mpw=MAP_COLS*SCALED_TILE, mph=MAP_ROWS*SCALED_TILE;
    if (cx<0) cx=0; if (cy<0) cy=0;
    if (cx>mpw-SCREEN_W) cx=(float)(mpw-SCREEN_W);
    if (cy>mph-SCREEN_H) cy=(float)(mph-SCREEN_H);
    if (mpw<SCREEN_W) cx=(mpw-SCREEN_W)/2.0f;
    if (mph<SCREEN_H) cy=(mph-SCREEN_H)/2.0f;

    for (int row=0;row<MAP_ROWS;row++) {
        for (int col=0;col<MAP_COLS;col++) {
            int sx=(int)(col*SCALED_TILE - cx);
            int sy=(int)(row*SCALED_TILE - cy);
            if (sx+SCALED_TILE<0||sx>SCREEN_W||sy+SCALED_TILE<0||sy>SCREEN_H) continue;
            uint8_t tile = lv.tiles[row][col];
            if (tile==0) continue;

            if (!tile_solid(tile)) draw_tile(T_FLOOR, sx, sy);

            if (tile==T_FLOOR || tile==T_EMPTY) {
                // уже нарисован пол
            } else if (tile==T_CARROT) {
                draw_tile(T_CARROT, sx, sy);
            } else if (tile==T_EGG) {
                draw_tile(T_EGG, sx, sy);
            } else if (tile==T_EXIT) {
                if (lv.R == 0 && tex_finish) {
                    SDL_Rect src={g.finish_anim_frame*TILE_SIZE,0,TILE_SIZE,TILE_SIZE};
                    SDL_Rect dst={sx,sy,SCALED_TILE,SCALED_TILE};
                    SDL_RenderCopy(g_ren,tex_finish,&src,&dst);
                } else {
                    draw_tile(tile, sx, sy);
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
            } else {
                draw_tile(tile, sx, sy);
            }
        }
    }

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
            bt=tex_idle; fw=18; fh=25; fr=1;
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

    SDL_SetRenderDrawBlendMode(g_ren,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ren,0,0,0,170);
    SDL_Rect hb={0,0,SCREEN_W,30}; SDL_RenderFillRect(g_ren,&hb);
    SDL_SetRenderDrawBlendMode(g_ren,SDL_BLENDMODE_NONE);

    draw_num(g.current_level+1, 10, 8);

    int mins = (int)g.level_timer / 60;
    int secs = (int)g.level_timer % 60;
    int time_x = SCREEN_W / 2 - 35;
    draw_num(mins / 10, time_x, 8);
    draw_num(mins % 10, time_x + 12, 8);
    SDL_SetRenderDrawColor(g_ren, 255, 255, 255, 255);
    SDL_Rect dot1 = {time_x + 26, 12, 3, 3};
    SDL_Rect dot2 = {time_x + 26, 20, 3, 3};
    SDL_RenderFillRect(g_ren, &dot1);
    SDL_RenderFillRect(g_ren, &dot2);
    draw_num(secs / 10, time_x + 32, 8);
    draw_num(secs % 10, time_x + 44, 8);

    draw_tile(T_CARROT, SCREEN_W-70, 4);
    draw_num(g.level.R, SCREEN_W-22, 8);

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

// ============================================================
int main(int argc, char* argv[]) {
    const char* adir=(argc>1)?argv[1]:"assets";

    SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO);
    IMG_Init(IMG_INIT_PNG);
    // Mix_Init не нужен для WAV файлов
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024);
    Mix_AllocateChannels(8);

    g_win=SDL_CreateWindow("Bobby Carrot", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    g_ren=SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");


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

 // Загружаем музыку (WAV)
    snprintf(p, 512, "%s/title.wav", adir);
    mus_title_chunk = Mix_LoadWAV(p);
    snprintf(p, 512, "%s/cleared.wav", adir);
    mus_clear_chunk = Mix_LoadWAV(p);
    snprintf(p, 512, "%s/death.wav", adir);
    mus_death_chunk = Mix_LoadWAV(p);
    snprintf(p, 512, "%s/end.wav", adir);
    mus_end_chunk = Mix_LoadWAV(p);

    // ========== ОТЛАДКА МУЗЫКИ ==========
    printf("=== MUSIC DEBUG ===\n");
    printf("title.wav:   %s\n", mus_title_chunk ? "OK" : "FAILED");
    printf("cleared.wav: %s\n", mus_clear_chunk ? "OK" : "FAILED");
    printf("death.wav:   %s\n", mus_death_chunk ? "OK" : "FAILED");
    printf("end.wav:     %s\n", mus_end_chunk   ? "OK" : "FAILED");
    printf("===================\n");

    memset(&g_game, 0, sizeof(g_game));
    build_levels(g_game, adir);
    g_game.state = STATE_TITLE;
    play_music(mus_title_chunk);

    bool run = true;
    Uint32 last = SDL_GetTicks();
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) run = false;
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                bool down = (e.type == SDL_KEYDOWN);
                Player& pp = g_game.player;
                switch (e.key.keysym.sym) {
                    case SDLK_UP:    case SDLK_w: pp.key_up    = down; break;
                    case SDLK_DOWN:  case SDLK_s: pp.key_down  = down; break;
                    case SDLK_LEFT:  case SDLK_a: pp.key_left  = down; break;
                    case SDLK_RIGHT: case SDLK_d: pp.key_right = down; break;
                    case SDLK_ESCAPE: run = false; break;
                    case SDLK_r: if (down && g_game.state == STATE_PLAYING) start_level(g_game, g_game.current_level); break;
                    case SDLK_n: if (down) { int n = g_game.current_level + 1; if (n < g_game.total_levels) start_level(g_game, n); } break;
                    case SDLK_m: if (down) { if (music_channel != -1) { if (Mix_Paused(music_channel)) Mix_Resume(music_channel); else Mix_Pause(music_channel); } } break;
                    case SDLK_RETURN: case SDLK_SPACE:
                        if (down) {
                            if (g_game.state == STATE_TITLE) start_level(g_game, 0);
                            else if (g_game.state == STATE_WIN) {
                                g_game.state = STATE_TITLE;
                                play_music(mus_title_chunk);
                            }
                        }
                        break;
                    default: break;
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        last = now;
        update(g_game, dt);

        static GameState prev = (GameState)-1;
        if (g_game.state != prev) {
            prev = g_game.state;
            switch (g_game.state) {
                case STATE_TITLE: play_music(mus_title_chunk); break;
                case STATE_CLEAR: play_music(mus_clear_chunk); break;
                case STATE_DEAD:  play_music(mus_death_chunk); break;
                case STATE_WIN:   play_music(mus_end_chunk);   break;
                default: break;
            }
        }
        render(g_game);
    }

    if (music_channel != -1) Mix_HaltChannel(music_channel);
    if (mus_title_chunk) Mix_FreeChunk(mus_title_chunk);
    if (mus_clear_chunk) Mix_FreeChunk(mus_clear_chunk);
    if (mus_death_chunk) Mix_FreeChunk(mus_death_chunk);
    if (mus_end_chunk)   Mix_FreeChunk(mus_end_chunk);

    Mix_CloseAudio();
    Mix_Quit();
    SDL_DestroyRenderer(g_ren);
    SDL_DestroyWindow(g_win);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
