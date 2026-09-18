#include "shoryu.h"

#include <unistd.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>

static CGEventSourceRef g_src=NULL;

static int macos_vk(int id){
    static const int az[26]={0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6};
    static const int d[10]={29,18,19,20,21,23,22,26,28,25};
    static const int fk[12]={122,120,99,118,96,97,98,100,101,109,103,111};
    if(id>=KEYID_A && id<=KEYID_Z) return az[id-KEYID_A];
    if(id>=KEYID_0 && id<=KEYID_9) return d[id-KEYID_0];
    if(id>=KEYID_F1 && id<=KEYID_F12) return fk[id-KEYID_F1];
    switch(id){
        case KEYID_MINUS: return 27;
        case KEYID_EQUAL: return 24;
        case KEYID_LBRACKET: return 33;
        case KEYID_RBRACKET: return 30;
        case KEYID_BACKSLASH: return 42;
        case KEYID_SEMICOLON: return 41;
        case KEYID_QUOTE: return 39;
        case KEYID_COMMA: return 43;
        case KEYID_DOT: return 47;
        case KEYID_SLASH: return 44;
        case KEYID_GRAVE: return 50;
        case KEYID_ENTER: return 36;
        case KEYID_TAB: return 48;
        case KEYID_SPACE: return 49;
        case KEYID_DELETE: return 51;
        case KEYID_FDEL: return 117;
        case KEYID_ESC: return 53;
        case KEYID_LEFT: return 123;
        case KEYID_RIGHT: return 124;
        case KEYID_DOWN: return 125;
        case KEYID_UP: return 126;
        case KEYID_HOME: return 115;
        case KEYID_END: return 119;
        case KEYID_PGUP: return 116;
        case KEYID_PGDN: return 121;
        default: return -1;
    }
}

static CGEventFlags macos_flags(unsigned mods){
    CGEventFlags f=0;
    if(mods & MOD_CMD) f|=kCGEventFlagMaskCommand;
    if(mods & MOD_CTRL) f|=kCGEventFlagMaskControl;
    if(mods & MOD_ALT) f|=kCGEventFlagMaskAlternate;
    if(mods & MOD_SHIFT) f|=kCGEventFlagMaskShift;
    return f;
}

int output_init(void){
    g_src=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    return g_src ? 0 : -1;
}

void output_shutdown(void){
    if(g_src){ CFRelease(g_src); g_src=NULL; }
}

int output_is_trusted(void){ return AXIsProcessTrusted() ? 1 : 0; }

const char *output_trust_hint(void){
    return "!! Accessibility not granted — keystrokes won't be delivered. Grant it in\n"
           "   System Settings → Privacy & Security → Accessibility (add shoryumuxd),\n"
           "   then Stop and Start the daemon.";
}

void output_send_key(int key_id, unsigned mods){
    int vk=macos_vk(key_id);
    CGEventFlags flags;
    CGEventRef kd, ku;
    if(vk<0 || !g_src) return;
    flags=macos_flags(mods);
    kd=CGEventCreateKeyboardEvent(g_src,(CGKeyCode)vk,true);
    if(!kd) return;
    CGEventSetFlags(kd,flags); CGEventPost(kCGHIDEventTap,kd); usleep(12000);
    ku=CGEventCreateKeyboardEvent(g_src,(CGKeyCode)vk,false);
    if(ku){ CGEventSetFlags(ku,flags); CGEventPost(kCGHIDEventTap,ku); CFRelease(ku); }
    CFRelease(kd); usleep(12000);
}
