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

/* ports.c: functions to handle port I/O from the CPU module, as well
   as functions for emulated hardware components to register their
   read/write callback functions across the port address range. */

#include <stdint.h>
#include <stdio.h>

void *port_write_callback[0x10000];
void *port_read_callback[0x10000];

extern uint8_t verbose;
void portout (uint16_t portnum, uint8_t value)
{
    void (*do_callback_write) (uint16_t portnum, uint8_t value) = NULL;
    //if (verbose) printf("portout(0x%X, 0x%02X);\n", portnum, value);

    do_callback_write = (void (*) (uint16_t portnum, uint8_t value) ) port_write_callback[portnum];
    if (do_callback_write != (void *) 0) {
        (*do_callback_write) (portnum, value);
    }
}

void portout16 (uint16_t portnum, uint16_t value)
{
    portout (portnum, (uint8_t) value);
    portout (portnum + 1, (uint8_t) (value >> 8) );
}

uint8_t portin (uint16_t portnum)
{
    uint8_t (*do_callback_read) (uint16_t portnum) = NULL;
    //if (verbose) printf("portin(0x%X);\n", portnum);

    do_callback_read = (uint8_t (*) (uint16_t portnum) ) port_read_callback[portnum];
    if (do_callback_read != (void *) 0)
        return ( (*do_callback_read) (portnum) );
    return (0xFF);
}

uint16_t portin16 (uint16_t portnum)
{
    uint16_t ret;

    ret = (uint16_t) portin (portnum);
    ret |= (uint16_t) portin (portnum+1) << 8;
    return (ret);
}

void set_port_write_redirector (uint16_t start, uint16_t end, void *callback)
{
    uint16_t i;
    for (i=start; i<=end; i++) {
        port_write_callback[i] = callback;
    }
}

void set_port_read_redirector (uint16_t start, uint16_t end, void *callback)
{
    uint16_t i;
    for (i=start; i<=end; i++) {
        port_read_callback[i] = callback;
    }
}
