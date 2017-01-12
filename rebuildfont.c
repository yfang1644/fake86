/*
 * ============================================================================
 *
 *       Filename:  pack.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  01/11/2017 05:15:50 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Fang Yuan (yfang@nju.edu.cn)
 *   Organization:  nju
 *
 * ============================================================================
 */


#include <stdio.h>
char buf[32768];
char nb[4096];
int main (int argc, char *argv[])
{
    FILE *fp;
    int i, x,y ;
    int cnt = 0;

    fp = fopen("asciivga.dat", "rb");
    fread(buf, 32768, 1, fp);
    for(i = 0; i < 4096; i++){
        y = 0;
        for(x = 0; x < 8; x++) {
            y = (y<<1) | buf[cnt++];
        }
        nb[i] = y;
    }
    fclose(fp);
    fp = fopen("nfont.dat", "wb");
    fwrite(nb, 4096, 1, fp);
    fclose(fp);
}
