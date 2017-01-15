/*
 * ============================================================================
 *
 *       Filename:  ports.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  01/12/2017 11:55:50 AM
 *       Revision:  none
 *       Compiler: 
 *
 *         Author:  Fang Yuan (yfang@nju.edu.cn)
 *   Organization:  nju
 *
 * ============================================================================
 */

#ifndef _PORTS_H
#define _PORTS_H

#include <stdint.h>

#define ADLIBPORT   (0x388)
#define PARPORT     (0x378)

void portout (uint16_t portnum, uint8_t value);

uint8_t portin (uint16_t portnum);

void portout16 (uint16_t portnum, uint16_t value);

uint16_t portin16 (uint16_t portnum);

void set_port_write_redirector (uint16_t start, uint16_t end, void *callback);

void set_port_read_redirector (uint16_t start, uint16_t end, void *callback);

#endif //_PORTS_H
