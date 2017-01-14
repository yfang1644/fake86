/*
  Fake86: A portable, open-source 8086 PC emulator.
  Copyright (C)2010-2012 Mike Chambers

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

#define PIT_MODE_LATCHCOUNT	(0b00000000)
#define PIT_MODE_LOBYTE	    (0b00010000)
#define PIT_MODE_HIBYTE	    (0b00100000)
#define PIT_MODE_TOGGLE	    (0b00110000)

struct i8253_s {
    uint16_t chandata;
    uint8_t accessmode;
    uint8_t bytetoggle;
    float chanfreq;
    uint8_t active;
    uint16_t counter;
};
