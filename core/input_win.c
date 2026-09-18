#include "shoryu.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <xinput.h>

#ifndef XINPUT_GAMEPAD_GUIDE
#define XINPUT_GAMEPAD_GUIDE 0x0400
#endif

typedef DWORD (WINAPI *XInputGetStateEx_t)(DWORD, XINPUT_STATE *);

static int g_user = -1;
static HMODULE g_xinput = NULL;
static XInputGetStateEx_t g_get_state_ex = NULL;

static DWORD get_state(DWORD idx, XINPUT_STATE *st){
    if(g_get_state_ex) return g_get_state_ex(idx, st);
    return XInputGetState(idx, st);
}

static void map_state(const XINPUT_STATE *st, int cur[B_COUNT]){
    WORD w=st->Gamepad.wButtons;
    memset(cur,0,sizeof(int)*B_COUNT);
    cur[B_UP]    = (w & XINPUT_GAMEPAD_DPAD_UP) ? 1 : 0;
    cur[B_DOWN]  = (w & XINPUT_GAMEPAD_DPAD_DOWN) ? 1 : 0;
    cur[B_LEFT]  = (w & XINPUT_GAMEPAD_DPAD_LEFT) ? 1 : 0;
    cur[B_RIGHT] = (w & XINPUT_GAMEPAD_DPAD_RIGHT) ? 1 : 0;
    cur[B_START] = (w & XINPUT_GAMEPAD_START) ? 1 : 0;
    cur[B_BACK]  = (w & XINPUT_GAMEPAD_BACK) ? 1 : 0;
    cur[B_L3]    = (w & XINPUT_GAMEPAD_LEFT_THUMB) ? 1 : 0;
    cur[B_R3]    = (w & XINPUT_GAMEPAD_RIGHT_THUMB) ? 1 : 0;
    cur[B_LB]    = (w & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 1 : 0;
    cur[B_RB]    = (w & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1 : 0;
    cur[B_GUIDE] = (w & XINPUT_GAMEPAD_GUIDE) ? 1 : 0;
    cur[B_A]     = (w & XINPUT_GAMEPAD_A) ? 1 : 0;
    cur[B_B]     = (w & XINPUT_GAMEPAD_B) ? 1 : 0;
    cur[B_X]     = (w & XINPUT_GAMEPAD_X) ? 1 : 0;
    cur[B_Y]     = (w & XINPUT_GAMEPAD_Y) ? 1 : 0;
    cur[B_LT]    = (st->Gamepad.bLeftTrigger > 0) ? 1 : 0;
    cur[B_RT]    = (st->Gamepad.bRightTrigger > 0) ? 1 : 0;
}

int input_open(int verbose){
    int i;
    XINPUT_STATE st;
    (void)verbose;
    if(!g_xinput){
        g_xinput=LoadLibraryA("xinput1_4.dll");
        if(!g_xinput) g_xinput=LoadLibraryA("xinput1_3.dll");
        if(g_xinput) g_get_state_ex=(XInputGetStateEx_t)GetProcAddress(g_xinput,(LPCSTR)100);
    }
    for(i=0;i<4;i++){
        if(get_state((DWORD)i,&st)==ERROR_SUCCESS){
            g_user=i;
            return 0;
        }
    }
    g_user=-1;
    return 1;
}

void input_close(void){ g_user=-1; }

int input_poll(int cur[B_COUNT]){
    XINPUT_STATE st;
    if(g_user<0) return -1;
    if(get_state((DWORD)g_user,&st)!=ERROR_SUCCESS) return -1;
    map_state(&st, cur);
    Sleep(10);
    return 0;
}
