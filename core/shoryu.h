#ifndef SHORYU_H
#define SHORYU_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#define VID 0x0F0D
#define PID 0x01C7
#define LOG_MAX  (256 * 1024)
#define LOG_KEEP (64 * 1024)

#ifdef _WIN32
#define shoryu_strcasecmp _stricmp
#define shoryu_strncasecmp _strnicmp
#else
#define shoryu_strcasecmp strcasecmp
#define shoryu_strncasecmp strncasecmp
#endif

enum {
    B_UP, B_DOWN, B_LEFT, B_RIGHT,
    B_START, B_BACK, B_L3, B_R3,
    B_LB, B_RB, B_GUIDE,
    B_A, B_B, B_X, B_Y,
    B_LT, B_RT,
    B_COUNT
};

extern const char *BTN_NAMES[B_COUNT];
int name_to_btn(const char *n);

#define MOD_CMD   1u
#define MOD_CTRL  2u
#define MOD_ALT   4u
#define MOD_SHIFT 8u

enum {
    KEYID_NONE = 0,
    KEYID_A, KEYID_B, KEYID_C, KEYID_D, KEYID_E, KEYID_F, KEYID_G, KEYID_H,
    KEYID_I, KEYID_J, KEYID_K, KEYID_L, KEYID_M, KEYID_N, KEYID_O, KEYID_P,
    KEYID_Q, KEYID_R, KEYID_S, KEYID_T, KEYID_U, KEYID_V, KEYID_W, KEYID_X,
    KEYID_Y, KEYID_Z,
    KEYID_0, KEYID_1, KEYID_2, KEYID_3, KEYID_4, KEYID_5, KEYID_6, KEYID_7, KEYID_8, KEYID_9,
    KEYID_MINUS, KEYID_EQUAL, KEYID_LBRACKET, KEYID_RBRACKET, KEYID_BACKSLASH,
    KEYID_SEMICOLON, KEYID_QUOTE, KEYID_COMMA, KEYID_DOT, KEYID_SLASH, KEYID_GRAVE,
    KEYID_ENTER, KEYID_TAB, KEYID_SPACE, KEYID_DELETE, KEYID_FDEL, KEYID_ESC,
    KEYID_LEFT, KEYID_RIGHT, KEYID_DOWN, KEYID_UP,
    KEYID_HOME, KEYID_END, KEYID_PGUP, KEYID_PGDN,
    KEYID_F1, KEYID_F2, KEYID_F3, KEYID_F4, KEYID_F5, KEYID_F6,
    KEYID_F7, KEYID_F8, KEYID_F9, KEYID_F10, KEYID_F11, KEYID_F12
};

#define MAX_ACTIONS 64
#define MAX_TOKENS  16

typedef struct {
    int  btn;
    int  is_shell;
    char shellcmd[512];
    int  nkeys;
    struct { int id; unsigned mods; } keys[MAX_TOKENS];
    char raw[128];
} Action;

extern Action g_actions[MAX_ACTIONS];
extern int g_nactions;
extern volatile int g_run, g_reload;
extern char g_dir[1024];
extern char g_config[1200];

int  parse_key_token(const char *tok, int *id, unsigned *mods);
void parse_config(const char *path);
int  config_file_changed(void);
const Action *find_action(int btn);

void resolve_config(int argc, char **argv);
void log_event(const char *btn, const char *desc);
void fire(const Action *a, const char *btn);
void write_status(const char *state);

void platform_init(void);
void sleep_ms(int ms);
void fire_shell(const char *cmd);
int  acquire_singleton(void);
void release_singleton(void);
int  shoryu_pid(void);
int  shoryu_pid_alive(int pid);

void xinput_decode_report(const uint8_t *d, int cur[B_COUNT]);

int  input_open(int verbose);
void input_close(void);
int  input_poll(int cur[B_COUNT]); /* 0=report, 1=idle, -1=lost */

int  output_init(void);
void output_shutdown(void);
int  output_is_trusted(void);
const char *output_trust_hint(void);
void output_send_key(int key_id, unsigned mods);

#endif
