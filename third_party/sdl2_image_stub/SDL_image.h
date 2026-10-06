#pragma once

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMG_INIT_JPG 0x00000001
#define IMG_INIT_PNG 0x00000002
#define IMG_INIT_TIF 0x00000004
#define IMG_INIT_WEBP 0x00000008
#define IMG_INIT_JXL 0x00000010
#define IMG_INIT_AVIF 0x00000020

int IMG_Init(int flags);
void IMG_Quit(void);
SDL_Surface* IMG_Load(const char* file);
SDL_Texture* IMG_LoadTexture(SDL_Renderer* renderer, const char* file);
int IMG_SavePNG(SDL_Surface* surface, const char* file);

#define IMG_GetError SDL_GetError

#ifdef __cplusplus
}
#endif
