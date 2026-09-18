#include "shoryu.h"

#include <windows.h>

static WORD win_vk(int id){
    if(id>=KEYID_A && id<=KEYID_Z) return (WORD)('A' + (id-KEYID_A));
    if(id>=KEYID_0 && id<=KEYID_9) return (WORD)('0' + (id-KEYID_0));
    if(id>=KEYID_F1 && id<=KEYID_F12) return (WORD)(VK_F1 + (id-KEYID_F1));
    switch(id){
        case KEYID_MINUS: return VK_OEM_MINUS;
        case KEYID_EQUAL: return VK_OEM_PLUS;
        case KEYID_LBRACKET: return VK_OEM_4;
        case KEYID_RBRACKET: return VK_OEM_6;
        case KEYID_BACKSLASH: return VK_OEM_5;
        case KEYID_SEMICOLON: return VK_OEM_1;
        case KEYID_QUOTE: return VK_OEM_7;
        case KEYID_COMMA: return VK_OEM_COMMA;
        case KEYID_DOT: return VK_OEM_PERIOD;
        case KEYID_SLASH: return VK_OEM_2;
        case KEYID_GRAVE: return VK_OEM_3;
        case KEYID_ENTER: return VK_RETURN;
        case KEYID_TAB: return VK_TAB;
        case KEYID_SPACE: return VK_SPACE;
        case KEYID_DELETE: return VK_BACK;
        case KEYID_FDEL: return VK_DELETE;
        case KEYID_ESC: return VK_ESCAPE;
        case KEYID_LEFT: return VK_LEFT;
        case KEYID_RIGHT: return VK_RIGHT;
        case KEYID_DOWN: return VK_DOWN;
        case KEYID_UP: return VK_UP;
        case KEYID_HOME: return VK_HOME;
        case KEYID_END: return VK_END;
        case KEYID_PGUP: return VK_PRIOR;
        case KEYID_PGDN: return VK_NEXT;
        default: return 0;
    }
}

static void tap_vk(WORD vk, int down){
    INPUT in;
    memset(&in,0,sizeof(in));
    in.type=INPUT_KEYBOARD;
    in.ki.wVk=vk;
    if(!down) in.ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(1,&in,sizeof(INPUT));
}

int output_init(void){ return 0; }
void output_shutdown(void){}
int output_is_trusted(void){ return 1; }
const char *output_trust_hint(void){ return NULL; }

void output_send_key(int key_id, unsigned mods){
    WORD vk=win_vk(key_id);
    if(!vk) return;
    if(mods & MOD_CMD) tap_vk(VK_LWIN,1);
    if(mods & MOD_CTRL) tap_vk(VK_CONTROL,1);
    if(mods & MOD_ALT) tap_vk(VK_MENU,1);
    if(mods & MOD_SHIFT) tap_vk(VK_SHIFT,1);
    tap_vk(vk,1);
    Sleep(12);
    tap_vk(vk,0);
    if(mods & MOD_SHIFT) tap_vk(VK_SHIFT,0);
    if(mods & MOD_ALT) tap_vk(VK_MENU,0);
    if(mods & MOD_CTRL) tap_vk(VK_CONTROL,0);
    if(mods & MOD_CMD) tap_vk(VK_LWIN,0);
    Sleep(12);
}
