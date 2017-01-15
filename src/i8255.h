/*
 * ============================================================================
 *
 *       Filename:  i8255.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  01/15/2017 10:26:42 AM
 *       Revision:  none
 *       Compiler: 
 *
 *         Author:  Fang Yuan (yfang@nju.edu.cn)
 *   Organization:  nju
 *
 * ============================================================================
 */

#ifndef _I8255_H
#define _I8255_H

struct i8255_s {
	uint8_t portA; // 8255 port A
	uint8_t portB; // 8255 port B
	uint8_t portC; // 8255 port C
	uint8_t control; // control register
};

#endif
