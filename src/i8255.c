/*
 * ============================================================================
 *
 *       Filename:  i8255.c
 *
 *    Description:  
 *        functions to emulate the Intel 8255 programmable parallel interface
 *
 *        Version:  1.0
 *        Created:  01/15/2017 10:29:22 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Fang Yuan (yfang@nju.edu.cn)
 *   Organization:  nju
 *
 * ============================================================================
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "i8255.h"
#include "ports.h"

struct i8255_s i8255;
extern uint8_t portram[];

void out8255 (uint16_t portnum, uint8_t value)
{
    portram[portnum] = value;   
}

uint8_t in8255 (uint16_t portnum)
{
    if(portnum == 0x62)
        return 0;
    return portram[portnum];
}

void init8255()
{
    memset (&i8255, 0, sizeof (i8255) );
    set_port_write_redirector (0x60, 0x64, &out8255);
    set_port_read_redirector (0x60, 0x64, &in8255);
}
