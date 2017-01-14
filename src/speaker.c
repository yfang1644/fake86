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

/* speaker.c: function to generate output samples for PC speaker emulation. */

#include "config.h"
#include <stdint.h>
#include "i8253.h"
#include "mutex.h"

extern struct i8253_s i8253[];

int16_t speakergensample(uint64_t samplerate)
{
    int16_t speakervalue;
    uint64_t speakerfullstep;
    static uint64_t speakercurstep = 0;

    speakerfullstep = (uint64_t) ( (float) samplerate / (float) i8253[2].chanfreq);
    if (speakerfullstep < 2) speakerfullstep = 2;
    if (speakercurstep < speakerfullstep/2) {
        speakervalue = 32;
    } else {
        speakervalue = -32;
    }
    speakercurstep = (speakercurstep + 1) % speakerfullstep;
    return (speakervalue);
}
