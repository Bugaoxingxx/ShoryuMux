#include "shoryu.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/time.h>

static int g_ufd = -1;

static int linux_code(int id){
    if(id>=KEYID_A && id<=KEYID_Z) return KEY_A + (id-KEYID_A);
    if(id>=KEYID_0 && id<=KEYID_9) return (id==KEYID_0) ? KEY_0 : (KEY_1 + (id-KEYID_1));
    if(id>=KEYID_F1 && id<=KEYID_F12) return KEY_F1 + (id-KEYID_F1);
    switch(id){
        case KEYID_MINUS: return KEY_MINUS;
        case KEYID_EQUAL: return KEY_EQUAL;
        case KEYID_LBRACKET: return KEY_LEFTBRACE;
        case KEYID_RBRACKET: return KEY_RIGHTBRACE;
        case KEYID_BACKSLASH: return KEY_BACKSLASH;
        case KEYID_SEMICOLON: return KEY_SEMICOLON;
        case KEYID_QUOTE: return KEY_APOSTROPHE;
        case KEYID_COMMA: return KEY_COMMA;
        case KEYID_DOT: return KEY_DOT;
        case KEYID_SLASH: return KEY_SLASH;
        case KEYID_GRAVE: return KEY_GRAVE;
        case KEYID_ENTER: return KEY_ENTER;
        case KEYID_TAB: return KEY_TAB;
        case KEYID_SPACE: return KEY_SPACE;
        case KEYID_DELETE: return KEY_BACKSPACE;
        case KEYID_FDEL: return KEY_DELETE;
        case KEYID_ESC: return KEY_ESC;
        case KEYID_LEFT: return KEY_LEFT;
        case KEYID_RIGHT: return KEY_RIGHT;
        case KEYID_DOWN: return KEY_DOWN;
        case KEYID_UP: return KEY_UP;
        case KEYID_HOME: return KEY_HOME;
        case KEYID_END: return KEY_END;
        case KEYID_PGUP: return KEY_PAGEUP;
        case KEYID_PGDN: return KEY_PAGEDOWN;
        default: return -1;
    }
}

static int emit(int type, int code, int val){
    struct input_event ie;
    memset(&ie,0,sizeof(ie));
    gettimeofday(&ie.time, NULL);
    ie.type=(uint16_t)type;
    ie.code=(uint16_t)code;
    ie.value=val;
    return write(g_ufd, &ie, sizeof(ie))==(ssize_t)sizeof(ie) ? 0 : -1;
}

int output_init(void){
    struct uinput_setup usetup;
    int id;
    g_ufd=open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if(g_ufd<0) g_ufd=open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
    if(g_ufd<0){
        fprintf(stderr,"cannot open /dev/uinput: %s\n", strerror(errno));
        fprintf(stderr,"  install linux/99-shoryumux.rules and add your user to group 'input', then re-log.\n");
        return -1;
    }
    ioctl(g_ufd, UI_SET_EVBIT, EV_KEY);
    ioctl(g_ufd, UI_SET_EVBIT, EV_SYN);
    ioctl(g_ufd, UI_SET_KEYBIT, KEY_LEFTCTRL);
    ioctl(g_ufd, UI_SET_KEYBIT, KEY_LEFTALT);
    ioctl(g_ufd, UI_SET_KEYBIT, KEY_LEFTSHIFT);
    ioctl(g_ufd, UI_SET_KEYBIT, KEY_LEFTMETA);
    for(id=KEYID_A; id<=KEYID_F12; id++){
        int c=linux_code(id);
        if(c>=0) ioctl(g_ufd, UI_SET_KEYBIT, c);
    }
    memset(&usetup,0,sizeof(usetup));
    snprintf(usetup.name, UINPUT_MAX_NAME_SIZE, "ShoryuMux");
    usetup.id.bustype=BUS_USB;
    usetup.id.vendor=VID;
    usetup.id.product=PID;
    if(ioctl(g_ufd, UI_DEV_SETUP, &usetup)<0){
        struct uinput_user_dev uud;
        memset(&uud,0,sizeof(uud));
        snprintf(uud.name, UINPUT_MAX_NAME_SIZE, "ShoryuMux");
        uud.id.bustype=BUS_USB;
        uud.id.vendor=VID;
        uud.id.product=PID;
        if(write(g_ufd,&uud,sizeof(uud))!=(ssize_t)sizeof(uud)){
            fprintf(stderr,"uinput setup failed: %s\n", strerror(errno));
            close(g_ufd); g_ufd=-1;
            return -1;
        }
    }
    if(ioctl(g_ufd, UI_DEV_CREATE)<0){
        fprintf(stderr,"UI_DEV_CREATE: %s\n", strerror(errno));
        close(g_ufd); g_ufd=-1;
        return -1;
    }
    return 0;
}

void output_shutdown(void){
    if(g_ufd>=0){
        ioctl(g_ufd, UI_DEV_DESTROY);
        close(g_ufd);
        g_ufd=-1;
    }
}

int output_is_trusted(void){ return g_ufd>=0 ? 1 : 0; }

const char *output_trust_hint(void){
    return "!! cannot write uinput yet — install linux/99-shoryumux.rules (or add user to group input) so keystrokes can be injected.";
}

void output_send_key(int key_id, unsigned mods){
    int code=linux_code(key_id);
    if(code<0 || g_ufd<0) return;
    if(mods & MOD_CMD){ emit(EV_KEY, KEY_LEFTMETA, 1); emit(EV_SYN, SYN_REPORT, 0); }
    if(mods & MOD_CTRL){ emit(EV_KEY, KEY_LEFTCTRL, 1); emit(EV_SYN, SYN_REPORT, 0); }
    if(mods & MOD_ALT){ emit(EV_KEY, KEY_LEFTALT, 1); emit(EV_SYN, SYN_REPORT, 0); }
    if(mods & MOD_SHIFT){ emit(EV_KEY, KEY_LEFTSHIFT, 1); emit(EV_SYN, SYN_REPORT, 0); }
    emit(EV_KEY, code, 1); emit(EV_SYN, SYN_REPORT, 0);
    usleep(12000);
    emit(EV_KEY, code, 0); emit(EV_SYN, SYN_REPORT, 0);
    if(mods & MOD_SHIFT){ emit(EV_KEY, KEY_LEFTSHIFT, 0); emit(EV_SYN, SYN_REPORT, 0); }
    if(mods & MOD_ALT){ emit(EV_KEY, KEY_LEFTALT, 0); emit(EV_SYN, SYN_REPORT, 0); }
    if(mods & MOD_CTRL){ emit(EV_KEY, KEY_LEFTCTRL, 0); emit(EV_SYN, SYN_REPORT, 0); }
    if(mods & MOD_CMD){ emit(EV_KEY, KEY_LEFTMETA, 0); emit(EV_SYN, SYN_REPORT, 0); }
    usleep(12000);
}
