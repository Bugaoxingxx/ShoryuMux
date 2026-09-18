#include "shoryu.h"

#include <string.h>

void xinput_decode_report(const uint8_t *d, int cur[B_COUNT]){
    memset(cur,0,sizeof(int)*B_COUNT);
    cur[B_UP]    = (d[0]&0x01)?1:0;
    cur[B_DOWN]  = (d[0]&0x02)?1:0;
    cur[B_LEFT]  = (d[0]&0x04)?1:0;
    cur[B_RIGHT] = (d[0]&0x08)?1:0;
    cur[B_START] = (d[0]&0x10)?1:0;
    cur[B_BACK]  = (d[0]&0x20)?1:0;
    cur[B_L3]    = (d[0]&0x40)?1:0;
    cur[B_R3]    = (d[0]&0x80)?1:0;
    cur[B_LB]    = (d[1]&0x01)?1:0;
    cur[B_RB]    = (d[1]&0x02)?1:0;
    cur[B_GUIDE] = (d[1]&0x04)?1:0;
    cur[B_A]     = (d[1]&0x10)?1:0;
    cur[B_B]     = (d[1]&0x20)?1:0;
    cur[B_X]     = (d[1]&0x40)?1:0;
    cur[B_Y]     = (d[1]&0x80)?1:0;
    cur[B_LT]    = (d[2]>0)?1:0;
    cur[B_RT]    = (d[3]>0)?1:0;
}
