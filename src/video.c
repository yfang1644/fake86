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

/* video.c: many various functions to emulate bits of the video controller.
   a lot of this code is inefficient, and just plain ugly. i plan to rework
   large sections of it soon. */

#include <SDL/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include "mutex.h"
#include "cpu.h"
#include "ports.h"

#ifdef __BIG_ENDIAN__
#define rgb(r,g,b)      ((r<<24) | (g<<16) | (b<<8))
#else
#define rgb(r,g,b)      (r | (g<<8) | (b<<16))
#endif

extern SDL_Surface *screen;
extern uint8_t verbose;
extern union _bytewordregs_ regs;
extern uint8_t RAM[];   // 1MB
extern uint8_t portram[];    // 64KB
extern uint16_t segregs[4];

extern uint8_t read86 (uint32_t addr32);
extern uint8_t scrmodechange;

uint8_t VRAM[0x40000], vidmode, cgabg, blankattr, vidgfxmode, vidcolor;
uint16_t cursx, cursy, cols = 80, rows = 25, cursorposition, cursorvisible;
uint8_t updatedscreen, clocksafe, port3da;
uint16_t VGA_SC[0x100], VGA_CRTC[0x100], VGA_ATTR[0x100], VGA_GC[0x100];
uint32_t videobase= 0xB8000, textbase = 0xB8000;
uint32_t palettecga[16] = {
	rgb (0, 0, 0),
	rgb (0, 0, 0xAA),
	rgb (0, 0xAA, 0),
	rgb (0, 0xAA, 0xAA),
	rgb (0xAA, 0, 0),
	rgb (0xAA, 0, 0xAA),
	rgb (0xAA, 0x55, 0),
	rgb (0xAA, 0xAA, 0xAA),
	rgb (0x55, 0x55, 0x55),
	rgb (0x55, 0x55, 0xFF),
	rgb (0x55, 0xFF, 0x55),
	rgb (0x55, 0xFF, 0xFF),
	rgb (0xFF, 0x55, 0x55),
	rgb (0xFF, 0x55, 0xFF),
	rgb (0xFF, 0xFF, 0x55),
	rgb (0xFF, 0xFF, 0xFF)
};

uint32_t palettevga[256] = {
	rgb (0, 0, 0),
	rgb (0, 0, 169),
	rgb (0, 169, 0),
	rgb (0, 169, 169),
	rgb (169, 0, 0),
	rgb (169, 0, 169),
	rgb (169, 169, 0),
	rgb (169, 169, 169),
	rgb (0, 0, 84),
	rgb (0, 0, 255),
	rgb (0, 169, 84),
	rgb (0, 169, 255),
	rgb (169, 0, 84),
	rgb (169, 0, 255),
	rgb (169, 169, 84),
	rgb (169, 169, 255),
	rgb (0, 84, 0),
	rgb (0, 84, 169),
	rgb (0, 255, 0),
	rgb (0, 255, 169),
	rgb (169, 84, 0),
	rgb (169, 84, 169),
	rgb (169, 255, 0),
	rgb (169, 255, 169),
	rgb (0, 84, 84),
	rgb (0, 84, 255),
	rgb (0, 255, 84),
	rgb (0, 255, 255),
	rgb (169, 84, 84),
	rgb (169, 84, 255),
	rgb (169, 255, 84),
	rgb (169, 255, 255),
	rgb (84, 0, 0),
	rgb (84, 0, 169),
	rgb (84, 169, 0),
	rgb (84, 169, 169),
	rgb (255, 0, 0),
	rgb (255, 0, 169),
	rgb (255, 169, 0),
	rgb (255, 169, 169),
	rgb (84, 0, 84),
	rgb (84, 0, 255),
	rgb (84, 169, 84),
	rgb (84, 169, 255),
	rgb (255, 0, 84),
	rgb (255, 0, 255),
	rgb (255, 169, 84),
	rgb (255, 169, 255),
	rgb (84, 84, 0),
	rgb (84, 84, 169),
	rgb (84, 255, 0),
	rgb (84, 255, 169),
	rgb (255, 84, 0),
	rgb (255, 84, 169),
	rgb (255, 255, 0),
	rgb (255, 255, 169),
	rgb (84, 84, 84),
	rgb (84, 84, 255),
	rgb (84, 255, 84),
	rgb (84, 255, 255),
	rgb (255, 84, 84),
	rgb (255, 84, 255),
	rgb (255, 255, 84),
	rgb (255, 255, 255),
	rgb (255, 125, 125),
	rgb (255, 157, 125),
	rgb (255, 190, 125),
	rgb (255, 222, 125),
	rgb (255, 255, 125),
	rgb (222, 255, 125),
	rgb (190, 255, 125),
	rgb (157, 255, 125),
	rgb (125, 255, 125),
	rgb (125, 255, 157),
	rgb (125, 255, 190),
	rgb (125, 255, 222),
	rgb (125, 255, 255),
	rgb (125, 222, 255),
	rgb (125, 190, 255),
	rgb (125, 157, 255),
	rgb (182, 182, 255),
	rgb (198, 182, 255),
	rgb (218, 182, 255),
	rgb (234, 182, 255),
	rgb (255, 182, 255),
	rgb (255, 182, 234),
	rgb (255, 182, 218),
	rgb (255, 182, 198),
	rgb (255, 182, 182),
	rgb (255, 198, 182),
	rgb (255, 218, 182),
	rgb (255, 234, 182),
	rgb (255, 255, 182),
	rgb (234, 255, 182),
	rgb (218, 255, 182),
	rgb (198, 255, 182),
	rgb (182, 255, 182),
	rgb (182, 255, 198),
	rgb (182, 255, 218),
	rgb (182, 255, 234),
	rgb (182, 255, 255),
	rgb (182, 234, 255),
	rgb (182, 218, 255),
	rgb (182, 198, 255),
	rgb (0, 0, 113),
	rgb (28, 0, 113),
	rgb (56, 0, 113),
	rgb (84, 0, 113),
	rgb (113, 0, 113),
	rgb (113, 0, 84),
	rgb (113, 0, 56),
	rgb (113, 0, 28),
	rgb (113, 0, 0),
	rgb (113, 28, 0),
	rgb (113, 56, 0),
	rgb (113, 84, 0),
	rgb (113, 113, 0),
	rgb (84, 113, 0),
	rgb (56, 113, 0),
	rgb (28, 113, 0),
	rgb (0, 113, 0),
	rgb (0, 113, 28),
	rgb (0, 113, 56),
	rgb (0, 113, 84),
	rgb (0, 113, 113),
	rgb (0, 84, 113),
	rgb (0, 56, 113),
	rgb (0, 28, 113),
	rgb (56, 56, 113),
	rgb (68, 56, 113),
	rgb (84, 56, 113),
	rgb (97, 56, 113),
	rgb (113, 56, 113),
	rgb (113, 56, 97),
	rgb (113, 56, 84),
	rgb (113, 56, 68),
	rgb (113, 56, 56),
	rgb (113, 68, 56),
	rgb (113, 84, 56),
	rgb (113, 97, 56),
	rgb (113, 113, 56),
	rgb (97, 113, 56),
	rgb (84, 113, 56),
	rgb (68, 113, 56),
	rgb (56, 113, 56),
	rgb (56, 113, 68),
	rgb (56, 113, 84),
	rgb (56, 113, 97),
	rgb (56, 113, 113),
	rgb (56, 97, 113),
	rgb (56, 84, 113),
	rgb (56, 68, 113),
	rgb (80, 80, 113),
	rgb (89, 80, 113),
	rgb (97, 80, 113),
	rgb (105, 80, 113),
	rgb (113, 80, 113),
	rgb (113, 80, 105),
	rgb (113, 80, 97),
	rgb (113, 80, 89),
	rgb (113, 80, 80),
	rgb (113, 89, 80),
	rgb (113, 97, 80),
	rgb (113, 105, 80),
	rgb (113, 113, 80),
	rgb (105, 113, 80),
	rgb (97, 113, 80),
	rgb (89, 113, 80),
	rgb (80, 113, 80),
	rgb (80, 113, 89),
	rgb (80, 113, 97),
	rgb (80, 113, 105),
	rgb (80, 113, 113),
	rgb (80, 105, 113),
	rgb (80, 97, 113),
	rgb (80, 89, 113),
	rgb (0, 0, 64),
	rgb (16, 0, 64),
	rgb (32, 0, 64),
	rgb (48, 0, 64),
	rgb (64, 0, 64),
	rgb (64, 0, 48),
	rgb (64, 0, 32),
	rgb (64, 0, 16),
	rgb (64, 0, 0),
	rgb (64, 16, 0),
	rgb (64, 32, 0),
	rgb (64, 48, 0),
	rgb (64, 64, 0),
	rgb (48, 64, 0),
	rgb (32, 64, 0),
	rgb (16, 64, 0),
	rgb (0, 64, 0),
	rgb (0, 64, 16),
	rgb (0, 64, 32),
	rgb (0, 64, 48),
	rgb (0, 64, 64),
	rgb (0, 48, 64),
	rgb (0, 32, 64),
	rgb (0, 16, 64),
	rgb (32, 32, 64),
	rgb (40, 32, 64),
	rgb (48, 32, 64),
	rgb (56, 32, 64),
	rgb (64, 32, 64),
	rgb (64, 32, 56),
	rgb (64, 32, 48),
	rgb (64, 32, 40),
	rgb (64, 32, 32),
	rgb (64, 40, 32),
	rgb (64, 48, 32),
	rgb (64, 56, 32),
	rgb (64, 64, 32),
	rgb (56, 64, 32),
	rgb (48, 64, 32),
	rgb (40, 64, 32),
	rgb (32, 64, 32),
	rgb (32, 64, 40),
	rgb (32, 64, 48),
	rgb (32, 64, 56),
	rgb (32, 64, 64),
	rgb (32, 56, 64),
	rgb (32, 48, 64),
	rgb (32, 40, 64),
	rgb (44, 44, 64),
	rgb (48, 44, 64),
	rgb (52, 44, 64),
	rgb (60, 44, 64),
	rgb (64, 44, 64),
	rgb (64, 44, 60),
	rgb (64, 44, 52),
	rgb (64, 44, 48),
	rgb (64, 44, 44),
	rgb (64, 48, 44),
	rgb (64, 52, 44),
	rgb (64, 60, 44),
	rgb (64, 64, 44),
	rgb (60, 64, 44),
	rgb (52, 64, 44),
	rgb (48, 64, 44),
	rgb (44, 64, 44),
	rgb (44, 64, 48),
	rgb (44, 64, 52),
	rgb (44, 64, 60),
	rgb (44, 64, 64),
	rgb (44, 60, 64),
	rgb (44, 52, 64),
	rgb (44, 48, 64),
	rgb (0, 0, 0),
	rgb (0, 0, 0),
	rgb (0, 0, 0),
	rgb (0, 0, 0),
	rgb (0, 0, 0),
	rgb (0, 0, 0),
	rgb (0, 0, 0),
	rgb (0, 0, 0)
};

uint32_t usefullscreen = 0, usegrabmode = SDL_GRAB_OFF;

uint8_t latchRGB = 0, latchPal = 0, VGA_latch[4], stateDAC = 0;
uint8_t latchReadRGB = 0, latchReadPal = 0;
uint32_t tempRGB;
uint16_t oldw, oldh; //used when restoring screen mode

extern uint32_t nw, nh;

void vidinterrupt()
{
    uint32_t tempcalc, memloc, n;
    uint16_t *p, val;

    updatedscreen = 1;
    switch (regs.byteregs[regah]) {
        //what video interrupt function?
    case 0: //set video mode
        if (verbose) {
            printf ("Set video mode %02Xh\n", regs.byteregs[regal]);
        }
        VGA_SC[0x4] = 0; //VGA modes are in chained mode by default after a mode switch
        //regs.byteregs[regal] = 3;
        switch (regs.byteregs[regal] & 0x7F) {
        case 0: //40x25 mono text
            videobase = textbase;
            cols = 40;
            rows = 25;
            vidcolor = 0;
            vidgfxmode = 0;
            blankattr = 7;
            p = (uint16_t *)&RAM[videobase];
            val = (uint16_t)blankattr << 8;
            for (tempcalc = 0; tempcalc<8192; tempcalc++)
                *p++ = val;

            break;
        case 1: //40x25 color text
            videobase = textbase;
            cols = 40;
            rows = 25;
            vidcolor = 1;
            vidgfxmode = 0;
            blankattr = 7;
            p = (uint16_t *)&RAM[videobase];
            val = (uint16_t)blankattr << 8;
            for (tempcalc = 0; tempcalc<8192; tempcalc++)
                *p++ = val;
            portram[0x3D8] &= 0xFE;
            break;
        case 2: //80x25 mono text
            videobase = textbase;
            cols = 80;
            rows = 25;
            vidcolor = 1;
            vidgfxmode = 0;
            blankattr = 7;
            p = (uint16_t *)&RAM[videobase];
            val = (uint16_t)blankattr << 8;
            for (tempcalc = 0; tempcalc<8192; tempcalc++)
                *p++ = val;
            portram[0x3D8] &= 0xFE;
            break;
        case 3: //80x25 color text
            videobase = textbase;
            cols = 80;
            rows = 25;
            vidcolor = 1;
            vidgfxmode = 0;
            blankattr = 7;
            p = (uint16_t *)&RAM[videobase];
            val = (uint16_t)blankattr << 8;
            for (tempcalc = 0; tempcalc<8192; tempcalc++)
                *p++ = val;
            portram[0x3D8] &= 0xFE;
            break;
        case 4:
        case 5: //80x25 color text
            videobase = textbase;
            cols = 40;
            rows = 25;
            vidcolor = 1;
            vidgfxmode = 1;
            blankattr = 7;
            p = (uint16_t *)&RAM[videobase];
            val = (uint16_t)blankattr << 8;
            for (tempcalc = 0; tempcalc<8192; tempcalc++)
                *p++ = val;
            if (regs.byteregs[regal]==4) portram[0x3D9] = 48;
            else portram[0x3D9] = 0;
            break;
        case 6:
            videobase = textbase;
            cols = 80;
            rows = 25;
            vidcolor = 0;
            vidgfxmode = 1;
            blankattr = 7;
            p = (uint16_t *)&RAM[videobase];
            val = (uint16_t)blankattr << 8;
            for (tempcalc = 0; tempcalc<8192; tempcalc++)
                *p++ = val;
            portram[0x3D8] &= 0xFE;
            break;
        case 127:
            videobase = 0xB8000;
            cols = 90;
            rows = 25;
            vidcolor = 0;
            vidgfxmode = 1;
            p = (uint16_t *)&RAM[videobase];
            for (tempcalc = 0; tempcalc<8192; tempcalc++)
                *p++ = 0;
            portram[0x3D8] &= 0xFE;
            break;
        case 0x9: //320x200 16-color
            videobase = 0xB8000;
            cols = 40;
            rows = 25;
            vidcolor = 1;
            vidgfxmode = 1;
            blankattr = 0;
            if ( (regs.byteregs[regal]&0x80) ==0) {
                p = (uint16_t *)&RAM[videobase];
                val = (uint16_t)blankattr << 8;
                for (tempcalc = 0; tempcalc<8192; tempcalc++)
                    *p++ = val;
            }
            portram[0x3D8] &= 0xFE;
            break;
        case 0xD: //320x200 16-color
        case 0x12: //640x480 16-color
        case 0x13: //320x200 256-color
            videobase = 0xA0000;
            cols = 40;
            rows = 25;
            vidcolor = 1;
            vidgfxmode = 1;
            blankattr = 0;
            p = (uint16_t *)&RAM[videobase];
            val = (uint16_t)blankattr << 8;
            for (tempcalc = 0; tempcalc<32768; tempcalc++)
                *p++ = val;
            portram[0x3D8] &= 0xFE;
            break;
        }
        vidmode = regs.byteregs[regal] & 0x7F;
        RAM[0x449] = vidmode;
        RAM[0x44A] = (uint8_t) cols;
        RAM[0x44B] = 0;
        RAM[0x484] = (uint8_t) (rows - 1);
        cursx = 0;
        cursy = 0;
        if ( (regs.byteregs[regal] & 0x80) == 0x00) {
            memset (&RAM[0xA0000], 0, 0x1FFFF);
            memset (VRAM, 0, 262144);
        }
        switch (vidmode) {
        case 127: //hercules
            nw = oldw = 720;
            nh = oldh = 348;
            scrmodechange = 1;
            break;
        case 0x12:
            nw = oldw = 640;
            nh = oldh = 480;
            scrmodechange = 1;
            break;
        case 0x13:
            oldw = 640;
            oldh = 400;
            nw = 320;
            nh = 200;
            scrmodechange = 1;
            break;
        default:
            nw = oldw = 640;
            nh = oldh = 400;
            scrmodechange = 1;
            break;
        }
        break;
    case 0x10: //VGA DAC functions
        switch (regs.byteregs[regal]) {
        case 0x10: //set individual DAC register
            palettevga[getreg16 (regbx) ] = rgb((regs.byteregs[regdh] & 63) << 2, (regs.byteregs[regch] & 63) << 2, (regs.byteregs[regcl] & 63) << 2);
            break;
        case 0x12: //set block of DAC registers
            memloc = segregs[reges]*16+getreg16 (regdx);
            for (n=getreg16 (regbx); n< (uint32_t) (getreg16 (regbx) +getreg16 (regcx) ); n++) {
                palettevga[n] = rgb(read86(memloc) << 2, read86(memloc + 1) << 2, read86(memloc + 2) << 2);
                memloc += 3;
            }
        }
        break;
    case 0x1A: //get display combination code (ps, vga/mcga)
        regs.byteregs[regal] = 0x1A;
        regs.byteregs[regbl] = 0x8;
        break;
    }
}

uint16_t vtotal = 0;
void outVGA (uint16_t portnum, uint8_t value)
{
    static uint8_t oldah, oldal;
    uint8_t flip3c0 = 0;
    updatedscreen = 1;
    switch (portnum) {
    case 0x3B8: //hercules support
        if ( ( (value & 2) == 2) && (vidmode != 127) ) {
            oldah = regs.byteregs[regah];
            oldal = regs.byteregs[regal];
            regs.byteregs[regah] = 0;
            regs.byteregs[regal] = 127;
            vidinterrupt();
            regs.byteregs[regah] = oldah;
            regs.byteregs[regal] = oldal;
        }
        if (value & 0x80) videobase = 0xB8000;
        else videobase = 0xB0000;
        break;
    case 0x3C0:
        if (flip3c0) {
            flip3c0 = 0;
            portram[0x3C0] = value & 255;
            return;
        } else {
            flip3c0 = 1;
            VGA_ATTR[portram[0x3C0]] = value & 255;
            return;
        }
    case 0x3C4: //sequence controller index
        portram[0x3C4] = value & 255;
        //if (portout16) VGA_SC[value & 255] = value >> 8;
        break;
    case 0x3C5: //sequence controller data
        VGA_SC[portram[0x3C4]] = value & 255;
        /*if (portram[0x3C4] == 2) {
        printf("VGA_SC[2] = %02X\n", value);
    }*/
        break;
    case 0x3D4: //CRT controller index
        portram[0x3D4] = value & 255;
        //if (portout16) VGA_CRTC[value & 255] = value >> 8;
        break;
    case 0x3C7: //color index register (read operations)
        latchReadPal = value & 255;
        latchReadRGB = 0;
        stateDAC = 0;
        break;
    case 0x3C8: //color index register (write operations)
        latchPal = value & 255;
        latchRGB = 0;
        tempRGB = 0;
        stateDAC = 3;
        break;
    case 0x3C9: //RGB data register
        value = value & 63;
        switch (latchRGB) {
#ifdef __BIG_ENDIAN__
        case 0: //red
            tempRGB = value << 26;
            break;
        case 1: //green
            tempRGB |= value << 18;
            break;
        case 2: //blue
            tempRGB |= value << 10;
            palettevga[latchPal] = tempRGB;
            latchPal = latchPal + 1;
            break;
#else
        case 0: //red
            tempRGB = value << 2;
            break;
        case 1: //green
            tempRGB |= value << 10;
            break;
        case 2: //blue
            tempRGB |= value << 18;
            palettevga[latchPal] = tempRGB;
            latchPal = latchPal + 1;
            break;
#endif   //__BIG_ENDIAN__
        }
        latchRGB = (latchRGB + 1) % 3;
        break;
    case 0x3D5: //cursor position latch
        VGA_CRTC[portram[0x3D4]] = value & 255;
        if (portram[0x3D4]==0xE) cursorposition = (cursorposition&0xFF) | (value<<8);
        else if (portram[0x3D4]==0xF) cursorposition = (cursorposition&0xFF00) |value;
        cursy = cursorposition/cols;
        cursx = cursorposition%cols;
        if (portram[0x3D4] == 6) {
            vtotal = value | ( ( (uint16_t) VGA_GC[7] & 1) << 8) | ( ( (VGA_GC[7] & 32) ? 1 : 0) << 9);
            //printf("Vertical total: %u\n", vtotal);
        }
        break;
    case 0x3CF:
        VGA_GC[portram[0x3CE]] = value;
        break;
    default:
        portram[portnum] = value;
    }
}

uint8_t inVGA (uint16_t portnum)
{
    switch (portnum) {
    case 0x3C1:
        return ( (uint8_t) VGA_ATTR[portram[0x3C0]]);
    case 0x3C5:
        return ( (uint8_t) VGA_SC[portram[0x3C4]]);
    case 0x3D5:
        return ( (uint8_t) VGA_CRTC[portram[0x3D4]]);
    case 0x3C7: //DAC state
        return (stateDAC);
    case 0x3C8: //palette index
        return (latchReadPal);
    case 0x3C9: //RGB data register
        switch (latchReadRGB++) {
#ifdef __BIG_ENDIAN__
        case 0: //blue
            return ( (palettevga[latchReadPal] >> 26) & 63);
        case 1: //green
            return ( (palettevga[latchReadPal] >> 18) & 63);
        case 2: //red
            latchReadRGB = 0;
            return ( (palettevga[latchReadPal++] >> 10) & 63);
#else
        case 0: //blue
            return ( (palettevga[latchReadPal] >> 2) & 63);
        case 1: //green
            return ( (palettevga[latchReadPal] >> 10) & 63);
        case 2: //red
            latchReadRGB = 0;
            return ( (palettevga[latchReadPal++] >> 18) & 63);
#endif  //__BIG_ENDIAN__
        }
        case 0x3DA:
        return (port3da);
		}
	return (portram[portnum]); //this won't be reached, but without it the compiler gives a warning
}

#define shiftVGA(value) {\
	for (cnt=0; cnt<(VGA_GC[3] & 7); cnt++) {\
		value = (value >> 1) | ((value & 1) << 7);\
	}\
}

#define logicVGA(curval, latchval) {\
	switch ((VGA_GC[3]>>3) & 3) {\
		case 1: curval &= latchval; break;\
		case 2: curval |= latchval; break;\
		case 3: curval ^= latchval; break;\
	}\
}

uint8_t lastmode = 0;
void writeVGA (uint32_t addr32, uint8_t value)
{
	uint32_t planesize = 0x10000;
	uint8_t curval, tempand, cnt;
    int i;

	updatedscreen = 1;
	//if (lastmode != VGA_GC[5] & 3) printf("write mode %u\n", VGA_GC[5] & 3);
    //lastmode = VGA_GC[5] & 3;
    switch (VGA_GC[5] & 3) {
        //get write mode
    case 0:
        shiftVGA (value);
        for(i = 0; i < 4; i++) {
            if (VGA_SC[2] & (1<<i)) {
                if (VGA_GC[1] & (1<<i)) {
                    if (VGA_GC[0] & (1<<i)) curval = 255;
                    else curval = 0;
                }
                else curval = value;
                logicVGA (curval, VGA_latch[i]);
                curval = (~VGA_GC[8] & curval) | (VGA_GC[8] & VGA_latch[i]);
                VRAM[addr32+planesize*i] = curval;
            }
        }
        break;
    case 1:
        for(i = 0; i < 4; i++) {
            if (VGA_SC[2] & (1<<i)) VRAM[addr32+planesize*i] = VGA_latch[i];
        }
        break;
    case 2:
        for(i = 0; i < 4; i++) {
            if (VGA_SC[2] & (1<<i)) {
                if (VGA_GC[1] & (1<<i)) {
                    if (value & (1<<i)) curval = 255;
                    else curval = 0;
                }
                else curval = value;
                logicVGA (curval, VGA_latch[i]);
                curval = (~VGA_GC[8] & curval) | (VGA_GC[8] & VGA_latch[i]);
                VRAM[addr32+planesize*i] = curval;
            }
        }
        break;
    case 3:
        tempand = value & VGA_GC[8];
        shiftVGA (value);
        for(i = 0; i < 4; i++) {
            if (VGA_SC[2] & (1<<i)) {
                if (VGA_GC[0] & (1<<i)) curval = 255;
                else curval = 0;
                //logicVGA (curval, VGA_latch[i]);
                curval = (~tempand & curval) | (tempand & VGA_latch[i]);
                VRAM[addr32+planesize*i] = curval;
            }
        }
        break;
    }
}

uint8_t readVGA (uint32_t addr32)
{
	uint32_t planesize = 0x10000;
    int i;
    for (i = 0; i < 4; i++) {
	    VGA_latch[i] = VRAM[addr32+planesize*i];
    }

    for (i = 0; i < 4; i++) {
        if (VGA_SC[2] & (1<<i)) return (VRAM[addr32+planesize*i]);
    }

    return (0); //this won't be reached, but without it some compilers give a warning
}

void initVideoPorts()
{
    set_port_write_redirector (0x3B0, 0x3DA, &outVGA);
    set_port_read_redirector (0x3B0, 0x3DA, &inVGA);
}
