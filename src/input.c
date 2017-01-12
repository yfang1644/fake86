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

/* input.c: functions for translation of SDL scancodes to BIOS scancodes,
   and handling of SDL events in general. */

#include <SDL/SDL.h>
#include <stdint.h>

extern uint32_t usegrabmode;

extern void doirq (uint8_t irqnum);
extern uint8_t running, portram[0x10000];
extern SDL_Surface *screen;

uint8_t translatescancode (uint16_t keyval) {
	switch (keyval) {
    case 0x1B:
        return (1); //Esc
    case 0x30:
        return (0xB); //zero
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
        return (keyval - 0x2F); //other number keys
    case 0x2D:
        return (0xC); //-_
    case 0x3D:
        return (0xD); //=+
    case 0x8:
        return (0xE); //backspace
    case 0x9:
        return (0xF); //tab
    case 0x71:
        return (0x10);
    case 0x77:
        return (0x11);
    case 0x65:
        return (0x12);
    case 0x72:
        return (0x13);
    case 0x74:
        return (0x14);
    case 0x79:
        return (0x15);
    case 0x75:
        return (0x16);
    case 0x69:
        return (0x17);
    case 0x6F:
        return (0x18);
    case 0x70:
        return (0x19);
    case 0x5B:
        return (0x1A);
    case 0x5D:
        return (0x1B);
    case 0xD:
    case 0x10F:
        return (0x1C); //enter
    case 0x131:
    case 0x132:
        return (0x1D); //ctrl
    case 0x61:
        return (0x1E);
    case 0x73:
        return (0x1F);
    case 0x64:
        return (0x20);
    case 0x66:
        return (0x21);
    case 0x67:
        return (0x22);
    case 0x68:
        return (0x23);
    case 0x6A:
        return (0x24);
    case 0x6B:
        return (0x25);
    case 0x6C:
        return (0x26);
    case 0x3B:
        return (0x27);
    case 0x27:
        return (0x28);
    case 0x60:
        return (0x29);
    case 0x130:
        return (0x2A); //left shift
    case 0x5C:
        return (0x2B);
    case 0x7A:
        return (0x2C);
    case 0x78:
        return (0x2D);
    case 0x63:
        return (0x2E);
    case 0x76:
        return (0x2F);
    case 0x62:
        return (0x30);
    case 0x6E:
        return (0x31);
    case 0x6D:
        return (0x32);
    case 0x2C:
        return (0x33);
    case 0x2E:
        return (0x34);
    case 0x2F:
        return (0x35);
    case 0x12F:
        return (0x36); //right shift
    case 0x13C:
        return (0x37); //print screen
    case 0x133:
    case 0x134:
        return (0x38); //alt
    case 0x20:
        return (0x39); //space
    case 0x12D:
        return (0x3A); //caps lock
    case 0x11A:
    case 0x11B:
    case 0x11C:
    case 0x11D:
    case 0x11E:
    case 0x11F:
    case 0x120:
    case 0x121:
    case 0x122:
    case 0x123:
        return (keyval - 0x11A + 0x3B); //F1 to F10
    case 0x12C:
        return (0x45); //num lock
    case 0x12E:
        return (0x46); //scroll lock
    case 0x116:
    case 0x107:
        return (0x47); //home
    case 0x111:
    case 0x108:
        return (0x48); //up
    case 0x118:
    case 0x109:
        return (0x49); //pgup
    case 0x10D:
        return (0x4A); //keypad -
    case 0x114:
    case 0x104:
        return (0x4B); //left
    case 0x105:
        return (0x4C); //center
    case 0x113:
    case 0x106:
        return (0x4D); //right
    case 0x10E:
        return (0x4E); //keypad +
    case 0x117:
    case 0x101:
        return (0x4F); //end
    case 0x112:
    case 0x102:
        return (0x50); //down
    case 0x119:
    case 0x103:
        return (0x51); //pgdn
    case 0x115:
    case 0x100:
        return (0x52); //ins
    case 0x7F:
    case 0x10A:
        return (0x53); //del
    default:
        return (0);
    }
}

extern void sermouseevent (uint8_t buttons, int8_t xrel, int8_t yrel);
extern void setwindowtitle (uint8_t *extra);

void mousegrabtoggle()
{
    if (usegrabmode == SDL_GRAB_ON) {
        usegrabmode = SDL_GRAB_OFF;
        SDL_WM_GrabInput (usegrabmode);
        SDL_ShowCursor (SDL_ENABLE);
        setwindowtitle ("");
    } else {
        usegrabmode = SDL_GRAB_ON;
        SDL_WM_GrabInput (usegrabmode);
        SDL_ShowCursor (SDL_DISABLE);
        setwindowtitle (" (press Ctrl + Alt to release mouse)");
    }
}

extern uint8_t scrmodechange;
extern uint32_t usefullscreen;
void handleinput()
{
    SDL_Event event;
    static uint8_t keydown[0x100];
    uint8_t buttons = 0;
    int mx = 0, my = 0;
    uint8_t tempbuttons;

    if(!SDL_PollEvent(&event))
        return;

    switch (event.type) {
    case SDL_KEYDOWN:
        portram[0x60] = translatescancode (event.key.keysym.sym);
        portram[0x64] |= 2;
        doirq (1);
        //printf("%02X\n", translatescancode(event.key.keysym.sym));
        keydown[translatescancode (event.key.keysym.sym) ] = 1;
        if (keydown[0x38] && keydown[0x1D] && (SDL_WM_GrabInput (SDL_GRAB_QUERY) == SDL_GRAB_ON) ) {
            keydown[0x1D] = 0;
            keydown[0x32] = 0;
            mousegrabtoggle();
            break;
        }
        if (keydown[0x38] && keydown[0x1C]) {
            keydown[0x1D] = 0;
            keydown[0x38] = 0;
            if (usefullscreen) usefullscreen = 0;
            else usefullscreen = SDL_FULLSCREEN;
            scrmodechange = 1;
            break;
        }
        break;
    case SDL_KEYUP:
        portram[0x60] = translatescancode (event.key.keysym.sym) | 0x80;
        portram[0x64] |= 2;
        doirq (1);
        keydown[translatescancode (event.key.keysym.sym) ] = 0;
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (SDL_WM_GrabInput (SDL_GRAB_QUERY) == SDL_GRAB_OFF) {
            mousegrabtoggle();
            break;
        }
        tempbuttons = SDL_GetMouseState (NULL, NULL);
        if (tempbuttons & 1) buttons = 2;
        else buttons = 0;
        if (tempbuttons & 4) buttons |= 1;
        sermouseevent (buttons, 0, 0);
        break;
    case SDL_MOUSEBUTTONUP:
        if (SDL_WM_GrabInput (SDL_GRAB_QUERY) == SDL_GRAB_OFF) break;
        tempbuttons = SDL_GetMouseState (NULL, NULL);
        if (tempbuttons & 1) buttons = 2;
        else buttons = 0;
        if (tempbuttons & 4) buttons |= 1;
        sermouseevent (buttons, 0, 0);
        break;
    case SDL_MOUSEMOTION:
        if (SDL_WM_GrabInput (SDL_GRAB_QUERY) == SDL_GRAB_OFF) break;
        SDL_GetRelativeMouseState (&mx, &my);
        sermouseevent (buttons, (int8_t) mx, (int8_t) my);
        SDL_WarpMouse (screen->w / 2, screen->h / 2);
        while (1) {
            SDL_PollEvent (&event);
            SDL_GetRelativeMouseState (&mx, &my);
            if ( (mx == 0) && (my == 0) ) break;
        }
        break;
    case SDL_QUIT:
        running = 0;
        break;
    default:
        break;
    }
}
