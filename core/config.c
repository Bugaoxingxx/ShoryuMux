#include "shoryu.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include <sys/stat.h>

#ifdef _WIN32
#include <sys/types.h>
#endif

const char *BTN_NAMES[B_COUNT] = {
    "UP","DOWN","LEFT","RIGHT",
    "START","BACK","L3","R3",
    "LB","RB","GUIDE",
    "A","B","X","Y",
    "LT","RT"
};

Action g_actions[MAX_ACTIONS];
int g_nactions = 0;
static time_t g_cfg_mtime = 0;

int name_to_btn(const char *n){
    int i;
    for(i=0;i<B_COUNT;i++) if(shoryu_strcasecmp(n,BTN_NAMES[i])==0) return i;
    return -1;
}

static int keyname_to_id(const char *t){
    if (t[0] && !t[1]) {
        char c=(char)tolower((unsigned char)t[0]);
        if(c>='a'&&c<='z') return KEYID_A + (c-'a');
        if(c>='0'&&c<='9') return KEYID_0 + (c-'0');
        switch(c){
            case '-': return KEYID_MINUS;
            case '=': return KEYID_EQUAL;
            case '[': return KEYID_LBRACKET;
            case ']': return KEYID_RBRACKET;
            case '\\': return KEYID_BACKSLASH;
            case ';': return KEYID_SEMICOLON;
            case '\'': return KEYID_QUOTE;
            case ',': return KEYID_COMMA;
            case '.': return KEYID_DOT;
            case '/': return KEYID_SLASH;
            case '`': return KEYID_GRAVE;
        }
    }
    if(!shoryu_strcasecmp(t,"enter")||!shoryu_strcasecmp(t,"return")) return KEYID_ENTER;
    if(!shoryu_strcasecmp(t,"tab")) return KEYID_TAB;
    if(!shoryu_strcasecmp(t,"space")) return KEYID_SPACE;
    if(!shoryu_strcasecmp(t,"delete")||!shoryu_strcasecmp(t,"backspace")) return KEYID_DELETE;
    if(!shoryu_strcasecmp(t,"forwarddelete")||!shoryu_strcasecmp(t,"fdel")) return KEYID_FDEL;
    if(!shoryu_strcasecmp(t,"esc")||!shoryu_strcasecmp(t,"escape")) return KEYID_ESC;
    if(!shoryu_strcasecmp(t,"left")) return KEYID_LEFT;
    if(!shoryu_strcasecmp(t,"right")) return KEYID_RIGHT;
    if(!shoryu_strcasecmp(t,"down")) return KEYID_DOWN;
    if(!shoryu_strcasecmp(t,"up")) return KEYID_UP;
    if(!shoryu_strcasecmp(t,"home")) return KEYID_HOME;
    if(!shoryu_strcasecmp(t,"end")) return KEYID_END;
    if(!shoryu_strcasecmp(t,"pageup")||!shoryu_strcasecmp(t,"pgup")) return KEYID_PGUP;
    if(!shoryu_strcasecmp(t,"pagedown")||!shoryu_strcasecmp(t,"pgdn")) return KEYID_PGDN;
    if((t[0]=='f'||t[0]=='F')&&t[1]){
        int n=atoi(t+1);
        if(n>=1&&n<=12) return KEYID_F1 + (n-1);
    }
    return KEYID_NONE;
}

static unsigned mod_of(const char *m){
    if(!shoryu_strcasecmp(m,"cmd")||!shoryu_strcasecmp(m,"command")||
       !shoryu_strcasecmp(m,"super")||!shoryu_strcasecmp(m,"win")||
       !shoryu_strcasecmp(m,"meta")) return MOD_CMD;
    if(!shoryu_strcasecmp(m,"ctrl")||!shoryu_strcasecmp(m,"control")) return MOD_CTRL;
    if(!shoryu_strcasecmp(m,"opt")||!shoryu_strcasecmp(m,"alt")||
       !shoryu_strcasecmp(m,"option")) return MOD_ALT;
    if(!shoryu_strcasecmp(m,"shift")) return MOD_SHIFT;
    return 0;
}

int parse_key_token(const char *tok, int *id, unsigned *mods){
    char buf[64];
    char *save=NULL, *last=NULL, *part;
    unsigned f=0;
    strncpy(buf,tok,sizeof(buf)-1); buf[sizeof(buf)-1]=0;
#ifdef _WIN32
    part=strtok_s(buf,"+",&save);
#else
    part=strtok_r(buf,"+",&save);
#endif
    while(part){
        unsigned mf;
        while(*part==' ') part++;
        { char *e=part+strlen(part); while(e>part&&e[-1]==' ') *--e=0; }
        mf=mod_of(part);
        if(mf) f|=mf; else last=part;
#ifdef _WIN32
        part=strtok_s(NULL,"+",&save);
#else
        part=strtok_r(NULL,"+",&save);
#endif
    }
    if(!last) return 0;
    *id=keyname_to_id(last);
    if(*id==KEYID_NONE) return 0;
    *mods=f;
    return 1;
}

static void note_mtime(const char *path){
    struct stat st;
    if(stat(path,&st)==0) g_cfg_mtime=st.st_mtime;
}

void parse_config(const char *path){
    FILE *f=fopen(path,"r");
    char line[1024];
    if(!f){ fprintf(stderr,"cannot open config: %s\n",path); return; }
    g_nactions=0;
    while(fgets(line,sizeof(line),f)){
        char *h=strchr(line,'#'); if(h)*h=0;
        char *eq=strchr(line,'='); if(!eq) continue;
        char *name, *val, *ne, *ve;
        int btn;
        Action *a;
        *eq=0;
        name=line; val=eq+1;
        while(*name&&isspace((unsigned char)*name)) name++;
        ne=name+strlen(name); while(ne>name&&isspace((unsigned char)ne[-1])) *--ne=0;
        while(*val&&isspace((unsigned char)*val)) val++;
        ve=val+strlen(val); while(ve>val&&isspace((unsigned char)ve[-1])) *--ve=0;
        if(!*name||!*val) continue;
        btn=name_to_btn(name);
        if(btn<0){ fprintf(stderr,"  unknown button '%s' — skipped\n",name); continue; }
        if(g_nactions>=MAX_ACTIONS) break;
        a=&g_actions[g_nactions++];
        memset(a,0,sizeof(*a)); a->btn=btn;
        strncpy(a->raw,val,sizeof(a->raw)-1);
        if(!shoryu_strncasecmp(val,"shell:",6)){
            char *s=val+6; while(*s&&isspace((unsigned char)*s)) s++;
            a->is_shell=1;
            strncpy(a->shellcmd,s,sizeof(a->shellcmd)-1);
        } else {
            char *s=val; char buf[512]; char *save=NULL; char *tk;
            if(!shoryu_strncasecmp(val,"keys:",5)) s=val+5;
            strncpy(buf,s,sizeof(buf)-1); buf[sizeof(buf)-1]=0;
#ifdef _WIN32
            tk=strtok_s(buf," \t",&save);
#else
            tk=strtok_r(buf," \t",&save);
#endif
            while(tk&&a->nkeys<MAX_TOKENS){
                int id; unsigned fl;
                if(parse_key_token(tk,&id,&fl)){
                    a->keys[a->nkeys].id=id; a->keys[a->nkeys].mods=fl; a->nkeys++;
                } else fprintf(stderr,"  unknown key token '%s' in '%s' — skipped\n",tk,name);
#ifdef _WIN32
                tk=strtok_s(NULL," \t",&save);
#else
                tk=strtok_r(NULL," \t",&save);
#endif
            }
        }
    }
    fclose(f);
    note_mtime(path);
    printf("loaded %d action(s) from %s\n",g_nactions,path);
    fflush(stdout);
}

int config_file_changed(void){
    struct stat st;
    if(!g_config[0] || stat(g_config,&st)!=0) return 0;
    if(st.st_mtime==g_cfg_mtime) return 0;
    g_cfg_mtime=st.st_mtime;
    return 1;
}

const Action *find_action(int btn){
    int i;
    for(i=0;i<g_nactions;i++) if(g_actions[i].btn==btn) return &g_actions[i];
    return NULL;
}
