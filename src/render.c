/*
  Fake86: A portable, open-source 8086 PC emulator.
  Copyright (C)2010-2013 Mike Chambers

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/* render.c: functions for SDL initialization, as well as video scaling/rendering.
   it is a bit messy. i plan to rework much of this in the future. i am also
   going to add hardware accelerated scaling soon. */

#include <SDL/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include "mutex.h"
#include "ports.h"

#ifdef _WIN32
CRITICAL_SECTION screenmutex;
#else
pthread_t vidthread;
pthread_mutex_t screenmutex = PTHREAD_MUTEX_INITIALIZER;
#endif

SDL_Surface *screen = NULL;
uint32_t *scalemap = NULL;
uint8_t regenscalemap = 1;

extern uint8_t RAM[];
extern uint8_t VRAM[], vidmode, cgabg, vidgfxmode, vidcolor, running;
extern uint16_t cursx, cursy, cols, rows, cursorvisible;
extern uint8_t updatedscreen;
extern uint16_t VGA_SC[0x100], VGA_CRTC[0x100], VGA_ATTR[0x100], VGA_GC[0x100];
extern uint32_t videobase;
extern uint32_t palettecga[16], palettevga[256];
extern uint32_t usefullscreen, usegrabmode;

uint8_t fontcga[4096];
uint64_t totalframes = 0;
uint32_t framedelay = 20;
uint8_t scrmodechange = 0, noscale = 0, nosmooth = 1, renderbenchmark = 0, doaudio = 1;
char windowtitle[128];

void setwindowtitle (char *extra)
{
    char temptext[128];
    sprintf (temptext, "%s%s", windowtitle, extra);
    SDL_WM_SetCaption ( (const char *) temptext, NULL);
}

void loadCGAfont(char *file)
{
    FILE *fontfile;
    fontfile = fopen (file, "rb");
    if (fontfile==NULL) {
        printf ("FATAL: Cannot open %s !\n", file);
        exit (1);
    }
    fread (fontcga, 1, 4096, fontfile);
    fclose (fontfile);
}

uint32_t prestretch[1024][1024];
uint32_t nw, nh; //native width and height, pre-stretching (i.e. 320x200 for mode 13h)
void createscalemap()
{
    uint32_t srcx, srcy, dstx, dsty, scalemapptr;
    double xscale, yscale;

    xscale = (double) nw / (double) screen->w;
    yscale = (double) nh / (double) screen->h;
    if (scalemap != NULL) free(scalemap);
    scalemap = (void *)malloc( ((uint32_t)screen->w + 1) * (uint32_t)screen->h * 4);
    if (scalemap == NULL) {
        printf("\nFATAL: Unable to allocate memory for scalemap!\n");
        exit(1);
    }
    scalemapptr = 0;
    for (dsty=0; dsty<(uint32_t)screen->h; dsty++) {
        srcy = (uint32_t) ( (double) dsty * yscale);
        scalemap[scalemapptr++] = srcy;
        for (dstx=0; dstx<(uint32_t)screen->w; dstx++) {
            srcx = (uint32_t) ( (double) dstx * xscale);
            scalemap[scalemapptr++] = srcx;
        }
    }

    regenscalemap = 0;
}

extern uint16_t constantw, constanth;

#ifdef _WIN32
void ShowMenu();
void HideMenu();
#endif

void doscrmodechange()
{
    uint32_t  x = 640, y = 400;   // default screen size

    MutexLock (screenmutex);
    if (scrmodechange) {
        if (screen != NULL) SDL_FreeSurface (screen);
#ifdef _WIN32
        if (usefullscreen) HideMenu(); else ShowMenu();
#endif

        if (constantw && constanth){
            x = constantw; y = constanth;
        } else if (noscale) {
            x = nw; y = nh;
        } else {
            if ( (nw >= 640) || (nh >= 400) ) {
                x = nw; y = nh; 
            }
        }
        screen = SDL_SetVideoMode (x , y, 32, SDL_HWSURFACE | usefullscreen);

        if (usefullscreen) SDL_WM_GrabInput (SDL_GRAB_ON); //always have mouse grab turned on for full screen mode
        else SDL_WM_GrabInput (usegrabmode);
        SDL_ShowCursor (SDL_DISABLE);
        if (!usefullscreen) {
            if (usegrabmode == SDL_GRAB_ON) setwindowtitle (" (press Ctrl + Alt to release mouse)");
            else setwindowtitle ("");
        }
        regenscalemap = 1;
        createscalemap();
    }
    MutexUnlock (screenmutex);
    scrmodechange = 0;
}

void stretchblit (SDL_Surface *target)
{
    uint32_t srcx, srcy, dstx, dsty, lastx, lasty, r, g, b;
    uint32_t consecutivex, consecutivey = 0, limitx, limity, scalemapptr;
    uint32_t ofs;
    uint8_t *pixelrgb;

    limitx = (uint32_t)((double) nw / (double) target->w);
    limity = (uint32_t)((double) nh / (double) target->h);

    if (SDL_MUSTLOCK (target) )
        if (SDL_LockSurface (target) < 0)
            return;

    lasty = 0;
    scalemapptr = 0;
    for (dsty=0; dsty<(uint32_t)target->h; dsty++) {
        srcy = scalemap[scalemapptr++];
        ofs = dsty*target->w;
        consecutivex = 0;
        lastx = 0;
        if (srcy == lasty) consecutivey++;
        else consecutivey = 0;
        for (dstx=0; dstx<(uint32_t)target->w; dstx++) {
            srcx = scalemap[scalemapptr++];
            pixelrgb = (uint8_t *) &prestretch[srcy][srcx];
            r = *pixelrgb++;
            g = *pixelrgb++;
            b = *pixelrgb++;
            if (srcx == lastx) consecutivex++;
            else consecutivex = 0;
            if ( (consecutivex > limitx) && (consecutivey > limity) ) {
                pixelrgb = (uint8_t *) &prestretch[srcy][srcx+1];
                r += *pixelrgb++;
                g += *pixelrgb++;
                b += *pixelrgb++;
                pixelrgb = (uint8_t *) &prestretch[srcy+1][srcx];
                r += *pixelrgb++;
                g += *pixelrgb++;
                b += *pixelrgb++;
                pixelrgb = (uint8_t *) &prestretch[srcy+1][srcx+1];
                r += *pixelrgb++;
                g += *pixelrgb++;
                b += *pixelrgb++;
                r = r >> 2;
                g = g >> 2;
                b = b >> 2;
                //r = 255; g = 0; b = 0;
            }
            else if (consecutivex > limitx) {
                pixelrgb = (uint8_t *) &prestretch[srcy][srcx+1];
                r += *pixelrgb++;
                g += *pixelrgb++;
                b += *pixelrgb++;
                r = r >> 1;
                g = g >> 1;
                b = b >> 1;
                //r = 0; g = 255; b = 0;
            }
            else if (consecutivey > limity) {
                pixelrgb = (uint8_t *) &prestretch[srcy+1][srcx];
                r += *pixelrgb++;
                g += *pixelrgb++;
                b += *pixelrgb++;
                r = r >> 1;
                g = g >> 1;
                b = b >> 1;
                //r = 0; g = 0; b = 255;
            }
            ( (uint32_t *) target->pixels) [ofs++] = SDL_MapRGB (target->format, (uint8_t) r, (uint8_t) g, (uint8_t) b);
            lastx = srcx;
        }
        lasty = srcy;
    }

    if (SDL_MUSTLOCK (target) )
        SDL_UnlockSurface (target);
    SDL_UpdateRect (target, 0, 0, target->w, target->h);
}

void roughblit (SDL_Surface *target)
{
    uint32_t srcx, srcy, dstx, dsty, curcolor, scalemapptr;
    int32_t ofs;
    uint8_t *pixelrgb;

    if (SDL_MUSTLOCK (target) )
        if (SDL_LockSurface (target) < 0)
            return;

    scalemapptr = 0;
    for (dsty=0; dsty<(uint32_t)target->h; dsty++) {
        srcy = scalemap[scalemapptr++];
        ofs = dsty*target->w;
        for (dstx=0; dstx<(uint32_t)target->w; dstx++) {
            srcx = scalemap[scalemapptr++];
            pixelrgb = (uint8_t *) &prestretch[srcy][srcx];
            curcolor = SDL_MapRGB (target->format, pixelrgb[0], pixelrgb[1], pixelrgb[2]);
            ( (uint32_t *) target->pixels) [ofs++] = curcolor;
        }
    }

    if (SDL_MUSTLOCK (target) )
        SDL_UnlockSurface (target);
    SDL_UpdateRect (target, 0, 0, target->w, target->h);
}

/* NOTE: doubleblit is only used when smoothing is not enabled, and the SDL window size
is exactly double of native resolution for the current video mode. we can take
advantage of the fact that every pixel is simply doubled both horizontally and
vertically. this way, we do not need to waste mountains of CPU time doing
floating point multiplication for each and every on-screen pixel. it makes the
difference between games being smooth and playable, and being jerky on my old
400 MHz PowerPC G3 iMac.
*/
void doubleblit (SDL_Surface *target)
{
    uint32_t srcx, srcy, dstx, dsty, curcolor;
    int32_t ofs;
    uint8_t *pixelrgb;

    if (SDL_MUSTLOCK (target) )
        if (SDL_LockSurface (target) < 0)
    return;

    for (dsty=0; dsty<(uint32_t)target->h; dsty += 2) {
        srcy = (uint32_t) (dsty >> 1);
        ofs = dsty*target->w;
        for (dstx=0; dstx<(uint32_t)target->w; dstx += 2) {
            srcx = (uint32_t) (dstx >> 1);
            pixelrgb = (uint8_t *) &prestretch[srcy][srcx];
            curcolor = SDL_MapRGB (target->format, pixelrgb[0], pixelrgb[1], pixelrgb[2]);
            ( (uint32_t *) target->pixels) [ofs+target->w] = curcolor;
            ( (uint32_t *) target->pixels) [ofs++] = curcolor;
            ( (uint32_t *) target->pixels) [ofs+target->w] = curcolor;
            ( (uint32_t *) target->pixels) [ofs++] = curcolor;
        }
    }

    if (SDL_MUSTLOCK (target) )
        SDL_UnlockSurface (target);
    SDL_UpdateRect (target, 0, 0, target->w, target->h);
}

extern uint16_t vtotal;
void draw ()
{
    uint32_t planemode, vgapage, color, chary, charx, vidptr, divx, divy, curchar, curpixel, usepal, intensity, blockw, curheight;
    uint32_t x, y, x1, y1;
    uint8_t bytecolor;
    uint8_t vgaport;

    switch (vidmode) {
    case 0:
    case 1:
    case 2: //text modes
    case 3:
    case 7:
    case 0x82:
        nw = 640;
        nh = 400;
        vgapage = ( (uint32_t) VGA_CRTC[0xC]<<8) + (uint32_t) VGA_CRTC[0xD];
        divx = (cols == 80) ? 1: 2;
        for (y=0; y<nh; y++){
            for (x=0; x<nw; x++) {
                charx = x/8/divx;
                if ( (portin(0x3D8)==9) && (portin(0x3D4)==9) ) {
                    chary = y/4;
                    vidptr = vgapage + videobase + chary*cols*2 + charx*2;
                    curchar = RAM[vidptr];
                    bytecolor = fontcga[curchar*16 + (y%4)];
                } else {
                    chary = y/16;
                    vidptr = videobase + chary*cols*2 + charx*2;
                    curchar = RAM[vidptr];
                    bytecolor = fontcga[curchar*16 + y%16];
                }
                color = (bytecolor << ((x/divx)&7) ) & 0x80;
                if (vidcolor) {
                    /*if (!color) if (portin(0x3D8)&128) color = palettecga[ (RAM[vidptr+1]/16) &7];
                    else*/ if (!color) color = palettecga[RAM[vidptr+1]/16]; //high intensity background
                    else color = palettecga[RAM[vidptr+1]&15];
                } else {
                    if ( (RAM[vidptr+1] & 0x70) ) {
                        if (!color) color = palettecga[7];
                        else color = palettecga[0];
                    } else {
                        if (!color) color = palettecga[0];
                        else color = palettecga[7];
                    }
                }
                prestretch[y][x] = color;
            }
        }
        break;
    case 4:
    case 5:
        nw = 320;
        nh = 200;
        vgaport = portin(0x3D9);
        usepal = (vgaport>>5) & 1;
        intensity = ( (vgaport>>4) & 1) << 3;
        for (y=0; y<nh; y++) {
            for (x=0; x<nw; x++) {
                charx = x;
                chary = y;
                vidptr = videobase + ( (chary>>1) * 80) + ( (chary & 1) * 8192) + (charx >> 2);
                curpixel = RAM[vidptr];
                switch (charx & 3) {
                case 3:
                    curpixel = curpixel & 3;
                    break;
                case 2:
                    curpixel = (curpixel>>2) & 3;
                    break;
                case 1:
                    curpixel = (curpixel>>4) & 3;
                    break;
                case 0:
                    curpixel = (curpixel>>6) & 3;
                    break;
                }
                if (vidmode==4) {
                    curpixel = curpixel * 2 + usepal + intensity;
                    if (curpixel == (usepal + intensity) )
                        curpixel = cgabg;
                } else {
                    curpixel = curpixel * 63;
                }
                color = palettecga[curpixel];
                prestretch[y][x] = color;
            }
        }
        break;
    case 6:
        nw = 640;
        nh = 200;
        for (y=0; y<nh; y++) {
            for (x=0; x<nw; x++) {
                charx = x;
                chary = y;
                vidptr = videobase + ( (chary>>1) * 80) + ( (chary&1) * 8192) + (charx>>3);
                curpixel = (RAM[vidptr]>> (7- (charx&7) ) ) &1;
                color = palettecga[curpixel*15];
                prestretch[2*y][x] = color;
                prestretch[2*y+1][x] = color;
            }
        }
        break;
    case 127:
        nw = 720;
        nh = 348;
        for (y=0; y<nh; y++) {
            for (x=0; x<nw; x++) {
                charx = x;
                chary = y>>1;
                vidptr = videobase + ( (y & 3) << 13) + (y >> 2) *90 + (x >> 3);
                curpixel = (RAM[vidptr]>> (7- (charx&7) ) ) &1;
#ifdef __BIG_ENDIAN__
                if (curpixel) color = 0xFFFFFF00;
#else
                if (curpixel) color = 0x00FFFFFF;
#endif
                else color = 0x00000000;
                prestretch[y][x] = color;
            }
        }
        break;
    case 0x8: //160x200 16-color (PCjr)
        nw = 640; //fix this
        nh = 400; //part later
        for (y=0; y<nh; y++) {
            for (x=0; x<nw; x++) {
                vidptr = 0xB8000 + (y>>2) *80 + (x>>3) + ( (y>>1) &1) *8192;
                if ( ( (x>>1) &1) ==0) color = palettecga[RAM[vidptr] >> 4];
                else color = palettecga[RAM[vidptr] & 15];
                prestretch[y][x] = color;
            }
        }
        break;
    case 0x9: //320x200 16-color (Tandy/PCjr)
        nw = 640; //fix this
        nh = 400; //part later
        for (y=0; y<nh; y++) {
            for (x=0; x<nw; x++) {
                vidptr = 0xB8000 + (y>>3) *160 + (x>>2) + ( (y>>1) &3) *8192;
                if ( ( (x>>1) &1) ==0) color = palettecga[RAM[vidptr] >> 4];
                else color = palettecga[RAM[vidptr] & 15];
                prestretch[y][x] = color;
            }
        }
        break;
    case 0xD:
    case 0xE:
        nw = 640; //fix this
        nh = 400; //part later
        for (y=0; y<nh; y++) {
            for (x=0; x<nw; x++) {
                divx = x>>1;
                divy = y>>1;
                vidptr = divy*40 + (divx>>3);
                x1 = 7 - (divx & 7);
                color = (VRAM[vidptr] >> x1) & 1;
                color |= ( ( (VRAM[0x10000 + vidptr] >> x1) & 1) << 1);
                color |= ( ( (VRAM[0x20000 + vidptr] >> x1) & 1) << 2);
                color |= ( ( (VRAM[0x30000 + vidptr] >> x1) & 1) << 3);
                prestretch[y][x] = palettevga[color];
            }
        }
        break;
    case 0x10:
        nw = 640;
        nh = 350;
        for (y=0; y<nh; y++) {
            for (x=0; x<nw; x++) {
                vidptr = y*80 + (x>>3);
                x1 = 7 - (x & 7);
                color = (VRAM[vidptr] >> x1) & 1;
                color |= ( ( (VRAM[0x10000 + vidptr] >> x1) & 1) << 1);
                color |= ( ( (VRAM[0x20000 + vidptr] >> x1) & 1) << 2);
                color |= ( ( (VRAM[0x30000 + vidptr] >> x1) & 1) << 3);
                prestretch[y][x] = palettevga[color];
            }
        }
        break;
    case 0x12:
        nw = 640;
        nh = 480;
        vgapage = ( (uint32_t) VGA_CRTC[0xC]<<8) + (uint32_t) VGA_CRTC[0xD];
        for (y=0; y<nh; y++) {
            for (x=0; x<nw; x++) {
                vidptr = y*80 + (x/8);
                color  = (VRAM[vidptr] >> (~x & 7) ) & 1;
                color |= ( (VRAM[vidptr+0x10000] >> (~x & 7) ) & 1) << 1;
                color |= ( (VRAM[vidptr+0x20000] >> (~x & 7) ) & 1) << 2;
                color |= ( (VRAM[vidptr+0x30000] >> (~x & 7) ) & 1) << 3;
                prestretch[y][x] = palettevga[color];
            }
        }
        break;
    case 0x13:
        if (vtotal == 11) {
            //ugly hack to show Flashback at the proper resolution
            nw = 256;
            nh = 224;
        } else {
            nw = 320;
            nh = 200;
        }
        if (VGA_SC[4] & 6) planemode = 1;
        else planemode = 0;
        //vgapage = ( (uint32_t) VGA_CRTC[0xC]<<8) + (uint32_t) VGA_CRTC[0xD];
        vgapage = (( (uint32_t) VGA_CRTC[0xC]<<8) + (uint32_t) VGA_CRTC[0xD]) << 2;
        for (y=0; y<nh; y++) {
            for (x=0; x<nw; x++) {
                if (!planemode) color = palettevga[RAM[videobase + ((vgapage + y*nw + x) & 0xFFFF) ]];
                //if (!planemode) color = palettevga[RAM[videobase + y*nw + x]];
                else {
                    vidptr = y*nw + x;
                    vidptr = vidptr/4 + (x & 3) *0x10000;
                    vidptr = vidptr + vgapage - (VGA_ATTR[0x13] & 15);
                    color = palettevga[VRAM[vidptr]];
                }
                prestretch[y][x] = color;
            }
        }
    }

    if (vidgfxmode==0) {
        if (cursorvisible) {
            curheight = 2;
            blockw = (cols==80) ? 8: 16;
            x1 = cursx * blockw;
            y1 = cursy * 8 + 8 - curheight;
            curpixel = RAM[videobase+cursy*cols*2+cursx*2+1]&15;
            for (y=y1*2; y<y1*2+curheight; y++) {
                for (x=x1; x<x1+blockw; x++) {
                    color = palettecga[curpixel];
                    prestretch[y&1023][x&1023] = color;
                }
            }
        }
    }
    if (nosmooth) {
        if ( ((nw << 1) == screen->w) && ((nh << 1) == screen->h) ) doubleblit (screen);
        else roughblit (screen);
    } else stretchblit (screen);
}

#ifdef _WIN32
void VideoThread (void *dummy)
#else
void *VideoThread (void *dummy)
#endif
{
    uint32_t cursorprevtick, cursorcurtick, delaycalc;
    cursorprevtick = SDL_GetTicks();
    cursorvisible = 0;
    int *run = (int *)dummy;

    while (*run) {
        cursorcurtick = SDL_GetTicks();
        if ( (cursorcurtick - cursorprevtick) >= 250) {
            updatedscreen = 1;
            cursorvisible = ~cursorvisible & 1;
            cursorprevtick = cursorcurtick;
        }

        if (updatedscreen || renderbenchmark) {
            updatedscreen = 0;
            if (screen != NULL) {
                MutexLock (screenmutex);
                if (regenscalemap) createscalemap();
                draw();
                MutexUnlock (screenmutex);
            }
            totalframes++;
        }
        if (!renderbenchmark) {
            delaycalc = framedelay - (SDL_GetTicks() - cursorcurtick);
            if (delaycalc > framedelay) delaycalc = framedelay;
            SDL_Delay (delaycalc);
        }
    }
#ifdef _WIN32
    return;
#else
    return NULL;
#endif
}

uint8_t initscreen (char *ver)
{
    if (doaudio) {
        if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) ) return (0);
    } else {
        if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_TIMER) ) return (0);
    }
    screen = SDL_SetVideoMode (640, 400, 32, SDL_HWSURFACE);
    if (screen == NULL) return (0);
    sprintf (windowtitle, "%s", ver);
    setwindowtitle ("");
    loadCGAfont(PATH_DATAFILES "font8x16.dat");
#ifdef _WIN32
    InitializeCriticalSection (&screenmutex);
    _beginthread (VideoThread, 0, &running);
#else
    pthread_create (&vidthread, NULL, (void *) VideoThread, &running);
#endif

    return (1);
}
