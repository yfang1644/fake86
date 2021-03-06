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

/* console.c: functions for a simple interactive console on stdio. */

#include "config.h"
#include <SDL/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#define strcmpi _strcmpi
#else
#define strcmpi strcasecmp
#endif

extern uint8_t insertdisk (uint8_t drivenum, char *filename);
extern void ejectdisk (uint8_t drivenum);

void waitforcmd (int8_t *dst, uint16_t maxlen)
{
#ifdef _WIN32
    uint16_t inputptr = 0;
    uint8_t cc;

    maxlen -= 2;
    dst[0] = 0;
    while (1) {
        if (_kbhit () ) {
            cc = (uint8_t) _getch ();
            switch (cc) {
            case 0:
            case 9:
            case 10:
                break;
            case 8: //backspace
                if (inputptr > 0) {
                    printf ("%c %c", 8, 8);
                    dst[--inputptr] = 0;
                }
                break;
            case 13: //enter
                printf ("\n");
                return;
                default:
                if (inputptr < maxlen) {
                    dst[inputptr++] = cc;
                    dst[inputptr] = 0;
                    printf ("%c",cc);
                }
            }
        }
        SDL_Delay(10); //don't waste CPU time while in the polling loop
    }
#else
    int n;
    fgets(dst, maxlen, stdin);
    for (n = 0; n < maxlen; n++) {
        if (dst[n] == '\n') {
            dst[n] = '\0';
            break;
        }
    }
#endif  //_WIN32
}

void consolehelp ()
{
    printf ("\nConsole command summary:\n");
    printf ("  The console is not very robust yet. There are only a few commands:\n\n");
    printf ("    change fd0        Mount a new image file on first floppy drive.\n");
    printf ("                      Entering a blank line just ejects any current image file.\n");
    printf ("    change fd1        Mount a new image file on first floppy drive.\n");
    printf ("                      Entering a blank line just ejects any current image file.\n");
    printf ("    help              This help display.\n");
    printf ("    quit              Immediately abort emulation and close Fake86.\n");
}

#ifdef _WIN32
void runconsole (void *dummy)
#else
void *runconsole (void *dummy)
#endif
{
    int8_t inputline[1024];
    int *run = (int *)dummy;

    printf ("\nFake86 management console\n");
    printf ("Type \"help\" for a summary of commands.\n");
    while (*run) {
        printf ("\n>");
        waitforcmd (inputline, sizeof(inputline) );
        if (!strcmpi ( (const char *) inputline, "change fd0")) {
            printf ("Path to new image file: ");
            waitforcmd (inputline, sizeof(inputline) );
            if (strlen (inputline) > 0) {
                insertdisk (0, (char *) inputline);
            }
            else {
                ejectdisk (0);
                printf ("Floppy image ejected from first drive.\n");
            }
        } else if (!strcmpi ( (const char *) inputline, "change fd1")) {
            printf ("Path to new image file: ");
            waitforcmd (inputline, sizeof(inputline) );
            if (strlen (inputline) > 0) {
                insertdisk (1, (char *) inputline);
            } else {
                ejectdisk (1);
                printf ("Floppy image ejected from second drive.\n");
            }
        } else if (!strcmpi ( (const char *) inputline, "help")) {
            consolehelp ();
        } else if (!strcmpi ( (const char *) inputline, "quit")) {
            *run= 0;
        } else printf("Invalid command was entered.\n");
    }
#ifdef  _WIN32
    return;
#else
    return NULL;
#endif
}
