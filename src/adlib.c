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

/* adlib.c: very ugly Adlib OPL2 emulation for Fake86. very much a work in progress. :) */

#include "config.h"
#include <stdint.h>
#include <math.h>
#include "ports.h"

extern int32_t usesamplerate;

uint8_t optable[0x16] = { 0, 0, 0, 1, 1, 1, 255, 255, 0, 0, 0, 1, 1, 1, 255, 255, 0, 0, 0, 1, 1, 1 };
uint16_t adlibregmem[0xFF], adlibaddr = 0;

int8_t oplwave[4][256];

struct structadlibop {
	uint8_t wave;
} adlibop[9][2];

struct structadlibchan {
	uint16_t freq;
	double convfreq;
	uint8_t keyon;
	uint16_t octave;
	uint8_t wavesel;
} adlibch[9];

double attacktable[16] = { 1.0003, 1.00025, 1.0002, 1.00015, 1.0001, 1.00009, 1.00008, 1.00007, 1.00006, 1.00005, 1.00004, 1.00003, 1.00002, 1.00001, 1.000005 }; //1.003, 1.05, 1.01, 1.015, 1.02, 1.025, 1.03, 1.035, 1.04, 1.045, 1.05, 1.055, 1.06, 1.065, 1.07, 1.075 };
double decaytable[16] = { 0.99999, 0.999985, 0.99998, 0.999975, 0.99997, 0.999965, 0.99996, 0.999955, 0.99995, 0.999945, 0.99994, 0.999935, 0.99994, 0.999925, 0.99992, 0.99991 };
double adlibenv[9], adlibdecay[9], adlibattack[9];
uint8_t adlibdidattack[9], adlibpercussion = 0, adlibstatus = 0;

uint16_t adlibport = 0x388;

void outadlib (uint16_t portnum, uint8_t value)
{
    if (portnum==adlibport) {
        adlibaddr = value;
        return;
    }
    portnum = adlibaddr;
    adlibregmem[portnum] = value;
    switch (portnum) {
        case 4: //timer control
        if (value&0x80) {
            adlibstatus = 0;
            adlibregmem[4] = 0;
        }
        break;
        case 0xBD:
        if (value & 0x10) adlibpercussion = 1;
        else adlibpercussion = 0;
        break;
    }
    if ( (portnum >= 0x60) && (portnum <= 0x75) ) {
        //attack/decay
        portnum &= 15;
        adlibattack[portnum] = attacktable[15- (value>>4) ]*1.006;
        adlibdecay[portnum] = decaytable[value&15];
    }
    else if ( (portnum >= 0xA0) && (portnum <= 0xB8) ) {
        //octave, freq, key on
        portnum &= 15;
        if (!adlibch[portnum].keyon && ( (adlibregmem[0xB0+portnum]>>5) &1) ) {
            adlibdidattack[portnum] = 0;
            adlibenv[portnum] = 0.0025;
        }
        adlibch[portnum].freq = adlibregmem[0xA0+portnum] | ( (adlibregmem[0xB0+portnum]&3) <<8);
        adlibch[portnum].convfreq = ( (double) adlibch[portnum].freq * 0.7626459);
        adlibch[portnum].keyon = (adlibregmem[0xB0+portnum]>>5) &1;
        adlibch[portnum].octave = (adlibregmem[0xB0+portnum]>>2) &7;
    }
    else if ( (portnum >= 0xE0) && (portnum <= 0xF5) ) {
        //waveform select
        portnum &= 15;
        if (portnum<9) adlibch[portnum].wavesel = value&3;
    }
}

uint8_t inadlib (uint16_t portnum)
{
    if (!adlibregmem[4]) adlibstatus = 0;
    else adlibstatus = 0x80;
    adlibstatus = adlibstatus + (adlibregmem[4]&1) *0x40 + (adlibregmem[4]&2) *0x10;
    return (adlibstatus);
}

uint16_t adlibfreq (uint8_t chan)
{
    uint16_t tmpfreq;
    if (!adlibch[chan].keyon) return (0);
    tmpfreq = (uint16_t) adlibch[chan].convfreq;
    switch (adlibch[chan].octave) {
        case 0:
        tmpfreq = tmpfreq >> 4;
        break;
        case 1:
        tmpfreq = tmpfreq >> 3;
        break;
        case 2:
        tmpfreq = tmpfreq >> 2;
        break;
        case 3:
        tmpfreq = tmpfreq >> 1;
        break;
        case 5:
        tmpfreq = tmpfreq << 1;
        break;
        case 6:
        tmpfreq = tmpfreq << 2;
        break;
        case 7:
        tmpfreq = tmpfreq << 3;
    }

    return (tmpfreq);
}

int32_t adlibsample (uint8_t curchan)
{
    int32_t tempsample;
    double tempstep;
    uint64_t fullstep;
    static uint64_t adlibstep[9];

    if (adlibpercussion && (curchan>=6) && (curchan<=8) ) return (0);

    fullstep = usesamplerate/adlibfreq (curchan);

    tempsample = (int32_t) oplwave[adlibch[curchan].wavesel][ (uint8_t) ( (double) adlibstep[curchan]/ ( (double) fullstep/ (double) 256) ) ];
    tempstep = adlibenv[curchan];
    if (tempstep>1.0) tempstep = 1;
    tempsample = (int32_t) ( (double) tempsample * tempstep * 2.0);

    adlibstep[curchan]++;
    if (adlibstep[curchan]>fullstep) adlibstep[curchan] = 0;
    return (tempsample);
}

int16_t adlibgensample()
{
    uint8_t curchan;
    int16_t adlibaccum;
    adlibaccum = 0;
    for (curchan=0; curchan<9; curchan++) {
        if (adlibfreq (curchan) !=0) {
            adlibaccum += (int16_t) adlibsample (curchan);
        }
    }
    return (adlibaccum);
}

void tickadlib()
{
    uint8_t curchan;
    for (curchan=0; curchan<9; curchan++) {
        if (adlibfreq (curchan) !=0) {
            if (adlibdidattack[curchan]) {
                adlibenv[curchan] *= adlibdecay[curchan];
            }
            else {
                adlibenv[curchan] *= adlibattack[curchan];
                if (adlibenv[curchan]>=1.0) adlibdidattack[curchan] = 1;
            }
        }
    }
}
void initwavetable()
{
    int i;
    float x;
    int8_t val;
    for(i = 0; i < 256; i++) {
        x = 65 * sin(2*M_PI*i/256.0);
        if (x > 64) val = 116;
        else if (x<-46) val = -116;
        else    val = (int8_t)x;

        oplwave[0][i] = val;
        if (i < 128) {
            oplwave[1][i] = val;
            oplwave[2][i] = val;
            if (i < 64) {
                oplwave[3][i] = val;
            } else {
                oplwave[3][i] = 0;
            }
        } else {
            oplwave[1][i] = 0;
            oplwave[2][i] = -val;
            if (i < 192) {
                oplwave[3][i] = -val;
            } else {
                oplwave[3][i] = 0;
            }
        }
    }
}

void initadlib (uint16_t baseport)
{
    set_port_write_redirector (baseport, baseport + 1, &outadlib);
    set_port_read_redirector (baseport, baseport + 1, &inadlib);
    initwavetable();
}
