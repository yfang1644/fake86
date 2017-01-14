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

/* i8253.c: functions to emulate the Intel 8253 programmable interval timer.
   these are required for the timer interrupt and PC speaker to be
   properly emulated! */

#include <stdint.h>
#include <stdio.h>
#include <memory.h>
#include "i8253.h"
#include "mutex.h"
#include "ports.h"

struct i8253_s i8253[3];

extern uint64_t hostfreq, tickgap;

void out8253 (uint16_t portnum, uint8_t value)
{
    uint8_t curbyte = 0;
    portnum &= 3;
    if(portnum == 3) {
        ///mode/command
        i8253[value>>6].accessmode = value & PIT_MODE_TOGGLE;
        i8253[value>>6].bytetoggle = 0;
        if(value & PIT_MODE_TOGGLE == PIT_MODE_LATCHCOUNT) {
            i8253[value>>6].latched= i8253[value>>6].counter;
            i8253[value>>6].latch = 2;
        }
    } else {
        //channel data
        if (i8253[portnum].accessmode == PIT_MODE_LOBYTE) {
            curbyte = 0;        // write low byte
        } else if (i8253[portnum].accessmode == PIT_MODE_HIBYTE) {
            curbyte = 1;        // write high byte
        } else if (i8253[portnum].accessmode == PIT_MODE_TOGGLE) {
            curbyte = i8253[portnum].bytetoggle;    // low byte first
            i8253[portnum].bytetoggle = (~i8253[portnum].bytetoggle) & 1;
        }

        if (curbyte == 0) {
            //low byte
            i8253[portnum].chandata = (i8253[portnum].chandata & 0xFF00) | value;
        } else {
            //high byte
            i8253[portnum].chandata = (i8253[portnum].chandata & 0x00FF) | ( (uint16_t) value << 8);
        }
        i8253[portnum].active = 1;

        if (i8253[portnum].chandata) {
            i8253[portnum].chanfreq = (float) ( (uint32_t) ( ( 1193182.0 / i8253[portnum].chandata) ) );
        } else {
            i8253[portnum].chanfreq = (float) ( (uint32_t) ( ( 1193182.0 / 65536.0) ) );
        }
        //printf("[DEBUG] PIT channel %u counter changed to %u (%f Hz)\n", portnum, i8253[portnum].chandata, i8253[portnum].chanfreq);
        if (portnum == 0)
            tickgap = (uint64_t) ( (float) hostfreq / i8253[portnum].chanfreq );
    }
}

uint8_t in8253 (uint16_t portnum)
{
    uint8_t curbyte;
    portnum &= 3;
    if(portnum == 3) {
        // read command register ?
        return 0;
    } else {
        //channel data
        if (i8253[portnum].accessmode == PIT_MODE_LOBYTE) {
            curbyte = 0;        // write low byte
        } else if (i8253[portnum].accessmode == PIT_MODE_HIBYTE) {
            curbyte = 1;        // write high byte
        } else {
            //latch or 2-byte mode always toggle
            curbyte = i8253[portnum].bytetoggle;    // low byte first
            i8253[portnum].bytetoggle = (~i8253[portnum].bytetoggle) & 1;
        } 

        if (i8253[portnum].accessmode == PIT_MODE_LATCHCOUNT) {
            if(i8253[portnum].latch--) {
                if (curbyte == 0)
                    //low byte
                    return ( (uint8_t) i8253[portnum].latched);
                else
                    return ( (uint8_t) (i8253[portnum].latched >> 8) );
            }
        }

        if (curbyte == 0) {
            //low byte
            return ( (uint8_t) i8253[portnum].counter);
        } else {
            //high byte
            return ( (uint8_t) (i8253[portnum].counter >> 8) );
        }
    }
}

void init8253()
{
    memset (&i8253, 0, sizeof (i8253) );
    set_port_write_redirector (0x40, 0x43, &out8253);
    set_port_read_redirector (0x40, 0x43, &in8253);
}
