#include "shoryu.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define shoryu_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/time.h>
#define shoryu_mkdir(p) mkdir((p), 0755)
#endif

volatile int g_run = 1, g_reload = 0;
char g_dir[1024] = ".";
char g_config[1200];

static void dirname_of(const char *path, char *out, size_t cap){
    size_t n=strlen(path);
    while(n>0 && path[n-1]!='/' && path[n-1]!='\\') n--;
    if(n==0){ strncpy(out,".",cap-1); out[cap-1]=0; return; }
    if(n>=cap) n=cap-1;
    memcpy(out,path,n);
    if(n>1 && (out[n-1]=='/'||out[n-1]=='\\')) n--;
    out[n]=0;
}

void resolve_config(int argc, char **argv){
    const char *cand=NULL;
    const char *home;
    if(argc>1) cand=argv[1];
    else if(getenv("SHORYUMUX_CONFIG")) cand=getenv("SHORYUMUX_CONFIG");
    if(cand){ strncpy(g_config,cand,sizeof(g_config)-1); g_config[sizeof(g_config)-1]=0; }
    else {
        home=getenv("HOME");
#ifdef _WIN32
        if(!home) home=getenv("USERPROFILE");
#endif
        if(home) snprintf(g_config,sizeof(g_config),"%s/.config/shoryumux/config.conf",home);
        else strncpy(g_config,"./shoryumux.conf",sizeof(g_config)-1);
    }
    dirname_of(g_config, g_dir, sizeof(g_dir));
    shoryu_mkdir(g_dir);
#ifdef _WIN32
    { char parent[1024]; dirname_of(g_dir, parent, sizeof(parent)); shoryu_mkdir(parent); shoryu_mkdir(g_dir); }
#else
    { char tmp[1200], parent[1024];
      strncpy(tmp,g_dir,sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
      dirname_of(tmp, parent, sizeof(parent));
      shoryu_mkdir(parent);
      shoryu_mkdir(g_dir);
    }
#endif
}

static void trim_log(const char *path, long long sz){
    FILE *f; char *buf; size_t n; int c;
    if(sz<=LOG_MAX) return;
    f=fopen(path,"rb"); if(!f) return;
#ifdef _WIN32
    if(_fseeki64(f, -(long long)LOG_KEEP, SEEK_END)!=0){ fclose(f); return; }
#else
    if(fseeko(f, -(off_t)LOG_KEEP, SEEK_END)!=0){ fclose(f); return; }
#endif
    while((c=fgetc(f))!=EOF && c!='\n') {}
    buf=(char*)malloc(LOG_KEEP);
    if(!buf){ fclose(f); return; }
    n=fread(buf,1,LOG_KEEP,f);
    fclose(f);
    f=fopen(path,"wb");
    if(f){ fwrite(buf,1,n,f); fclose(f); }
    free(buf);
}

static long long now_ms(void){
#ifdef _WIN32
    static const long long EPOCH_DIFF = 11644473600000LL;
    FILETIME ft; ULARGE_INTEGER u;
    GetSystemTimeAsFileTime(&ft);
    u.LowPart=ft.dwLowDateTime; u.HighPart=ft.dwHighDateTime;
    return (long long)(u.QuadPart/10000) - EPOCH_DIFF;
#else
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (long long)tv.tv_sec*1000 + tv.tv_usec/1000;
#endif
}

void log_event(const char *btn, const char *desc){
    char path[1200];
    FILE *f; long long sz=0;
    snprintf(path,sizeof(path),"%s/events.log",g_dir);
    f=fopen(path,"a"); if(!f) return;
    fprintf(f,"%lld\t%s\t%s\n",now_ms(),btn,desc);
    fclose(f);
    f=fopen(path,"rb");
    if(f){
#ifdef _WIN32
        _fseeki64(f,0,SEEK_END); sz=_ftelli64(f);
#else
        fseeko(f,0,SEEK_END); sz=ftello(f);
#endif
        fclose(f);
    }
    if(sz>LOG_MAX) trim_log(path,sz);
}

void fire(const Action *a, const char *btn){
    int i;
    if(a->is_shell){
        printf("[%s] shell: %s\n",btn,a->shellcmd); fflush(stdout);
        log_event(btn,a->raw);
        fire_shell(a->shellcmd);
    } else {
        printf("[%s] keys: %s\n",btn,a->raw); fflush(stdout);
        log_event(btn,a->raw);
        for(i=0;i<a->nkeys;i++) output_send_key(a->keys[i].id, a->keys[i].mods);
    }
}

void write_status(const char *state){
    char p[1200];
    FILE *f;
    snprintf(p,sizeof(p),"%s/status",g_dir);
    f=fopen(p,"w"); if(!f) return;
    fprintf(f,"state=%s\nax=%d\npid=%d\n",state, output_is_trusted()?1:0, shoryu_pid());
    fclose(f);
}
