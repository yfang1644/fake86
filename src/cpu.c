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

/* cpu.c: functions to emulate the 8086/V20 CPU in software. the heart of Fake86. */

#include "config.h"
#ifndef CPU_INSTRUCTION_FLOW_CACHE

#include <stdint.h>
#include <stdio.h>
#include "cpu.h"
#include "i8259.h"
#include "ports.h"

extern struct structpic i8259;

uint8_t byteregtable[8] = { regal, regcl, regdl, regbl, regah, regch, regdh, regbh };

uint8_t	RAM[0x100000], readonly[0x100000]; //1MB
uint8_t	portram[0x10000];  // 64KB
uint8_t	opcode, segoverride, bootdrive = 0, hdcount = 0, hltstate = 0;
uint16_t segregs[4], savecs, saveip, ip, useseg;
uint16_t cf, pf, af, zf, sf, tf, ifl, df, of, mode, reg, rm;
uint16_t disp16;
uint8_t	disp8;
uint32_t ea;
uint64_t totalexec;

extern uint16_t	VGA_SC[0x100];
extern uint8_t updatedscreen;
union _bytewordregs_ regs;

uint8_t	running = 0, didbootstrap = 0;

extern uint8_t vidmode, verbose;

extern void vidinterrupt();

extern uint8_t readVGA (uint32_t addr32);

extern void	writeVGA (uint32_t addr32, uint8_t value);

uint8_t read86 (uint32_t addr32)
{
    addr32 &= 0xFFFFF;
    if ( (addr32 >= 0xA0000) && (addr32 <= 0xBFFFF) ) {
        if ((vidmode == 0xD) ||
            (vidmode == 0xE) ||
            (vidmode == 0x10) ||
            (vidmode == 0x12) ) {
                return (readVGA (addr32 - 0xA0000) );
            }
        if ((vidmode != 0x13) &&
            (vidmode != 0x12) &&
            (vidmode != 0xD) ) {
                return (RAM[addr32]);
            }
        if ( (VGA_SC[4] & 6) == 0)
            return (RAM[addr32]);
        else
            return (readVGA (addr32 - 0xA0000) );
    }

    if (!didbootstrap) {
        RAM[0x410] = 0x41; //ugly hack to make BIOS always believe we have an EGA/VGA card installed
        RAM[0x475] = hdcount; //the BIOS doesn't have any concept of hard drives, so here's another hack
    }

    return (RAM[addr32]);
}

uint16_t readw86 (uint32_t addr32)
{
    return ( (uint16_t) read86 (addr32) | (uint16_t) (read86 (addr32 + 1) << 8) );
}

void write86 (uint32_t addr32, uint8_t value)
{
    uint32_t tempaddr32;

	tempaddr32 = addr32 & 0xFFFFF;
#ifdef CPU_ADDR_MODE_CACHE
	if (!readonly[tempaddr32]) addrcachevalid[tempaddr32] = 0;
#endif
	if (readonly[tempaddr32] || (tempaddr32 >= 0xC0000) ) {
        return;
    }

    if ( (tempaddr32 >= 0xA0000) && (tempaddr32 <= 0xBFFFF) ) {
        if ((vidmode != 0x13) &&
            (vidmode != 0x12) &&
            (vidmode != 0xD) &&
            (vidmode != 0x10) ) {
                RAM[tempaddr32] = value;
            } else if ( ( (VGA_SC[4] & 6) == 0) && (vidmode == 0x13)) {
                RAM[tempaddr32] = value;
            } else {
                writeVGA (tempaddr32 - 0xA0000, value);
            }

        updatedscreen = 1;
    } else {
        RAM[tempaddr32] = value;
    }
}

void writew86 (uint32_t addr32, uint16_t value)
{
    write86 (addr32, (uint8_t) value);
    write86 (addr32 + 1, (uint8_t) (value >> 8) );
}

uint16_t parity(uint8_t value)
{
    int i;
    uint16_t p = 1;
    for (i = 0; i < 8; i++) {
        p ^= (value & 1);
        value >>= 1;
    }
    return p;
}

void flag_szp8 (uint8_t value)
{
    zf = (value == 0); /* set or clear zero flag */
    sf = (value >> 7) & 1; /* set or clear sign flag */
    pf = parity(value); /* retrieve parity state */
}

void flag_szp16 (uint16_t value)
{
    zf = (value == 0); /* set or clear zero flag */
    sf = (value >> 15) & 1; /* set or clear sign flag */
    pf = parity(value);	/* retrieve parity state */
}

void flag_log8 (uint8_t value)
{
    flag_szp8 (value);
    cf = 0;
    of = 0; /* bitwise logic ops always clear carry and overflow */
}

void flag_log16 (uint16_t value)
{
    flag_szp16 (value);
    cf = 0;
    of = 0; /* bitwise logic ops always clear carry and overflow */
}

uint16_t op_add16 (uint16_t v1, uint16_t v2, uint16_t v3)
{
    uint32_t	dst;
    uint32_t    temp;

    dst = (uint32_t) v1 + (uint32_t) v2 + (uint32_t) v3;
    flag_szp16 ( (uint16_t) dst);
    temp =  (dst ^ v1) & (dst ^ v2);
    of = (temp >> 15) & 1; /* set or clear overflow flag */

    if (dst & 0xFFFF0000) {
        cf = 1;
    } else {
        cf = 0;
    }

    temp = (v1 ^ v2 ^ dst);
    af = (temp >> 4) & 1; /* set or clear auxilliary flag */
    return (uint16_t)dst;
}

uint8_t op_add8 (uint8_t v1, uint8_t v2, uint16_t v3)
{
    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    int16_t	 dst;
    uint16_t  temp;

    dst = (int16_t) v1 + (int16_t) v2 + (int16_t) v3;
    flag_szp8 ( (uint8_t) dst);
    if (dst & 0xFF00) {
        cf = 1;
    } else {
        cf = 0; /* set or clear carry flag */
    }

    temp =  (dst ^ v1) & (dst ^ v2);
    of = (temp >> 7) & 1; /* set or clear overflow flag */

    temp = (v1 ^ v2 ^ dst);
    af = (temp >> 4) & 1; /* set or clear auxilliary flag */
    return (uint8_t)dst;
}

uint8_t op_sub8 (uint8_t v1, uint8_t v2, uint16_t v3)
{
    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    uint16_t  dst;
    uint16_t  temp;

    v2 += v3;
    dst = (int16_t) v1 - (int16_t) v2;
    flag_szp8 ( (uint8_t) dst);
    if (dst & 0xFF00) {
        cf = 1;
    } else {
        cf = 0;
    }

    temp =  (dst ^ v1) & (v1 ^ v2);
    of = (temp >> 7) & 1; /* set or clear overflow flag */

    temp = (v1 ^ v2 ^ dst);
    af = (temp >> 4) & 1; /* set or clear auxilliary flag */
    return (uint8_t)dst;
}

uint16_t op_sub16 (uint16_t v1, uint16_t v2, uint16_t v3)
{
    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    uint32_t	dst;
    uint32_t    temp;

    v2 += v3;
    dst = (uint32_t) v1 - (uint32_t) v2;
    flag_szp16 ( (uint16_t) dst);
    if (dst & 0xFFFF0000) {
        cf = 1;
    } else {
        cf = 0;
    }

    temp =  (dst ^ v1) & (v1 ^ v2);
    of = (temp >> 15) & 1; /* set or clear overflow flag */

    temp = (v1 ^ v2 ^ dst);
    af = (temp >> 4) & 1; /* set or clear auxilliary flag */
    return (uint16_t)dst;
}

uint8_t op_and8(uint8_t a, uint8_t b)
{
    uint8_t res;
    res = a & b;
    flag_log8 (res);
    return res;
}

uint16_t op_and16(uint16_t a, uint16_t b)
{
    uint16_t res;
    res = a & b;
    flag_log16 (res);
    return res;
}

uint8_t op_or8(uint8_t a, uint8_t b)
{
    uint8_t res;
    res = a | b;
    flag_log8 (res);
    return res;
}

uint16_t op_or16(uint16_t a, uint16_t b)
{
    uint16_t res;
    res = a | b;
    flag_log16 (res);
    return res;
}

uint8_t op_xor8(uint8_t a, uint8_t b)
{
    uint8_t res;
    res = a ^ b;
    flag_log8 (res);
    return res;
}

uint16_t op_xor16(uint16_t a, uint16_t b)
{
    uint16_t res;
    res = a ^ b;
    flag_log16 (res);
    return res;
}

void push (uint16_t pushval)
{
    regs.wordregs[regsp] -= 2;
    putmem16 (segregs[regss], regs.wordregs[regsp], pushval);
}

uint16_t pop()
{
    uint16_t	tempval;

    tempval = getmem16 (segregs[regss], regs.wordregs[regsp]);
    regs.wordregs[regsp] += 2;
    return tempval;
}

#ifdef NETWORKING_ENABLED
extern void nethandler();
#endif
extern void diskhandler();
extern void readdisk (uint8_t drivenum, uint16_t dstseg, uint16_t dstoff, uint16_t cyl, uint16_t sect, uint16_t head, uint16_t sectcount);

void intcall86 (uint8_t intnum)
{
    static uint16_t lastint10ax;
    uint16_t oldregax;

    if (intnum == 0x19) didbootstrap = 1;

    switch (intnum) {
    case 0x10:
        updatedscreen = 1;
        /*if (regs.byteregs[regah]!=0x0E) {
                    printf("Int 10h AX = %04X\n", regs.wordregs[regax]);
    }*/
        if ( (regs.byteregs[regah]==0x00) ||
            (regs.byteregs[regah]==0x10) ||
           (regs.byteregs[regah]==0x0c)) {
            oldregax = regs.wordregs[regax];
            vidinterrupt();
            regs.wordregs[regax] = oldregax;
            if (regs.byteregs[regah]==0x10) return;
            if (vidmode==9) return;
        }
        if ( (regs.byteregs[regah]==0x1A) && (lastint10ax!=0x0100) ) {
            //the 0x0100 is a cheap hack to make it not do this if DOS EDIT/QBASIC
            regs.byteregs[regal] = 0x1A;
            regs.byteregs[regbl] = 0x8;
            return;
        }
        lastint10ax = regs.wordregs[regax];
        if (regs.byteregs[regah] == 0x1B) {
            regs.byteregs[regal] = 0x1B;
            segregs[reges] = 0xC800;
            regs.wordregs[regdi] = 0x0000;
            writew86(0xC8000, 0x0000);
            writew86(0xC8002, 0xC900);
            write86(0xC9000, 0x00);
            write86(0xC9001, 0x00);
            write86(0xC9002, 0x01);
            return;
        }
        break;

#ifndef DISK_CONTROLLER_ATA
    case 0x19: //bootstrap
#ifdef BENCHMARK_BIOS
        running = 0;
#endif
        if (bootdrive<255) {
            //read first sector of boot drive into 07C0:0000 and execute it
            regs.byteregs[regdl] = bootdrive;
            readdisk (regs.byteregs[regdl], 0x07C0, 0x0000, 0, 1, 0, 1);
            segregs[regcs] = 0x0000;
            ip = 0x7C00;
        } else {
            segregs[regcs] = 0xF600;	//start ROM BASIC at bootstrap if requested
            ip = 0x0000;
        }
        return;

    case 0x13:
    case 0xFD:
        diskhandler();
        return;
#endif
#ifdef NETWORKING_OLDCARD
    case 0xFC:
#ifdef NETWORKING_ENABLED
        nethandler();
#endif
        return;
#endif
    }

    push (makeflagsword() );
    push (segregs[regcs]);
    push (ip);
    segregs[regcs] = getmem16 (0, (uint16_t) intnum * 4 + 2);
    ip = getmem16 (0, (uint16_t) intnum * 4);
    ifl = 0;
    tf = 0;
}

void op_div8 (uint16_t valdiv, uint8_t divisor)
{
    if (divisor == 0) {
        intcall86 (0);
        return;
    }

	if ( (valdiv / (uint16_t) divisor) > 0xFF) {
        intcall86 (0);
        return;
    }

	regs.byteregs[regah] = valdiv % (uint16_t) divisor;
    regs.byteregs[regal] = valdiv / (uint16_t) divisor;
}

void op_idiv8 (uint16_t valdiv, uint8_t divisor)
{
    uint16_t s1;
    uint16_t s2;
    uint16_t d1;
    uint16_t d2;
    uint16_t sign;

    if (divisor == 0) {
        intcall86 (0);
        return;
    }

    s1 = valdiv;
    s2 = signext(divisor);
    sign = (s1 ^ s2) & 0x8000;
    if ((s1 & 0x8000)!=0)  s1 = (~s1 + 1) & 0xffff;
    if ((s2 & 0x8000)!=0)  s2 = (~s2 + 1) & 0xffff;
    d1 = s1 / s2;
    d2 = s1 % s2;
    if (d1 & 0xFF00) {
        intcall86 (0);
        return;
    }

    if (sign) {
        d1 = (~d1 + 1) & 0xff;
        d2 = (~d2 + 1) & 0xff;
    }

    regs.byteregs[regah] = (uint8_t) d2;
    regs.byteregs[regal] = (uint8_t) d1;
}

void op_div16 (uint32_t valdiv, uint16_t divisor)
{
    if (divisor == 0) {
        intcall86 (0);
        return;
    }

    if ( (valdiv / (uint32_t) divisor) > 0xFFFF) {
        intcall86 (0);
        return;
    }

    regs.wordregs[regdx] = valdiv % (uint32_t) divisor;
    regs.wordregs[regax] = valdiv / (uint32_t) divisor;
}

void op_idiv16 (uint32_t valdiv, uint16_t divisor)
{
    uint32_t d1;
    uint32_t d2;
    uint32_t s1;
    uint32_t s2;
    uint32_t sign;

    if (divisor == 0) {
        intcall86 (0);
        return;
    }

    s1 = valdiv;
    s2 = signext32(divisor);
    sign = (s1 ^ s2) & 0x80000000;
    if ((s1 & 0x80000000)!=0)  s1 = (~s1) + 1 ;
    if ((s2 & 0x80000000)!=0)  s2 = (~s2) + 1 ;
    d1 = s1 / s2;
    d2 = s1 % s2;
    if (d1 & 0xFFFF0000) {
        intcall86 (0);
        return;
    }

    if (sign) {
        d1 = (~d1 + 1) & 0xffff;
        d2 = (~d2 + 1) & 0xffff;
    }

    regs.wordregs[regax] = d1;
    regs.wordregs[regdx] = d2;
}

void getea (uint8_t rmval)
{
    uint32_t	tempea = 0;

    switch (mode) {
    case 0:
        switch (rmval) {
        case 0:
            tempea = regs.wordregs[regbx] + regs.wordregs[regsi];
            break;
        case 1:
            tempea = regs.wordregs[regbx] + regs.wordregs[regdi];
            break;
        case 2:
            tempea = regs.wordregs[regbp] + regs.wordregs[regsi];
            break;
        case 3:
            tempea = regs.wordregs[regbp] + regs.wordregs[regdi];
            break;
        case 4:
            tempea = regs.wordregs[regsi];
            break;
        case 5:
            tempea = regs.wordregs[regdi];
            break;
        case 6:
            tempea = disp16;
            break;
        case 7:
            tempea = regs.wordregs[regbx];
            break;
        }
        break;

    case 1:
    case 2:
        tempea = disp16;
        switch (rmval) {
        case 0:
            tempea += regs.wordregs[regbx] + regs.wordregs[regsi];
            break;
        case 1:
            tempea += regs.wordregs[regbx] + regs.wordregs[regdi];
            break;
        case 2:
            tempea += regs.wordregs[regbp] + regs.wordregs[regsi];
            break;
        case 3:
            tempea += regs.wordregs[regbp] + regs.wordregs[regdi];
            break;
        case 4:
            tempea += regs.wordregs[regsi];
            break;
        case 5:
            tempea += regs.wordregs[regdi];
            break;
        case 6:
            tempea += regs.wordregs[regbp];
            break;
        case 7:
            tempea += regs.wordregs[regbx];
            break;
        }
        break;
    }

    ea = (tempea & 0xFFFF) + (useseg << 4);
}

void reset86()
{
    segregs[regcs] = 0xFFFF;
    ip = 0x0000;
    hltstate = 0;
}

uint8_t readrm8 (uint8_t rmval)
{
    if (mode < 3) {
        getea (rmval);
        return read86 (ea);
    } else {
        return getreg8 (rmval);
    }
}

void writerm8 (uint8_t rmval, uint8_t value)
{
    if (mode < 3) {
        getea (rmval);
        write86 (ea, value);
    } else {
        getreg8 (rmval) = value;
    }
}

uint16_t readrm16 (uint8_t rmval)
{
    if (mode < 3) {
        getea (rmval);
        return readw86 (ea);
    } else {
        return getreg16 (rmval);
    }
}

void writerm16 (uint8_t rmval, uint16_t value)
{
    if (mode < 3) {
        getea (rmval);
        writew86 (ea, value);
    } else {
        getreg16 (rmval) = value;
    }
}

uint8_t op_grp2_8 (uint8_t s, uint8_t cnt)
{
    uint16_t shift;
    uint16_t oldcf;

#ifdef CPU_LIMIT_SHIFT_COUNT
    cnt &= 0x1F;
#endif
    if (cnt == 0)
        return s;

    switch (reg) {
    case 0: /* ROL r/m8 */
        for (shift = 0; shift < cnt - 1; shift++) {
            cf = (s >> 7) & 1;
            s = (s << 1) | cf;
        }
        cf = (s >> 7) & 1;
        s = (s << 1) | cf;
        of = (cf ^ (s >> 7)) & 1;
        break;

    case 1: /* ROR r/m8 */
        for (shift = 0; shift < cnt - 1; shift++) {
            cf = s & 1;
            s = (s >> 1) | (cf << 7);
        }
        cf = s & 1;
        s = (s >> 1) | (cf << 7);
        of = ((s >> 7) ^ (s >> 6)) & 1;
        break;

    case 2: /* RCL r/m8 */
        for (shift = 0; shift < cnt - 1; shift++) {
            oldcf = cf;
            cf = (s >> 7) & 1;
            s = (s << 1) | oldcf;
        }
        oldcf = cf;
        cf = (s >> 7) & 1;
        s = (s << 1) | oldcf;
        of = (cf ^ (s >> 7)) & 1;
        break;

    case 3: /* RCR r/m8 */
        for (shift = 0; shift < cnt - 1; shift++) {
            oldcf = cf;
            cf = s & 1;
            s = (s >> 1) | (oldcf << 7);
        }
        oldcf = cf;
        cf = s & 1;
        s = (s >> 1) | (oldcf << 7);
        of = ((s >> 7) ^ (s >> 6)) & 1;
        break;

    case 4:
    case 6: /* SHL r/m8 */
        shift = cnt - 1;
        s <<= shift;
        cf = (s >> 7) & 1;
        of = (cf ^ (s >> 6)) & 1;
        s <<= 1;
        break;

    case 5: /* SHR r/m8 */
        if ( cnt == 1 )     of = (s >> 7) & 1;
    case 7: /* SAR r/m8 */
        shift = cnt - 1;
        s >>= shift;
        cf = s & 1;
        s >>= 1;

        if(reg == 7)  of = 0;
        break;
    }

    flag_szp8(s);
	return s;
}

uint16_t op_grp2_16 (uint16_t s, uint8_t cnt)
{
    uint32_t shift;
    uint32_t oldcf;

#ifdef CPU_LIMIT_SHIFT_COUNT
	cnt &= 0x1F;
#endif
    if (cnt == 0)
        return s;

	switch (reg) {
    case 0: /* ROL r/m8 */
        for (shift = 0; shift < cnt - 1; shift++) {
            cf = (s >> 15) & 1;
            s = (s << 1) | cf;
        }
        cf = (s >> 15) & 1;
        s = (s << 1) | cf;
        of = cf ^ ( (s >> 15) & 1);
        break;

    case 1: /* ROR r/m8 */
        for (shift = 0; shift < cnt - 1; shift++) {
            cf = s & 1;
            s = (s >> 1) | (cf << 15);
        }
        cf = s & 1;
        s = (s >> 1) | (cf << 15);
        of = ((s >> 15) ^ (s >> 14)) & 1;
        break;

    case 2: /* RCL r/m8 */
        for (shift = 0; shift < cnt - 1; shift++) {
            oldcf = cf;
            cf = (s >> 15) & 1;
            s = (s << 1) | oldcf;
        }
        oldcf = cf;
        cf = (s >> 15) & 1;
        s = (s << 1) | oldcf;
        of = cf ^ ( (s >> 15) & 1);
        break;

    case 3: /* RCR r/m8 */
        for (shift = 0; shift < cnt - 1; shift++) {
            oldcf = cf;
            cf = s & 1;
            s = (s >> 1) | (oldcf << 15);
        }
        oldcf = cf;
        cf = s & 1;
        s = (s >> 1) | (oldcf << 15);
        of = ((s >> 15) ^ (s >> 14)) & 1;
        return (s);
        break;

    case 4:
    case 6: /* SHL r/m8 */
        shift = cnt - 1;
        s <<= shift;
        cf = (s >> 15) & 1;
        of = (cf ^ (s >> 14)) & 1;
        s <<= 1;
		break;

    case 5: /* SHR r/m8 */
        if ( cnt == 1 )     of = (s >> 15) & 1;
    case 7: /* SAR r/m8 */
        shift = cnt - 1;
        s >>= shift;
        cf = s & 1;
        s >>= 1;

        if(reg == 7)  of = 0;
        break;
    }
    flag_szp16(s);
    return s;
}

uint8_t op_grp3_8(uint8_t oper1b)
{
    uint32_t temp1, temp2, temp3;
    uint8_t res = 0;
    uint16_t oper1;

    switch (reg) {
    case 0:
    case 1: /* TEST */
        flag_log8 (oper1b & getmem8 (segregs[regcs], ip++) );
        break;

    case 2: /* NOT */
        res = ~oper1b;
        break;

    case 3: /* NEG */
        res = (~oper1b) + 1;
        op_sub8 (0, oper1b, 0);
        if( res ) {
            cf = 1;
        } else {
            cf = 0;
        }
        break;

    case 4: /* MUL */
        temp1 = (uint32_t) oper1b * (uint32_t) regs.byteregs[regal];
        regs.wordregs[regax] = temp1 & 0xFFFF;
        flag_szp8 ( (uint8_t) temp1);
        if (regs.byteregs[regah]) {
            cf = 1;
            of = 1;
        } else {
            cf = 0;
            of = 0;
        }
#ifdef CPU_CLEAR_ZF_ON_MUL
        zf = 0;
#endif
        break;

    case 5: /* IMUL */
        oper1 = signext (oper1b);
        temp1 = signext (regs.byteregs[regal]);
        temp2 = signext32(oper1);
        temp1 = signext32(temp1);

        temp3 = (temp1 * temp2) & 0xFFFF;
        regs.wordregs[regax] = temp3 & 0xFFFF;
        if (regs.byteregs[regah]) {
            cf = 1;
            of = 1;
        } else {
            cf = 0;
            of = 0;
        }
#ifdef CPU_CLEAR_ZF_ON_MUL
        zf = 0;
#endif
        break;

    case 6: /* DIV */
        op_div8 (regs.wordregs[regax], oper1b);
        break;

    case 7: /* IDIV */
        op_idiv8 (regs.wordregs[regax], oper1b);
        break;
    }
    return res;
}

uint16_t op_grp3_16(uint16_t oper1)
{
    uint32_t temp1, temp2, temp3;
    uint16_t res = 0;

    switch (reg) {
    case 0:
    case 1: /* TEST */
        flag_log16 (oper1 & getmem16 (segregs[regcs], ip++) );
        ip++;
        break;

    case 2: /* NOT */
        res = ~oper1;
    break;

    case 3: /* NEG */
        res = (~oper1) + 1;
        op_sub16 (0, oper1, 0);
        if(res ) {
            cf = 1;
        } else {
            cf = 0;
        }
        break;

    case 4: /* MUL */
        temp1 = (uint32_t) oper1 * (uint32_t) regs.wordregs[regax];
        regs.wordregs[regax] = temp1 & 0xFFFF;
        regs.wordregs[regdx] = temp1 >> 16;
        flag_szp16 ( (uint16_t) temp1);
        if (regs.wordregs[regdx]) {
            cf = 1;
            of = 1;
        } else {
            cf = 0;
            of = 0;
        }
#ifdef CPU_CLEAR_ZF_ON_MUL
        zf = 0;
#endif
        break;

    case 5: /* IMUL */
        temp1 = signext32(regs.wordregs[regax]);
        temp2 = signext32(oper1);

        temp3 = (int32_t)temp1 * (int32_t)temp2;
        regs.wordregs[regax] = temp3 & 0xFFFF;	/* into register ax */
        regs.wordregs[regdx] = temp3 >> 16;	/* into register dx */
        if (regs.wordregs[regdx]) {
            cf = 1;
            of = 1;
        } else {
            cf = 0;
            of = 0;
        }
#ifdef CPU_CLEAR_ZF_ON_MUL
        zf = 0;
#endif
        break;

    case 6: /* DIV */
        op_div16 ( ( (uint32_t) regs.wordregs[regdx] << 16) + regs.wordregs[regax], oper1);
        break;

    case 7: /* DIV */
        op_idiv16 ( ( (uint32_t) regs.wordregs[regdx] << 16) + regs.wordregs[regax], oper1);
        break;
    }
    return res;
}

uint16_t op_grp5(uint16_t oper1)
{
    uint8_t tempcf;
    uint16_t res = 0;

	switch (reg) {
    case 0: /* INC Ev */
        tempcf = cf;
        res = op_add16(oper1, 1, 0);
        cf = tempcf;
        break;

    case 1: /* DEC Ev */
        tempcf = cf;
        res = op_sub16(oper1, 1, 0);
        cf = tempcf;
        break;

    case 2: /* CALL Ev */
        push (ip);
        ip = oper1;
        break;

    case 3: /* CALL Mp */
        push (segregs[regcs]);
        push (ip);
        getea (rm);
        ip = readw86 (ea);
        segregs[regcs] = readw86 (ea + 2);
        break;

    case 4: /* JMP Ev */
        ip = oper1;
        break;

    case 5: /* JMP Mp */
        getea (rm);
        ip = readw86 (ea);
        segregs[regcs] = readw86 (ea + 2);
        break;

    case 6: /* PUSH Ev */
        push (oper1);
        break;
    }
    return res;
}

#ifdef CPU_ADDR_MODE_CACHE
struct addrmodecache_s addrcache[0x100000];
uint8_t addrcachevalid[0x100000];

uint64_t cached_access_count = 0, uncached_access_count = 0;
void modregrm()
{
    uint32_t addrdatalen, dataisvalid;
    uint32_t tempaddr32;
    uint8_t addrbyte;

    tempaddr32 = (((uint32_t)savecs << 4) + ip) & 0xFFFFF;
    if (addrcachevalid[tempaddr32]) {
        switch (addrcache[tempaddr32].len) {
        case 0:
            dataisvalid = 1;
            break;
        case 1:
            if (addrcachevalid[tempaddr32+1])
                dataisvalid = 1;
            else
                dataisvalid = 0;
            break;
        case 2:
            if (addrcachevalid[tempaddr32+1] && addrcachevalid[tempaddr32+2])
                dataisvalid = 1;
            else
                dataisvalid = 0;
				break;
		}
	} else dataisvalid = 0;

    if (dataisvalid) {
        cached_access_count++;
        disp16 = addrcache[tempaddr32].disp16;
        segregs[regcs] = addrcache[tempaddr32].exitcs;
        ip = addrcache[tempaddr32].exitip;
        mode = addrcache[tempaddr32].mode;
        reg = addrcache[tempaddr32].reg;
        rm = addrcache[tempaddr32].rm;
        if ((!segoverride) && addrcache[tempaddr32].forcess)
        useseg = segregs[regss];
    } else {
        uncached_access_count++;
        addrbyte = getmem8(segregs[regcs], ip++);
        mode = addrbyte >> 6;
        reg = (addrbyte >> 3) & 7;
        rm = addrbyte & 7;
        addrdatalen = 0;
        addrcache[tempaddr32].forcess = 0;

        switch(mode)
        {
        case 0:
            if(rm == 6) {
                disp16 = getmem16(segregs[regcs], ip++);
                ip++;
                addrdatalen = 2;
            }
            if (((rm == 2) || (rm == 3))) {
                if (!segoverride) useseg = segregs[regss];
                addrcache[tempaddr32].forcess = 1;
            }
            break;
        case 1:
            disp16 = signext(getmem8(segregs[regcs], ip++));
            addrdatalen = 1;
            if (((rm == 2) || (rm == 3) || (rm == 6))) {
                if (!segoverride) useseg = segregs[regss];
                addrcache[tempaddr32].forcess = 1;
            }
            break;
        case 2:
            disp16 = getmem16(segregs[regcs], ip++);
            ip++;
            addrdatalen = 2;
            if (((rm == 2) || (rm == 3) || (rm == 6))) {
                if (!segoverride) useseg = segregs[regss];
                addrcache[tempaddr32].forcess = 1;
            }
            break;
        default:
            disp16 = 0;
        }
        addrcache[tempaddr32].disp16 = disp16;
        addrcache[tempaddr32].exitcs = segregs[regcs];
        addrcache[tempaddr32].exitip = ip;
        addrcache[tempaddr32].mode = mode;
        addrcache[tempaddr32].reg = reg;
        addrcache[tempaddr32].rm = rm;
        addrcache[tempaddr32].len = addrdatalen;
        memset(&addrcachevalid[tempaddr32], 1, addrdatalen+1);
    }
}
#else
void modregrm()
{
    uint8_t addrbyte;

    addrbyte = getmem8(segregs[regcs], ip++);
    mode = addrbyte >> 6;      // B7,B6
    reg = (addrbyte >> 3) & 7; // B5,B4,B3
    rm = addrbyte & 7;         // B2,B1,B0
    switch(mode)
    {
    case 0:
        if(rm == 6) {
            disp16 = getmem16(segregs[regcs], ip++);
            ip++;
        }
        if(((rm == 2) || (rm == 3)) && !segoverride) {
            useseg = segregs[regss];
        }
        break;
    case 1:
        disp16 = signext(getmem8(segregs[regcs], ip++));
        if(((rm == 2) || (rm == 3) || (rm == 6)) && !segoverride) {
            useseg = segregs[regss];
        }
        break;
    case 2:
        disp16 = getmem16(segregs[regcs], ip++);
        ip++;
        if(((rm == 2) || (rm == 3) || (rm == 6)) && !segoverride) {
            useseg = segregs[regss];
        }
        break;
    default:
        disp8 = 0;
        disp16 = 0;
    }
}
#endif  //CPU_ADDR_MODE_CACHE


#if defined(NETWORKING_ENABLED)
extern struct netstruct {
    uint8_t	enabled;
    uint8_t	canrecv;
    uint16_t pktlen;
} net;
#endif
extern uint8_t	nextintr();
extern void	timing();

#ifdef USE_PREFETCH_QUEUE
uint8_t prefetch[6];
uint32_t prefetch_base = 0;
#endif

void exec86 (uint32_t execloops)
{
    uint16_t oldcf, oldsp;
    uint8_t reptype, res8 = 0;
    uint16_t temp16, res16 = 0;
    uint32_t loopcount;
    uint8_t docontinue, nestlev;
    uint16_t firstip, stacksize, frametemp;
    static uint16_t trap_toggle = 0;
    uint32_t temp1, temp2, temp3;
    uint8_t oper1b, oper2b;
    uint16_t oper1, oper2;

    for (loopcount = 0; loopcount < execloops; loopcount++) {
        if ( (totalexec & TIMING_INTERVAL) == 0) timing();

        if (trap_toggle) {
            intcall86 (1);
        }

        trap_toggle = tf;

        if (!trap_toggle && (ifl && (i8259.irr & (~i8259.imr) ) ) ) {
            hltstate = 0;
            intcall86 (nextintr() );	/* get next interrupt from the i8259, if any */
        }

        if (hltstate) continue;

        if (!running) {
            break;
        }

        /*if ((((uint32_t)segregs[regcs] << 4) + (uint32_t)ip) == 0xFEC59) {
        //printf("Entered F000:EC59, returning to ");
                    ip = pop();
        segregs[regcs] = pop();
                    decodeflagsword(pop());
        //printf("%04X:%04X\n", segregs[regcs], ip);
        diskhandler();
                }*/

        reptype = 0;
        segoverride = 0;
        useseg = segregs[regds];
        docontinue = 0;
        firstip = ip;

        if ( (segregs[regcs] == 0xF000) && (ip == 0xE066) )
            didbootstrap = 0; //detect if we hit the BIOS entry point to clear didbootstrap because we've rebooted

        while (!docontinue) {
            savecs = segregs[regcs];
            saveip = ip;
#ifdef USE_PREFETCH_QUEUE
            ea = segbase(savecs) + (uint32_t)saveip;
            if ( (ea < prefetch_base) || (ea > (prefetch_base + 5)) ) {
                memcpy (&prefetch[0], &RAM[ea], 6);
                prefetch_base = ea;
            }
            opcode = prefetch[ea - prefetch_base];
            ip++;
#else
            opcode = getmem8 (segregs[regcs], ip++);
#endif

            switch (opcode) {
                /* segment prefix check */
            case 0x26:	/* segment segregs[reges] */
            case 0x2E:	/* segment segregs[regcs] */
            case 0x36:	/* segment segregs[regss] */
            case 0x3E:	/* segment segregs[regds] */
                temp1 = (opcode >> 3) & 3;
                useseg = segregs[temp1];
                segoverride = 1;
                break;

                /* repetition prefix check */
            case 0xF3:	/* REP/REPE/REPZ */
                reptype = 1;
                break;

            case 0xF2:	/* REPNE/REPNZ */
                reptype = 2;
                break;

            default:
                docontinue = 1;
                break;
            }
        }

        totalexec++;

        switch (opcode) {
        case 0x0:	/* 00 ADD Eb Gb */
        case 0x8:	/* 08 OR Eb Gb */
        case 0x10:	/* 10 ADC Eb Gb */
        case 0x18:	/* 18 SBB Eb Gb */
        case 0x20:	/* 20 AND Eb Gb */
        case 0x28:	/* 28 SUB Eb Gb */
        case 0x30:	/* 30 XOR Eb Gb */
        case 0x38:	/* 38 CMP Eb Gb */
            temp1 = (opcode >> 3) & 7;
            modregrm();
            oper1b = readrm8 (rm);
            oper2b = getreg8 (reg);
            switch (temp1) {
                case 0: res8 = op_add8(oper1b, oper2b, 0); break;
                case 1: res8 = op_or8(oper1b, oper2b); break;
                case 2: res8 = op_add8(oper1b, oper2b, cf); break;
                case 3: res8 = op_sub8(oper1b, oper2b, cf); break;
                case 4: res8 = op_and8(oper1b, oper2b); break;
                case 5:
                case 7: res8 = op_sub8(oper1b, oper2b, 0); break;
                case 6: res8 = op_xor8(oper1b, oper2b); break;
            }
            if (opcode != 0x38)
                writerm8 (rm, res8);
            break;

        case 0x1:	/* 01 ADD Ev Gv */
        case 0x9:	/* 09 OR Ev Gv */
        case 0x11:	/* 11 ADC Ev Gv */
        case 0x19:	/* 19 SBB Ev Gv */
        case 0x21:	/* 21 AND Ev Gv */
        case 0x29:	/* 29 SUB Ev Gv */
        case 0x31:	/* 31 XOR Ev Gv */
        case 0x39:	/* 39 CMP Ev Gv */
            temp1 = (opcode >> 3) & 7;
            modregrm();
            oper1 = readrm16 (rm);
            oper2 = getreg16 (reg);
            switch (temp1) {
                case 0: res16 = op_add16(oper1, oper2, 0); break;
                case 1: res16 = op_or16(oper1, oper2); break;
                case 2: res16 = op_add16(oper1, oper2, cf); break;
                case 3: res16 = op_sub16(oper1, oper2, cf); break;
                case 4: res16 = op_and16(oper1, oper2); break;
                case 7:
                case 5: res16 = op_sub16(oper1, oper2, 0); break;
                case 6: res16 = op_xor16(oper1, oper2); break;
            }
            if (opcode != 0x39)
                writerm16 (rm, res16);
            break;

        case 0x2:	/* 02 ADD Gb Eb */
        case 0xA:	/* 0A OR Gb Eb */
        case 0x12:	/* 12 ADC Gb Eb */
        case 0x1A:	/* 1A SBB Gb Eb */
        case 0x22:	/* 22 AND Gb Eb */
        case 0x2A:	/* 2A SUB Gb Eb */
        case 0x32:	/* 32 XOR Gb Eb */
        case 0x3A:	/* 3A CMP Gb Eb */
            temp1 = (opcode >> 3) & 7;
            modregrm();
            oper1b = getreg8 (reg);
            oper2b = readrm8 (rm);
            switch (temp1) {
                case 0: res8 = op_add8(oper1b, oper2b, 0); break;
                case 1: res8 = op_or8(oper1b, oper2b); break;
                case 2: res8 = op_add8(oper1b, oper2b, cf); break;
                case 3: res8 = op_sub8(oper1b, oper2b, cf); break;
                case 4: res8 = op_and8(oper1b, oper2b); break;
                case 7:
                case 5: res8 = op_sub8(oper1b, oper2b, 0); break;
                case 6: res8 = op_xor8(oper1b, oper2b); break;
            }
            if (opcode != 0x3A)
                getreg8 (reg) = res8;
            break;

        case 0x3:	/* 03 ADD Gv Ev */
        case 0xB:	/* 0B OR Gv Ev */
        case 0x13:	/* 13 ADC Gv Ev */
        case 0x1B:	/* 1B SBB Gv Ev */
        case 0x23:	/* 23 AND Gv Ev */
        case 0x2B:	/* 2B SUB Gv Ev */
        case 0x33:	/* 33 XOR Gv Ev */
        case 0x3B:	/* 3B CMP Gv Ev */
            temp1 = (opcode >> 3) & 7;
            modregrm();
            oper1 = getreg16 (reg);
            oper2 = readrm16 (rm);
            switch (temp1) {
                case 0: res16 = op_add16(oper1, oper2, 0); break;
                case 1: res16 = op_or16(oper1, oper2);
                    if ( (oper1 == 0xF802) && (oper2 == 0xF802)) {
                        sf = 0; /*  cheap hack to make Wolf 3D think
                        we're a 286 so it plays */
                    }
                    break;

                case 2: res16 = op_add16(oper1, oper2, cf); break;
                case 3: res16 = op_sub16(oper1, oper2, cf); break;
                case 4: res16 = op_and16(oper1, oper2); break;
                case 7:
                case 5: res16 = op_sub16(oper1, oper2, 0); break;
                case 6: res16 = op_xor16(oper1, oper2); break;
            }
            if (opcode != 0x3B)
                getreg16 (reg) = res16;
            break;

        case 0x4:	/* 04 ADD regs.byteregs[regal] Ib */
        case 0xC:	/* 0C OR regs.byteregs[regal] Ib */
        case 0x14:	/* 14 ADC regs.byteregs[regal] Ib */
        case 0x1C:	/* 1C SBB regs.byteregs[regal] Ib */
        case 0x24:	/* 24 AND regs.byteregs[regal] Ib */
        case 0x2C:	/* 2C SUB regs.byteregs[regal] Ib */
        case 0x34:	/* 34 XOR regs.byteregs[regal] Ib */
        case 0x3C:	/* 3C CMP regs.byteregs[regal] Ib */
            temp1 = (opcode >> 3) & 7;
            oper1b = regs.byteregs[regal];
            oper2b = getmem8 (segregs[regcs], ip++);
            switch (temp1) {
                case 0: res8 = op_add8(oper1b, oper2b, 0); break;
                case 1: res8 = op_or8(oper1b, oper2b); break;
                case 2: res8 = op_add8(oper1b, oper2b, cf); break;
                case 3: res8 = op_sub8(oper1b, oper2b, cf); break;
                case 4: res8 = op_and8(oper1b, oper2b); break;
                case 7:
                case 5: res8 = op_sub8(oper1b, oper2b, 0); break;
                case 6: res8 = op_xor8(oper1b, oper2b); break;
            }
            if (opcode != 0x3C)
                regs.byteregs[regal] = res8;
            break;

        case 0x5:	/* 05 ADD eAX Iv */
        case 0xD:	/* 0D OR eAX Iv */
        case 0x15:	/* 15 ADC eAX Iv */
        case 0x1D:	/* 1D SBB eAX Iv */
        case 0x25:	/* 25 AND eAX Iv */
        case 0x2D:	/* 2D SUB eAX Iv */
        case 0x35:	/* 35 XOR eAX Iv */
        case 0x3D:	/* 3D CMP eAX Iv */
            temp1 = (opcode >> 3) & 7;
            oper1 = regs.wordregs[regax];
            oper2 = getmem16 (segregs[regcs], ip++);
            ip++;
            switch (temp1) {
                case 0: res16 = op_add16(oper1, oper2, 0); break;
                case 1: res16 = op_or16(oper1, oper2); break;
                case 2: res16 = op_add16(oper1, oper2, cf); break;
                case 3: res16 = op_sub16(oper1, oper2, cf); break;
                case 4: res16 = op_and16(oper1, oper2); break;
                case 7:
                case 5: res16 = op_sub16(oper1, oper2, 0); break;
                case 6: res16 = op_xor16(oper1, oper2); break;
            }
            if (opcode != 0x3D)
                regs.wordregs[regax] = res16;
            break;

        case 0x6:	/* 06 PUSH segregs[reges] */
        case 0xE:	/* 0E PUSH segregs[regcs] */
        case 0x16:	/* 16 PUSH segregs[regss] */
        case 0x1E:	/* 1E PUSH segregs[regds] */
            temp1 = (opcode >>3) & 3;
            push (segregs[temp1]);
            break;

        case 0x7:	/* 07 POP segregs[reges] */
        case 0x17:	/* 17 POP segregs[regss] */
        case 0x1F:	/* 1F POP segregs[regds] */
            temp1 = (opcode >>3) & 3;
            segregs[temp1] = pop();
            break;

#ifdef CPU_ALLOW_POP_CS //only the 8086/8088 does this.
        case 0xF: //0F POP CS
            segregs[regcs] = pop();
            break;
#endif

        case 0x27:	/* 27 DAA */
            if ( ( (regs.byteregs[regal] & 0xF) > 9) || (af == 1) ) {
                oper1 = regs.byteregs[regal] + 6;
                regs.byteregs[regal] = oper1 & 0xFF;
                if (oper1 & 0xFF00) {
                    cf = 1;
                } else {
                    cf = 0;
                }

                af = 1;
            } else {
                //af = 0;
            }

            if ( (regs.byteregs[regal]  > 0x9F) || (cf == 1) ) {
                regs.byteregs[regal] += 0x60;
                cf = 1;
            } else {
                //cf = 0;
            }

            regs.byteregs[regal] &= 0xFF;
            flag_szp8 (regs.byteregs[regal]);
            break;

        case 0x2F:	/* 2F DAS */
            if ( ( (regs.byteregs[regal] & 15) > 9) || (af == 1) ) {
                oper1 = regs.byteregs[regal] - 6;
                regs.byteregs[regal] = oper1 & 0xFF;
                if (oper1 & 0xFF00) {
                    cf = 1;
                } else {
                    cf = 0;
                }

                af = 1;
            } else {
                af = 0;
            }

            if ( ( (regs.byteregs[regal] & 0xF0) > 0x90) || (cf == 1) ) {
                regs.byteregs[regal] -= 0x60;
                cf = 1;
            } else {
                cf = 0;
            }

            flag_szp8 (regs.byteregs[regal]);
            break;

        case 0x37:	/* 37 AAA ASCII */
            if ( ( (regs.byteregs[regal] & 0xF) > 9) || (af == 1) ) {
                regs.byteregs[regal] += 6;
                regs.byteregs[regah] += 1;
                af = 1;
                cf = 1;
            } else {
                af = 0;
                cf = 0;
            }

            regs.byteregs[regal] &= 0xF;
            break;

        case 0x3F:	/* 3F AAS ASCII */
            if ( ( (regs.byteregs[regal] & 0xF) > 9) || (af == 1) ) {
                regs.byteregs[regal] -= 6;
                regs.byteregs[regah] -= 1;
                af = 1;
                cf = 1;
            } else {
                af = 0;
                cf = 0;
            }

            regs.byteregs[regal] &= 0xF;
            break;

        case 0x40:	/* 40 INC eAX */
        case 0x41:	/* 41 INC eCX */
        case 0x42:	/* 42 INC eDX */
        case 0x43:	/* 43 INC eBX */
        case 0x44:	/* 44 INC eSP */
        case 0x45:	/* 45 INC eBP */
        case 0x46:	/* 46 INC eSI */
        case 0x47:	/* 47 INC eDI */
            temp1 = opcode & 7;
            oldcf = cf;
            oper1 = regs.wordregs[temp1];
            res16 = op_add16(oper1, 1, 0);
            cf = oldcf;
            regs.wordregs[temp1] = res16;
            break;

        case 0x48:	/* 48 DEC eAX */
        case 0x49:	/* 49 DEC eCX */
        case 0x4A:	/* 4A DEC eDX */
        case 0x4B:	/* 4B DEC eBX */
        case 0x4C:	/* 4C DEC eSP */
        case 0x4D:	/* 4D DEC eBP */
        case 0x4E:	/* 4E DEC eSI */
        case 0x4F:	/* 4F DEC eDI */
            temp1 = opcode & 7;
            oldcf = cf;
            oper1 = regs.wordregs[temp1];
            res16 = op_sub16(oper1, 1, 0);
            cf = oldcf;
            regs.wordregs[temp1] = res16;
            break;

        case 0x54:	/* 54 PUSH eSP */
#ifdef USE_286_STYLE_PUSH_SP
            push (regs.wordregs[regsp]);
#else
            push (regs.wordregs[regsp] - 2);
#endif
            break;
        case 0x50:	/* 50 PUSH eAX */
        case 0x51:	/* 51 PUSH eCX */
        case 0x52:	/* 52 PUSH eDX */
        case 0x53:	/* 53 PUSH eBX */
        case 0x55:	/* 55 PUSH eBP */
        case 0x56:	/* 56 PUSH eSI */
        case 0x57:	/* 57 PUSH eDI */
            temp1 = opcode & 7;
            push (regs.wordregs[temp1]);
            break;

        case 0x58:	/* 58 POP eAX */
        case 0x59:	/* 59 POP eCX */
        case 0x5A:	/* 5A POP eDX */
        case 0x5B:	/* 5B POP eBX */
        case 0x5C:	/* 5C POP eSP */
        case 0x5D:	/* 5D POP eBP */
        case 0x5E:	/* 5E POP eSI */
        case 0x5F:	/* 5F POP eDI */
            temp1 = opcode & 7;
            regs.wordregs[temp1] = pop();
            break;

#ifndef CPU_8086
        case 0x60:	/* 60 PUSHA (80186+) */
            oldsp = regs.wordregs[regsp];
            push (regs.wordregs[regax]);
            push (regs.wordregs[regcx]);
            push (regs.wordregs[regdx]);
            push (regs.wordregs[regbx]);
            push (oldsp);
            push (regs.wordregs[regbp]);
            push (regs.wordregs[regsi]);
            push (regs.wordregs[regdi]);
            break;

        case 0x61:	/* 61 POPA (80186+) */
            regs.wordregs[regdi] = pop();
            regs.wordregs[regsi] = pop();
            regs.wordregs[regbp] = pop();
            temp16 = pop();
            regs.wordregs[regbx] = pop();
            regs.wordregs[regdx] = pop();
            regs.wordregs[regcx] = pop();
            regs.wordregs[regax] = pop();
            break;

        case 0x62: /* 62 BOUND Gv, Ev (80186+) */
            modregrm();
            getea (rm);
            if (signext32 (getreg16 (reg) ) < signext32 ( getmem16 (ea >> 4, ea & 15) ) ) {
                intcall86 (5); //bounds check exception
            } else {
                ea += 2;
                if (signext32 (getreg16 (reg) ) > signext32 ( getmem16 (ea >> 4, ea & 15) ) ) {
                    intcall86(5); //bounds check exception
                }
            }
            break;

        case 0x68:	/* 68 PUSH Iv (80186+) */
            push (getmem16 (segregs[regcs], ip++) );
            ip++;
            break;

        case 0x69:	/* 69 IMUL Gv Ev Iv (80186+) */
        case 0x6B:	/* 6B IMUL Gv Eb Ib (80186+) */
            modregrm();
            temp1 = readrm16 (rm);
            if(opcode == 0x69) {
                temp2 = getmem16 (segregs[regcs], ip++);
                ip++;
            } else {
                temp2 = signext (getmem8 (segregs[regcs], ip++) );
            }
            temp1 = signext32(temp1);
            temp2 = signext32(temp2);

            temp3 = temp1 * temp2;
            putreg16 (reg, temp3 & 0xFFFFL);
            if (temp3 & 0xFFFF0000L) {
                cf = 1;
                of = 1;
            } else {
                cf = 0;
                of = 0;
            }
            break;

        case 0x6A:	/* 6A PUSH Ib (80186+) */
            push (getmem8 (segregs[regcs], ip++) );
            break;

        case 0x6C:	/* 6C INSB */
        case 0x6E:	/* 6E OUTSB */
        case 0xA4:	/* A4 MOVSB */
            if (reptype && (regs.wordregs[regcx] == 0) ) {
                break;
            }
            if (opcode == 0x6C) {
                putmem8 (useseg, regs.wordregs[regsi],
                         portin (regs.wordregs[regdx]) );
            } else if (opcode == 0x6E) {
                portout (regs.wordregs[regdx],
                         getmem8 (useseg, regs.wordregs[regsi]) );
            } else {
                oper1b = getmem8 (useseg, regs.wordregs[regsi]);
                putmem8 (segregs[reges], regs.wordregs[regdi], oper1b);
            }
            if (df) {
                regs.wordregs[regsi]--;
                regs.wordregs[regdi]--;
            } else {
                regs.wordregs[regsi]++;
                regs.wordregs[regdi]++;
            }

            totalexec++;
            loopcount++;
            if (reptype) {
                regs.wordregs[regcx]--;
                ip = firstip;
            }
            break;

        case 0x6D:	/* 6F INSW */
        case 0x6F:	/* 6F OUTSW */
        case 0xA5:	/* A5 MOVSW */
            if (reptype && (regs.wordregs[regcx] == 0) ) {
                break;
            }
            if (opcode == 0x6D) {
                putmem16 (useseg, regs.wordregs[regsi],
                          portin16 (regs.wordregs[regdx]) );
            } else if(opcode == 0x6F) {
                portout16 (regs.wordregs[regdx],
                           getmem16 (useseg, regs.wordregs[regsi]) );
            } else {
                oper1 = getmem16 (useseg, regs.wordregs[regsi]);
                putmem16 (segregs[reges], regs.wordregs[regdi], oper1);
            }
            if (df) {
                regs.wordregs[regsi] -= 2;
                regs.wordregs[regdi] -= 2;
            } else {
                regs.wordregs[regsi] += 2;
                regs.wordregs[regdi] += 2;
            }

            totalexec++;
            loopcount++;
            if (reptype) {
                regs.wordregs[regcx]--;
                ip = firstip;
            }
            break;
#endif //CPU_8086

        case 0x70:	/* 70 JO Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (of) {
                ip += temp16;
            }
            break;

        case 0x71:	/* 71 JNO Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (!of) {
                ip += temp16;
            }
            break;

        case 0x72:	/* 72 JB Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (cf) {
                ip += temp16;
            }
            break;

        case 0x73:	/* 73 JNB Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (!cf) {
                ip += temp16;
            }
            break;

        case 0x74:	/* 74 JZ Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (zf) {
                ip += temp16;
            }
            break;

        case 0x75:	/* 75 JNZ Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (!zf) {
                ip += temp16;
            }
            break;

        case 0x76:	/* 76 JBE Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (cf || zf) {
                ip += temp16;
            }
            break;

        case 0x77:	/* 77 JA Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (!cf && !zf) {
                ip += temp16;
            }
            break;

        case 0x78:	/* 78 JS Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (sf) {
                ip += temp16;
            }
            break;

        case 0x79:	/* 79 JNS Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (!sf) {
                ip += temp16;
            }
            break;

        case 0x7A:	/* 7A JPE Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (pf) {
                ip += temp16;
            }
            break;

            case 0x7B:	/* 7B JPO Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (!pf) {
                ip += temp16;
            }
            break;

        case 0x7C:	/* 7C JL Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (sf != of) {
                ip += temp16;
            }
            break;

        case 0x7D:	/* 7D JGE Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (sf == of) {
                ip += temp16;
            }
            break;

        case 0x7E:	/* 7E JLE Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if ( (sf != of) || zf) {
                ip += temp16;
            }
            break;

        case 0x7F:	/* 7F JG Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (!zf && (sf == of) ) {
                ip += temp16;
            }
            break;

        case 0x80:
        case 0x82:	/* 80/82 GRP1 Eb Ib */
            modregrm();
            oper1b = readrm8 (rm);
            oper2b = getmem8 (segregs[regcs], ip++);
            switch (reg) {
            case 0: res8 = op_add8(oper1b, oper2b, 0); break;
            case 1: res8 = op_or8(oper1b, oper2b); break;
            case 2: res8 = op_add8(oper1b, oper2b, cf); break;
            case 3: res8 = op_sub8(oper1b, oper2b, cf); break;
            case 4: res8 = op_and8(oper1b, oper2b); break;
            case 7:
            case 5: res8 = op_sub8(oper1b, oper2b, 0); break;
            case 6: res8 = op_xor8(oper1b, oper2b); break;
            default:
                break;	/* to avoid compiler warnings */
            }

            if (reg < 7) {
                writerm8 (rm, res8);
            }
            break;

        case 0x81:	/* 81 GRP1 Ev Iv */
        case 0x83:	/* 83 GRP1 Ev Ib */
            modregrm();
            oper1 = readrm16 (rm);
            if (opcode == 0x81) {
                oper2 = getmem16 (segregs[regcs], ip++);
                ip++;
            } else {
                oper2 = signext (getmem8 (segregs[regcs], ip++) );
            }

            switch (reg) {
            case 0: res16 = op_add16(oper1, oper2, 0); break;
            case 1: res16 = op_or16(oper1, oper2); break;
            case 2: res16 = op_add16(oper1, oper2, cf); break;
            case 3: res16 = op_sub16(oper1, oper2, cf); break;
            case 4: res16 = op_and16(oper1, oper2); break;
            case 7:
            case 5: res16 = op_sub16(oper1, oper2, 0); break;
            case 6: res16 = op_xor16(oper1, oper2); break;
            default:
                break;	/* to avoid compiler warnings */
            }

            if (reg < 7) {
                writerm16 (rm, res16);
            }
            break;

        case 0x84:	/* 84 TEST Gb Eb */
            modregrm();
            oper1b = getreg8 (reg);
            oper2b = readrm8 (rm);
            flag_log8 (oper1b & oper2b);
            break;

        case 0x85:	/* 85 TEST Gv Ev */
            modregrm();
            oper1 = getreg16 (reg);
            oper2 = readrm16 (rm);
            flag_log16 (oper1 & oper2);
            break;

        case 0x86:	/* 86 XCHG Gb Eb */
            modregrm();
            oper1b = getreg8 (reg);
            putreg8 (reg, readrm8 (rm) );
            writerm8 (rm, oper1b);
            break;

        case 0x87:	/* 87 XCHG Gv Ev */
            modregrm();
            oper1 = getreg16 (reg);
            putreg16 (reg, readrm16 (rm) );
            writerm16 (rm, oper1);
            break;

        case 0x88:	/* 88 MOV Eb Gb */
            modregrm();
            writerm8 (rm, getreg8 (reg) );
            break;

        case 0x89:	/* 89 MOV Ev Gv */
            modregrm();
            writerm16 (rm, getreg16 (reg) );
            break;

        case 0x8A:	/* 8A MOV Gb Eb */
            modregrm();
            putreg8 (reg, readrm8 (rm) );
            break;

        case 0x8B:	/* 8B MOV Gv Ev */
            modregrm();
            putreg16 (reg, readrm16 (rm) );
            break;

        case 0x8C:	/* 8C MOV Ew Sw */
            modregrm();
            writerm16 (rm, getsegreg (reg) );
            break;

        case 0x8D:	/* 8D LEA Gv M */
            modregrm();
            getea (rm);
            putreg16 (reg, ea - segbase (useseg) );
            break;

        case 0x8E:	/* 8E MOV Sw Ew */
            modregrm();
            putsegreg (reg, readrm16 (rm) );
            break;

        case 0x8F:	/* 8F POP Ev */
            modregrm();
            writerm16 (rm, pop() );
            break;

        case 0x90:	/* 90 NOP */
            break;

        case 0x91:	/* 91 XCHG eCX eAX */
        case 0x92:	/* 92 XCHG eDX eAX */
        case 0x93:	/* 93 XCHG eBX eAX */
        case 0x94:	/* 94 XCHG eSP eAX */
        case 0x95:	/* 95 XCHG eBP eAX */
        case 0x96:	/* 96 XCHG eSI eAX */
        case 0x97:	/* 97 XCHG eDI eAX */
            temp1 = opcode & 7;
            oper1 = regs.wordregs[temp1];
            regs.wordregs[temp1] = regs.wordregs[regax];
            regs.wordregs[regax] = oper1;
            break;

        case 0x98:	/* 98 CBW */
            regs.wordregs[regax] = (int8_t)regs.byteregs[regal];
            break;

        case 0x99:	/* 99 CWD */
            if(regs.wordregs[regax] & 0x8000)
                regs.wordregs[regdx] = 0xFFFF;
            else
                regs.wordregs[regdx] = 0;
            break;

        case 0x9A:	/* 9A CALL Ap */
            oper1 = getmem16 (segregs[regcs], ip++);
            ip++;
            oper2 = getmem16 (segregs[regcs], ip++);
            ip++;
            push (segregs[regcs]);
            push (ip);
            ip = oper1;
            segregs[regcs] = oper2;
            break;

        case 0x9B:	/* 9B WAIT */
            break;

        case 0x9C:	/* 9C PUSHF */
#ifdef CPU_SET_HIGH_FLAGS
            push (makeflagsword() | 0xF800);
#else
            push (makeflagsword() | 0x0800);
#endif
            break;

        case 0x9D:	/* 9D POPF */
            temp16 = pop();
            decodeflagsword (temp16);
            break;

        case 0x9E:	/* 9E SAHF */
            decodeflagsword ( (makeflagsword() & 0xFF00) | regs.byteregs[regah]);
            break;

        case 0x9F:	/* 9F LAHF */
            regs.byteregs[regah] = makeflagsword() & 0xFF;
            break;

        case 0xA0:	/* A0 MOV regs.byteregs[regal] Ob */
            regs.byteregs[regal] = getmem8 (useseg, getmem16 (segregs[regcs], ip++) );
            ip++;
            break;

        case 0xA1:	/* A1 MOV eAX Ov */
            oper1 = getmem16 (useseg, getmem16 (segregs[regcs], ip++) );
            ip++;
            regs.wordregs[regax] = oper1;
            break;

        case 0xA2:	/* A2 MOV Ob regs.byteregs[regal] */
            oper1 = getmem16 (segregs[regcs], ip++);
            ip++;
            putmem8 (useseg, oper1, regs.byteregs[regal]);
            break;

        case 0xA3:	/* A3 MOV Ov eAX */
            oper1 = getmem16 (segregs[regcs], ip++);
            ip++;
            putmem16 (useseg, oper1, regs.wordregs[regax]);
            break;

        case 0xA6:	/* A6 CMPSB */
            if (reptype && (regs.wordregs[regcx] == 0) ) {
                break;
            }

            oper1b = getmem8 (useseg, regs.wordregs[regsi]);
            oper2b = getmem8 (segregs[reges], regs.wordregs[regdi]);
            if (df) {
                regs.wordregs[regsi]--;
                regs.wordregs[regdi]--;
            } else {
                regs.wordregs[regsi]++;
                regs.wordregs[regdi]++;
            }

            op_sub8 (oper1b, oper2b, 0);
            totalexec++;
            loopcount++;
            if (reptype) {
                regs.wordregs[regcx]--;
                if ( (reptype == 1) && !zf) {
                    break;
                }
                if ( (reptype == 2) && zf ) {
                    break;
                }
                ip = firstip;
            }
            break;

        case 0xA7:	/* A7 CMPSW */
            if (reptype && (regs.wordregs[regcx] == 0) ) {
                break;
            }

            oper1 = getmem16 (useseg,regs.wordregs[regsi]);
            oper2 = getmem16 (segregs[reges], regs.wordregs[regdi]);
            if (df) {
                regs.wordregs[regsi] -= 2;
                regs.wordregs[regdi] -= 2;
            } else {
                regs.wordregs[regsi] += 2;
                regs.wordregs[regdi] += 2;
            }

            op_sub16 (oper1, oper2, 0);
            totalexec++;
            loopcount++;
            if (reptype) {
                regs.wordregs[regcx]--;
                if ( (reptype == 1) && !zf) {
                    break;
                }
                if ( (reptype == 2) && zf ) {
                    break;
                }
                ip = firstip;
            }
            break;

        case 0xA8:	/* A8 TEST regs.byteregs[regal] Ib */
            oper1b = regs.byteregs[regal];
            oper2b = getmem8 (segregs[regcs], ip++);
            flag_log8 (oper1b & oper2b);
            break;

        case 0xA9:	/* A9 TEST eAX Iv */
            oper1 = regs.wordregs[regax];
            oper2 = getmem16 (segregs[regcs], ip++);
            ip++;
            flag_log16 (oper1 & oper2);
            break;

        case 0xAA:	/* AA STOSB */
            if (reptype && (regs.wordregs[regcx] == 0) ) {
                break;
            }

            putmem8 (segregs[reges], regs.wordregs[regdi], regs.byteregs[regal]);
            if (df) {
                regs.wordregs[regdi]--;
            } else {
                regs.wordregs[regdi]++;
            }

            totalexec++;
            loopcount++;
            if (reptype) {
                regs.wordregs[regcx]--;
                ip = firstip;
            }
            break;

        case 0xAB:	/* AB STOSW */
            if (reptype && (regs.wordregs[regcx] == 0) ) {
                break;
            }

            putmem16 (segregs[reges], regs.wordregs[regdi], regs.wordregs[regax]);
            if (df) {
                regs.wordregs[regdi] -= 2;
            } else {
                regs.wordregs[regdi] += 2;
            }

            totalexec++;
            loopcount++;
            if (reptype) {
                regs.wordregs[regcx]--;
                ip = firstip;
            }
            break;

        case 0xAC:	/* AC LODSB */
            if (reptype && (regs.wordregs[regcx] == 0) ) {
                break;
            }

            regs.byteregs[regal] = getmem8 (useseg, regs.wordregs[regsi]);
            if (df) {
                regs.wordregs[regsi]--;
            } else {
                regs.wordregs[regsi]++;
            }

            totalexec++;
            loopcount++;
            if (reptype) {
                regs.wordregs[regcx]--;
                ip = firstip;
            }
            break;

        case 0xAD:	/* AD LODSW */
            if (reptype && (regs.wordregs[regcx] == 0) ) {
                break;
            }

            regs.wordregs[regax] = getmem16 (useseg, regs.wordregs[regsi]);
            if (df) {
                regs.wordregs[regsi] -= 2;
            } else {
                regs.wordregs[regsi] += 2;
            }

            totalexec++;
            loopcount++;
            if (reptype) {
                regs.wordregs[regcx]--;
                ip = firstip;
            }
            break;

        case 0xAE:	/* AE SCASB */
            if (reptype && (regs.wordregs[regcx] == 0) ) {
                break;
            }

            oper1b = regs.byteregs[regal];
            oper2b = getmem8 (segregs[reges], regs.wordregs[regdi]);
            op_sub8 (oper1b, oper2b, 0);
            if (df) {
                regs.wordregs[regdi]--;
            } else {
                regs.wordregs[regdi]++;
            }

            totalexec++;
            loopcount++;
            if (reptype) {
                regs.wordregs[regcx]--;
                if ( (reptype == 1) && !zf) {
                    break;
                }
                if ( (reptype == 2) && zf ) {
                    break;
                }
                ip = firstip;
            }
            break;

        case 0xAF:	/* AF SCASW */
            if (reptype && (regs.wordregs[regcx] == 0) ) {
                break;
            }

            oper1 = regs.wordregs[regax];
            oper2 = getmem16 (segregs[reges], regs.wordregs[regdi]);
            op_sub16 (oper1, oper2, 0);
            if (df) {
                regs.wordregs[regdi] -= 2;
            } else {
                regs.wordregs[regdi] += 2;
            }

            totalexec++;
            loopcount++;
            if (reptype) {
                regs.wordregs[regcx]--;
                if ( (reptype == 1) && !zf) {
                    break;
                }
                if ( (reptype == 2) && zf ) {
                    break;
                }
                ip = firstip;
            }
            break;

        case 0xB0:	/* B0 MOV regs.byteregs[regal] Ib */
        case 0xB1:	/* B1 MOV regs.byteregs[regcl] Ib */
        case 0xB2:	/* B2 MOV regs.byteregs[regdl] Ib */
        case 0xB3:	/* B3 MOV regs.byteregs[regbl] Ib */
        case 0xB4:	/* B4 MOV regs.byteregs[regah] Ib */
        case 0xB5:	/* B5 MOV regs.byteregs[regch] Ib */
        case 0xB6:	/* B6 MOV regs.byteregs[regdh] Ib */
        case 0xB7:	/* B7 MOV regs.byteregs[regbh] Ib */
            temp1 = opcode & 7;
            regs.byteregs[byteregtable[temp1]] = getmem8 (segregs[regcs], ip++);
            break;

        case 0xB8:	/* B8 MOV eAX Iv */
        case 0xB9:	/* B9 MOV eCX Iv */
        case 0xBA:	/* BA MOV eDX Iv */
        case 0xBB:	/* BB MOV eBX Iv */
        case 0xBC:	/* BC MOV eSP Iv */
        case 0xBD:	/* BD MOV eBP Iv */
        case 0xBE:	/* BE MOV eSI Iv */
        case 0xBF:	/* BF MOV eDI Iv */
            temp1 = opcode & 7;
            regs.wordregs[temp1] = getmem16 (segregs[regcs], ip++);
            ip++;
            break;

        case 0xC0:	/* C0 GRP2 byte imm8 (80186+) */
            modregrm();
            oper1b = readrm8 (rm);
            oper2b = getmem8 (segregs[regcs], ip++);
            res8 = op_grp2_8(oper1b, oper2b);
            writerm8 (rm, res8);
            break;

        case 0xC1:	/* C1 GRP2 word imm8 (80186+) */
            modregrm();
            oper1 = readrm16 (rm);
            oper2b = getmem8 (segregs[regcs], ip++);
            res16 = op_grp2_16 (oper1, oper2b);
            writerm16 (rm, res16);
            break;

        case 0xC2:	/* C2 RET Iw */
            oper1 = getmem16 (segregs[regcs], ip);
            ip = pop();
            regs.wordregs[regsp] += oper1;
            break;

        case 0xC3:	/* C3 RET */
            ip = pop();
            break;

        case 0xC4:	/* C4 LES Gv Mp */
            modregrm();
            getea (rm);
            putreg16 (reg, readw86 (ea));
            segregs[reges] = readw86 (ea + 2);
            break;

        case 0xC5:	/* C5 LDS Gv Mp */
            modregrm();
            getea (rm);
            putreg16 (reg, readw86 (ea));
            segregs[regds] = readw86 (ea + 2);
            break;

        case 0xC6:	/* C6 MOV Eb Ib */
            modregrm();
            writerm8 (rm, getmem8 (segregs[regcs], ip++) );
            break;

        case 0xC7:	/* C7 MOV Ev Iv */
            modregrm();
            writerm16 (rm, getmem16 (segregs[regcs], ip++) );
            ip++;
            break;

        case 0xC8:	/* C8 ENTER (80186+) */
            stacksize = getmem16 (segregs[regcs], ip++);
            ip++;
            nestlev = getmem8 (segregs[regcs], ip++);
            push (regs.wordregs[regbp]);
            frametemp = regs.wordregs[regsp];
            if (nestlev) {
                for (temp16 = 1; temp16 < nestlev; temp16++) {
                    regs.wordregs[regbp] -= 2;
                    push (regs.wordregs[regbp]);
                }

                push (regs.wordregs[regsp]);
            }

            regs.wordregs[regbp] = frametemp;
            regs.wordregs[regsp] = regs.wordregs[regbp] - stacksize;

            break;

        case 0xC9:	/* C9 LEAVE (80186+) */
            regs.wordregs[regsp] = regs.wordregs[regbp];
            regs.wordregs[regbp] = pop();
            break;

        case 0xCA:	/* CA RETF Iw */
            oper1 = getmem16 (segregs[regcs], ip);
            ip = pop();
            segregs[regcs] = pop();
            regs.wordregs[regsp] += oper1;
            break;

        case 0xCB:	/* CB RETF */
            ip = pop();
            segregs[regcs] = pop();
            break;

        case 0xCC:	/* CC INT 3 */
            intcall86 (3);
            break;

        case 0xCD:	/* CD INT Ib */
            oper1b = getmem8 (segregs[regcs], ip++);
            intcall86 (oper1b);
            break;

        case 0xCE:	/* CE INTO */
            if (of) {
                intcall86 (4);
            }
            break;

        case 0xCF:	/* CF IRET */
            ip = pop();
            segregs[regcs] = pop();
            decodeflagsword (pop() );

            /*
            * if (net.enabled) net.canrecv = 1;
            */
            break;

        case 0xD0:	/* D0 GRP2 Eb 1 */
            modregrm();
            oper1b = readrm8 (rm);
            res8 = op_grp2_8 (oper1b, 1);
            writerm8 (rm, res8);
            break;

        case 0xD1:	/* D1 GRP2 Ev 1 */
            modregrm();
            oper1 = readrm16 (rm);
            res16 = op_grp2_16 (oper1, 1);
            writerm16 (rm, res16);
            break;

        case 0xD2:	/* D2 GRP2 Eb regs.byteregs[regcl] */
            modregrm();
            oper1b = readrm8 (rm);
            res8 = op_grp2_8 (oper1b, regs.byteregs[regcl]);
            writerm8 (rm, res8);
            break;

        case 0xD3:	/* D3 GRP2 Ev regs.byteregs[regcl] */
            modregrm();
            oper1 = readrm16 (rm);
            res16 = op_grp2_16 (oper1, regs.byteregs[regcl]);
            writerm16 (rm, res16);
            break;

        case 0xD4:	/* D4 AAM I0 */
            oper1 = getmem8 (segregs[regcs], ip++);
            if (!oper1) {
                intcall86 (0);
                break;
            }	/* division by zero */

            regs.byteregs[regah] = (regs.byteregs[regal] / oper1) & 0xFF;
            regs.byteregs[regal] = (regs.byteregs[regal] % oper1) & 0xFF;
            flag_szp16 (regs.wordregs[regax]);
            break;

        case 0xD5:	/* D5 AAD I0 */
            oper1 = getmem8 (segregs[regcs], ip++);
            regs.byteregs[regal] = (regs.byteregs[regah] * oper1 + regs.byteregs[regal]) & 255;
            regs.byteregs[regah] = 0;
            flag_szp16 (regs.byteregs[regah] * oper1 + regs.byteregs[regal]);
            sf = 0;
            break;

        case 0xD6:	/* D6 XLAT on V20/V30, SALC on 8086/8088 */
#ifndef CPU_NO_SALC
            regs.byteregs[regal] = cf ? 0xFF : 0x00;
            break;
#endif

        case 0xD7:	/* D7 XLAT */
            regs.byteregs[regal] = read86(useseg * 16 + (regs.wordregs[regbx]) + regs.byteregs[regal]);
            break;

        case 0xD8:
        case 0xD9:
        case 0xDA:
        case 0xDB:
        case 0xDC:
        case 0xDE:
        case 0xDD:
        case 0xDF:	/* escape to x87 FPU (unsupported) */
            modregrm();
            break;

        case 0xE0:	/* E0 LOOPNZ Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if ((--regs.wordregs[regcx]) && !zf) {
                ip += temp16;
            }
            break;

        case 0xE1:	/* E1 LOOPZ Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if ((--regs.wordregs[regcx]) && zf ) {
                ip += temp16;
            }
            break;

        case 0xE2:	/* E2 LOOP Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (--regs.wordregs[regcx]) {
                ip += temp16;
            }
            break;

        case 0xE3:	/* E3 JCXZ Jb */
            temp16 = signext (getmem8 (segregs[regcs], ip++) );
            if (!regs.wordregs[regcx]) {
                ip += temp16;
            }
            break;

        case 0xE4:	/* E4 IN regs.byteregs[regal] Ib */
            oper1b = getmem8 (segregs[regcs], ip++);
            regs.byteregs[regal] = portin (oper1b);
            break;

        case 0xE5:	/* E5 IN eAX Ib */
            oper1b = getmem8 (segregs[regcs], ip++);
            regs.wordregs[regax] = portin16 (oper1b);
            break;

        case 0xE6:	/* E6 OUT Ib regs.byteregs[regal] */
            oper1b = getmem8 (segregs[regcs], ip++);
            portout (oper1b, regs.byteregs[regal]);
            break;

        case 0xE7:	/* E7 OUT Ib eAX */
            oper1b = getmem8 (segregs[regcs], ip++);
            portout16 (oper1b, regs.wordregs[regax]);
            break;

        case 0xE8:	/* E8 CALL Jv */
            oper1 = getmem16 (segregs[regcs], ip++);
            ip++;
            push (ip);
            ip += oper1;
            break;

        case 0xE9:	/* E9 JMP Jv */
            oper1 = getmem16 (segregs[regcs], ip++);
            ip++;
            ip += oper1;
            break;

        case 0xEA:	/* EA JMP Ap */
            oper1 = getmem16 (segregs[regcs], ip++);
            ip++;
            oper2 = getmem16 (segregs[regcs], ip);
            ip = oper1;
            segregs[regcs] = oper2;
            break;

        case 0xEB:	/* EB JMP Jb */
            oper1 = signext (getmem8 (segregs[regcs], ip++) );
            ip += oper1;
            break;

        case 0xEC:	/* EC IN regs.byteregs[regal] regdx */
            oper1 = regs.wordregs[regdx];
            regs.byteregs[regal] = portin (oper1);
            break;

        case 0xED:	/* ED IN eAX regdx */
            oper1 = regs.wordregs[regdx];
            regs.wordregs[regax] = portin16 (oper1);
            break;

        case 0xEE:	/* EE OUT regdx regs.byteregs[regal] */
            oper1 = regs.wordregs[regdx];
            portout (oper1, regs.byteregs[regal]);
            break;

        case 0xEF:	/* EF OUT regdx eAX */
            oper1 = regs.wordregs[regdx];
            portout16 (oper1, regs.wordregs[regax]);
            break;

        case 0xF0:	/* F0 LOCK */
            break;

        case 0xF4:	/* F4 HLT */
            hltstate = 1;
            break;

        case 0xF5:	/* F5 CMC */
            cf = 1 - cf;
            break;

        case 0xF6:	/* F6 GRP3a Eb */
            modregrm();
            oper1b = readrm8 (rm);
            res8 = op_grp3_8(oper1b);
            if ( (reg > 1) && (reg < 4) ) {
                writerm8 (rm, res8);
            }
            break;

        case 0xF7:	/* F7 GRP3b Ev */
            modregrm();
            oper1 = readrm16 (rm);
            res16 = op_grp3_16(oper1);
            if ( (reg > 1) && (reg < 4) ) {
                writerm16 (rm, res16);
            }
            break;

        case 0xF8:	/* F8 CLC */
            cf = 0;
            break;

        case 0xF9:	/* F9 STC */
            cf = 1;
            break;

        case 0xFA:	/* FA CLI */
            ifl = 0;
            break;

        case 0xFB:	/* FB STI */
            ifl = 1;
            break;

        case 0xFC:	/* FC CLD */
            df = 0;
            break;

        case 0xFD:	/* FD STD */
            df = 1;
            break;

        case 0xFE:	/* FE GRP4 Eb */
            modregrm();
            oper1b = readrm8 (rm);
            temp16 = cf;
            if (!reg) {
                res8 = op_add8 (oper1b, 1, 0);
            } else {
                res8 = op_sub8 (oper1b, 1, 0);
            }
            cf = temp16;
            writerm8 (rm, res8);
            break;

        case 0xFF:	/* FF GRP5 Ev */
            modregrm();
            oper1 = readrm16 (rm);
            res16 = op_grp5(oper1);
            if(reg < 2) {
                writerm16(rm, res16);
            }
            break;

        default:
#ifdef CPU_ALLOW_ILLEGAL_OP_EXCEPTION
            intcall86 (6); /* trip invalid opcode exception (this occurs on the 80186+, 8086/8088 CPUs treat them as NOPs. */
                        /* technically they aren't exactly like NOPs in most cases, but for our pursoses, that's accurate enough. */
#endif
            if (verbose) {
                printf ("Illegal opcode: %02X %02X /%X @ %04X:%04X\n", getmem8(savecs, saveip), getmem8(savecs, saveip+1), (getmem8(savecs, saveip+2) >> 3) & 7, savecs, saveip);
            }
            break;
        }
    }
}
#endif   //CPU_INSTRUCTION_FLOW_CACHE
