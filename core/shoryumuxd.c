// shoryumuxd.c — HORI XInput fight stick -> macOS keystrokes / shell actions.
//
// Reads the stick over raw USB (IOKit; no driver, no entitlement needed — verified),
// detects button press edges, and fires actions from a config file.
//
//   keys: <token> [token...]   synthesize keystrokes to the FRONTMOST app (CGEvent)
//   shell: <command>           run a shell command (async; does not block USB reads)
//
// Config path resolution (first that exists):
//   argv[1]  >  $SHORYUMUX_CONFIG  >  ~/.config/shoryumux/config.conf
//
// Signals:  SIGHUP = reload config   SIGINT/SIGTERM = quit
// Files (in the config's directory):  shoryumux.pid   events.log   status
//
// Build (see Makefile):
//   clang -O2 -o shoryumuxd shoryumuxd.c -framework IOKit -framework CoreFoundation \
//     -framework ApplicationServices -framework CoreGraphics

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <time.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>

#define VID 0x0F0D
#define PID 0x01C7
#define IN_PIPE 1
#define LOG_MAX  (256 * 1024)
#define LOG_KEEP (64 * 1024)

// ---------------------------------------------------------------- button model
enum {
    B_UP, B_DOWN, B_LEFT, B_RIGHT,
    B_START, B_BACK, B_L3, B_R3,
    B_LB, B_RB, B_GUIDE,
    B_A, B_B, B_X, B_Y,
    B_LT, B_RT,
    B_COUNT
};
static const char *BTN_NAMES[B_COUNT] = {
    "UP","DOWN","LEFT","RIGHT",
    "START","BACK","L3","R3",
    "LB","RB","GUIDE",
    "A","B","X","Y",
    "LT","RT"
};
static int name_to_btn(const char *n){
    for(int i=0;i<B_COUNT;i++) if(strcasecmp(n,BTN_NAMES[i])==0) return i;
    return -1;
}

// ---------------------------------------------------------------- config
#define MAX_ACTIONS 64
#define MAX_TOKENS  16
typedef struct {
    int  btn;
    int  is_shell;
    char shellcmd[512];
    int  nkeys;
    struct { CGKeyCode code; CGEventFlags flags; } keys[MAX_TOKENS];
    char raw[128];         // for event log / GUI
} Action;
static Action g_actions[MAX_ACTIONS];
static int g_nactions = 0;

static int keyname_to_code(const char *t, CGEventFlags *flags_out){
    *flags_out = 0;
    if (t[0] && !t[1]) {
        char c=(char)tolower((unsigned char)t[0]);
        if(c>='a'&&c<='z'){
            static const int kc[26]={0,11,8,2,14,3,5,4,34,38,40,37,46,45,31,35,12,15,1,17,32,9,13,7,16,6};
            return kc[c-'a'];
        }
        if(c>='0'&&c<='9'){ static const int d[10]={29,18,19,20,21,23,22,26,28,25}; return d[c-'0']; }
        switch(c){
            case '-':return 27; case '=':return 24; case '[':return 33; case ']':return 30;
            case '\\':return 42; case ';':return 41; case '\'':return 39; case ',':return 43;
            case '.':return 47; case '/':return 44; case '`':return 50;
        }
    }
    if(!strcasecmp(t,"enter")||!strcasecmp(t,"return"))return 36;
    if(!strcasecmp(t,"tab"))return 48;
    if(!strcasecmp(t,"space"))return 49;
    if(!strcasecmp(t,"delete")||!strcasecmp(t,"backspace"))return 51;
    if(!strcasecmp(t,"forwarddelete")||!strcasecmp(t,"fdel"))return 117;
    if(!strcasecmp(t,"esc")||!strcasecmp(t,"escape"))return 53;
    if(!strcasecmp(t,"left"))return 123;
    if(!strcasecmp(t,"right"))return 124;
    if(!strcasecmp(t,"down"))return 125;
    if(!strcasecmp(t,"up"))return 126;
    if(!strcasecmp(t,"home"))return 115;
    if(!strcasecmp(t,"end"))return 119;
    if(!strcasecmp(t,"pageup")||!strcasecmp(t,"pgup"))return 116;
    if(!strcasecmp(t,"pagedown")||!strcasecmp(t,"pgdn"))return 121;
    if((t[0]=='f'||t[0]=='F')&&t[1]){ int n=atoi(t+1);
        static const int fk[13]={0,122,120,99,118,96,97,98,100,101,109,103,111};
        if(n>=1&&n<=12) return fk[n]; }
    return -1;
}
static CGEventFlags mod_of(const char *m){
    if(!strcasecmp(m,"cmd")||!strcasecmp(m,"command"))return kCGEventFlagMaskCommand;
    if(!strcasecmp(m,"ctrl")||!strcasecmp(m,"control"))return kCGEventFlagMaskControl;
    if(!strcasecmp(m,"opt")||!strcasecmp(m,"alt")||!strcasecmp(m,"option"))return kCGEventFlagMaskAlternate;
    if(!strcasecmp(m,"shift"))return kCGEventFlagMaskShift;
    return 0;
}
static int parse_key_token(const char *tok, CGKeyCode *code, CGEventFlags *flags){
    char buf[64]; strncpy(buf,tok,sizeof(buf)-1); buf[sizeof(buf)-1]=0;
    CGEventFlags f=0; char *save=NULL; char *last=NULL;
    char *part=strtok_r(buf,"+",&save);
    while(part){
        while(*part==' ')part++;
        char *e=part+strlen(part); while(e>part&&e[-1]==' ')*--e=0;
        CGEventFlags mf=mod_of(part);
        if(mf) f|=mf; else last=part;
        part=strtok_r(NULL,"+",&save);
    }
    if(!last) return 0;
    CGEventFlags kf; int c=keyname_to_code(last,&kf);
    if(c<0) return 0;
    *code=(CGKeyCode)c; *flags=f|kf; return 1;
}

static void parse_config(const char *path){
    FILE *f=fopen(path,"r");
    if(!f){ fprintf(stderr,"cannot open config: %s\n",path); return; }
    g_nactions=0;
    char line[1024];
    while(fgets(line,sizeof(line),f)){
        char *h=strchr(line,'#'); if(h)*h=0;
        char *eq=strchr(line,'='); if(!eq) continue;
        *eq=0;
        char *name=line,*val=eq+1;
        while(*name&&isspace((unsigned char)*name))name++;
        char *ne=name+strlen(name); while(ne>name&&isspace((unsigned char)ne[-1]))*--ne=0;
        while(*val&&isspace((unsigned char)*val))val++;
        char *ve=val+strlen(val); while(ve>val&&isspace((unsigned char)ve[-1]))*--ve=0;
        if(!*name||!*val) continue;
        int btn=name_to_btn(name);
        if(btn<0){ fprintf(stderr,"  unknown button '%s' — skipped\n",name); continue; }
        if(g_nactions>=MAX_ACTIONS) break;
        Action *a=&g_actions[g_nactions++];
        memset(a,0,sizeof(*a)); a->btn=btn;
        strncpy(a->raw,val,sizeof(a->raw)-1);
        if(!strncasecmp(val,"shell:",6)){
            a->is_shell=1;
            char *s=val+6; while(*s&&isspace((unsigned char)*s))s++;
            strncpy(a->shellcmd,s,sizeof(a->shellcmd)-1);
        } else {
            char *s=val; if(!strncasecmp(val,"keys:",5)) s=val+5;
            char buf[512]; strncpy(buf,s,sizeof(buf)-1); buf[sizeof(buf)-1]=0;
            char *save=NULL; char *tk=strtok_r(buf," \t",&save);
            while(tk&&a->nkeys<MAX_TOKENS){
                CGKeyCode c; CGEventFlags fl;
                if(parse_key_token(tk,&c,&fl)){ a->keys[a->nkeys].code=c; a->keys[a->nkeys].flags=fl; a->nkeys++; }
                else fprintf(stderr,"  unknown key token '%s' in '%s' — skipped\n",tk,name);
                tk=strtok_r(NULL," \t",&save);
            }
        }
    }
    fclose(f);
    printf("loaded %d action(s) from %s\n",g_nactions,path);
    fflush(stdout);
}

// ---------------------------------------------------------------- key sending
static CGEventSourceRef g_src=NULL;
static void send_key(CGKeyCode code, CGEventFlags flags){
    if(!g_src) return;
    CGEventRef kd=CGEventCreateKeyboardEvent(g_src,code,true);
    if(!kd) return;
    CGEventSetFlags(kd,flags); CGEventPost(kCGHIDEventTap,kd); usleep(12000);
    CGEventRef ku=CGEventCreateKeyboardEvent(g_src,code,false);
    if(ku){ CGEventSetFlags(ku,flags); CGEventPost(kCGHIDEventTap,ku); CFRelease(ku); }
    CFRelease(kd); usleep(12000);
}

static char g_dir[1024]=".";
static void trim_log(const char *path, off_t sz){
    if(sz<=LOG_MAX) return;
    FILE *f=fopen(path,"rb"); if(!f) return;
    if(fseeko(f, -(off_t)LOG_KEEP, SEEK_END)!=0){ fclose(f); return; }
    int c; while((c=fgetc(f))!=EOF && c!='\n') {}
    char *buf=malloc(LOG_KEEP);
    if(!buf){ fclose(f); return; }
    size_t n=fread(buf,1,LOG_KEEP,f);
    fclose(f);
    FILE *w=fopen(path,"wb");
    if(w){ fwrite(buf,1,n,w); fclose(w); }
    free(buf);
}
static long long now_ms(void){
    struct timeval tv; gettimeofday(&tv,NULL);
    return (long long)tv.tv_sec*1000 + tv.tv_usec/1000;
}
static void log_event(const char *btn, const char *desc){
    char path[1200]; snprintf(path,sizeof(path),"%s/events.log",g_dir);
    FILE *f=fopen(path,"a"); if(!f) return;
    fprintf(f,"%lld\t%s\t%s\n",now_ms(),btn,desc);
    fclose(f);
    off_t sz=0; FILE *c=fopen(path,"r"); if(c){ fseeko(c,0,SEEK_END); sz=ftello(c); fclose(c); }
    if(sz>LOG_MAX) trim_log(path,sz);
}
static void fire_shell(const char *cmd){
    pid_t pid=fork();
    if(pid<0){ fprintf(stderr,"fork failed: %s\n",strerror(errno)); return; }
    if(pid==0){
        setsid();
        int z=open("/dev/null",O_RDWR);
        if(z>=0){ dup2(z,STDIN_FILENO); if(z>2) close(z); }
        signal(SIGINT,SIG_DFL); signal(SIGTERM,SIG_DFL); signal(SIGHUP,SIG_DFL); signal(SIGCHLD,SIG_DFL);
        execl("/bin/sh","sh","-c",cmd,(char*)NULL);
        _exit(127);
    }
}
static void fire(const Action *a, const char *btn){
    if(a->is_shell){
        printf("[%s] shell: %s\n",btn,a->shellcmd); fflush(stdout);
        log_event(btn,a->raw);
        fire_shell(a->shellcmd);
    } else {
        printf("[%s] keys: %s\n",btn,a->raw); fflush(stdout);
        log_event(btn,a->raw);
        for(int i=0;i<a->nkeys;i++) send_key(a->keys[i].code,a->keys[i].flags);
    }
}
static const Action *find_action(int btn){
    for(int i=0;i<g_nactions;i++) if(g_actions[i].btn==btn) return &g_actions[i];
    return NULL;
}

// ---------------------------------------------------------------- signals / paths
static volatile sig_atomic_t g_run=1, g_reload=0;
static void on_term(int s){ (void)s; g_run=0; }
static void on_hup(int s){ (void)s; g_reload=1; }
static void on_chld(int s){ (void)s; while(waitpid(-1,NULL,WNOHANG)>0) {} }

static char g_config[1200];
static void resolve_config(int argc,char**argv){
    const char *cand=NULL;
    if(argc>1) cand=argv[1];
    else if(getenv("SHORYUMUX_CONFIG")) cand=getenv("SHORYUMUX_CONFIG");
    if(cand){ strncpy(g_config,cand,sizeof(g_config)-1); g_config[sizeof(g_config)-1]=0; return; }
    const char *home=getenv("HOME");
    if(home){
        snprintf(g_config,sizeof(g_config),"%s/.config/shoryumux/config.conf",home);
        return;
    }
    strncpy(g_config,"./shoryumux.conf",sizeof(g_config)-1);
}

static int g_lockfd=-1;
static void write_status(const char *state){
    char p[1200]; snprintf(p,sizeof(p),"%s/status",g_dir);
    FILE *f=fopen(p,"w"); if(!f) return;
    fprintf(f,"state=%s\nax=%d\npid=%d\n",state, AXIsProcessTrusted()?1:0, (int)getpid());
    fclose(f);
}
static int acquire_singleton(void){
    char p[1200]; snprintf(p,sizeof(p),"%s/shoryumux.pid",g_dir);
    g_lockfd=open(p,O_RDWR|O_CREAT,0644);
    if(g_lockfd<0){ fprintf(stderr,"cannot open pidfile %s: %s\n",p,strerror(errno)); return -1; }
    if(flock(g_lockfd,LOCK_EX|LOCK_NB)<0){
        char buf[32]={0};
        (void)read(g_lockfd,buf,sizeof(buf)-1);
        fprintf(stderr,"already running (pid %s)\n",buf);
        close(g_lockfd); g_lockfd=-1;
        return -1;
    }
    (void)ftruncate(g_lockfd,0);
    lseek(g_lockfd,0,SEEK_SET);
    dprintf(g_lockfd,"%d\n",(int)getpid());
    return 0;
}
static void release_singleton(void){
    char p[1200]; snprintf(p,sizeof(p),"%s/shoryumux.pid",g_dir);
    char st[1200]; snprintf(st,sizeof(st),"%s/status",g_dir);
    if(g_lockfd>=0){ flock(g_lockfd,LOCK_UN); close(g_lockfd); g_lockfd=-1; }
    unlink(p);
    unlink(st);
}

// ---------------------------------------------------------------- USB
typedef struct {
    io_iterator_t it;
    io_service_t dev;
    IOCFPlugInInterface **plug;
    IOUSBDeviceInterface **udev;
    int dev_open;
    io_iterator_t iit;
    io_service_t isvc;
    IOCFPlugInInterface **iplug;
    IOUSBInterfaceInterface **uif;
    int if_open;
} Stick;

static void stick_release(Stick *s){
    if(!s) return;
    if(s->uif){
        if(s->if_open) (*s->uif)->USBInterfaceClose(s->uif);
        (*s->uif)->Release(s->uif);
    }
    if(s->iplug) IODestroyPlugInInterface(s->iplug);
    if(s->isvc) IOObjectRelease(s->isvc);
    if(s->iit) IOObjectRelease(s->iit);
    if(s->udev){
        if(s->dev_open) (*s->udev)->USBDeviceClose(s->udev);
        (*s->udev)->Release(s->udev);
    }
    if(s->plug) IODestroyPlugInInterface(s->plug);
    if(s->dev) IOObjectRelease(s->dev);
    if(s->it) IOObjectRelease(s->it);
    memset(s,0,sizeof(*s));
}

// 0 = opened, 1 = not found, -1 = error
static int stick_open(Stick *s, int verbose){
    memset(s,0,sizeof(*s));
    SInt32 vid=VID, pidv=PID;
    CFNumberRef nvid=CFNumberCreate(kCFAllocatorDefault,kCFNumberSInt32Type,&vid);
    CFNumberRef npid=CFNumberCreate(kCFAllocatorDefault,kCFNumberSInt32Type,&pidv);
    CFMutableDictionaryRef match=IOServiceMatching("IOUSBHostDevice");
    if(!match||!nvid||!npid){
        if(nvid) CFRelease(nvid);
        if(npid) CFRelease(npid);
        if(verbose) fprintf(stderr,"IOServiceMatching failed\n");
        return -1;
    }
    CFDictionarySetValue(match,CFSTR(kUSBVendorID),nvid);
    CFDictionarySetValue(match,CFSTR(kUSBProductID),npid);
    CFRelease(nvid); CFRelease(npid);

    kern_return_t kr=IOServiceGetMatchingServices(kIOMainPortDefault,match,&s->it);
    if(kr!=KERN_SUCCESS){
        s->it=0;
        if(verbose) fprintf(stderr,"IOServiceGetMatchingServices: %s\n",mach_error_string(kr));
        return -1;
    }
    s->dev=IOIteratorNext(s->it);
    if(!s->dev){ stick_release(s); return 1; }

    SInt32 score=0;
    kr=IOCreatePlugInInterfaceForService(s->dev,kIOUSBDeviceUserClientTypeID,kIOCFPlugInInterfaceID,&s->plug,&score);
    if(kr!=KERN_SUCCESS||!s->plug){
        if(verbose) fprintf(stderr,"device plugin: %s\n",mach_error_string(kr));
        s->plug=NULL; stick_release(s); return -1;
    }
    HRESULT hr=(*s->plug)->QueryInterface(s->plug,CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320),(LPVOID*)&s->udev);
    IODestroyPlugInInterface(s->plug); s->plug=NULL;
    if(hr||!s->udev){
        if(verbose) fprintf(stderr,"QueryInterface(device) failed\n");
        s->udev=NULL; stick_release(s); return -1;
    }
    kr=(*s->udev)->USBDeviceOpen(s->udev);
    if(kr){
        if(verbose) fprintf(stderr,"USBDeviceOpen: %s\n",mach_error_string(kr));
        stick_release(s); return -1;
    }
    s->dev_open=1;

    UInt8 cfg=0;
    (*s->udev)->GetConfiguration(s->udev,&cfg);
    if(cfg!=1){
        kr=(*s->udev)->SetConfiguration(s->udev,1);
        if(kr && verbose) fprintf(stderr,"SetConfiguration: %s (continuing)\n",mach_error_string(kr));
    }

    IOUSBFindInterfaceRequest req={kIOUSBFindInterfaceDontCare,kIOUSBFindInterfaceDontCare,
                                   kIOUSBFindInterfaceDontCare,kIOUSBFindInterfaceDontCare};
    kr=(*s->udev)->CreateInterfaceIterator(s->udev,&req,&s->iit);
    if(kr!=KERN_SUCCESS||!s->iit){
        if(verbose) fprintf(stderr,"CreateInterfaceIterator: %s\n",mach_error_string(kr));
        s->iit=0; stick_release(s); return -1;
    }
    s->isvc=IOIteratorNext(s->iit);
    if(!s->isvc){
        if(verbose) fprintf(stderr,"no USB interface on stick\n");
        stick_release(s); return -1;
    }
    score=0;
    kr=IOCreatePlugInInterfaceForService(s->isvc,kIOUSBInterfaceUserClientTypeID,kIOCFPlugInInterfaceID,&s->iplug,&score);
    if(kr!=KERN_SUCCESS||!s->iplug){
        if(verbose) fprintf(stderr,"interface plugin: %s\n",mach_error_string(kr));
        s->iplug=NULL; stick_release(s); return -1;
    }
    hr=(*s->iplug)->QueryInterface(s->iplug,CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300),(LPVOID*)&s->uif);
    IODestroyPlugInInterface(s->iplug); s->iplug=NULL;
    if(hr||!s->uif){
        if(verbose) fprintf(stderr,"QueryInterface(interface) failed\n");
        s->uif=NULL; stick_release(s); return -1;
    }
    kr=(*s->uif)->USBInterfaceOpen(s->uif);
    if(kr){
        if(verbose) fprintf(stderr,"USBInterfaceOpen: %s\n",mach_error_string(kr));
        stick_release(s); return -1;
    }
    s->if_open=1;
    (*s->uif)->SetPipePolicy(s->uif,IN_PIPE,0,200);
    return 0;
}

static void decode_report(const UInt8 *d, int cur[B_COUNT]){
    memset(cur,0,sizeof(int)*B_COUNT);
    cur[B_UP]=(d[0]&0x01)?1:0; cur[B_DOWN]=(d[0]&0x02)?1:0;
    cur[B_LEFT]=(d[0]&0x04)?1:0; cur[B_RIGHT]=(d[0]&0x08)?1:0;
    cur[B_START]=(d[0]&0x10)?1:0; cur[B_BACK]=(d[0]&0x20)?1:0;
    cur[B_L3]=(d[0]&0x40)?1:0; cur[B_R3]=(d[0]&0x80)?1:0;
    cur[B_LB]=(d[1]&0x01)?1:0; cur[B_RB]=(d[1]&0x02)?1:0; cur[B_GUIDE]=(d[1]&0x04)?1:0;
    cur[B_A]=(d[1]&0x10)?1:0; cur[B_B]=(d[1]&0x20)?1:0;
    cur[B_X]=(d[1]&0x40)?1:0; cur[B_Y]=(d[1]&0x80)?1:0;
    cur[B_LT]=(d[2]>0)?1:0; cur[B_RT]=(d[3]>0)?1:0;
}

int main(int argc,char**argv){
    signal(SIGINT,on_term); signal(SIGTERM,on_term); signal(SIGHUP,on_hup);
    signal(SIGPIPE,SIG_IGN); signal(SIGCHLD,on_chld);

    resolve_config(argc,argv);
    { char tmp[1200]; strncpy(tmp,g_config,sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
      char *d=dirname(tmp); strncpy(g_dir,d,sizeof(g_dir)-1); g_dir[sizeof(g_dir)-1]=0; }
    mkdir(g_dir,0755);

    if(acquire_singleton()!=0) return 5;
    parse_config(g_config);

    if(!AXIsProcessTrusted())
        printf("!! Accessibility not granted — keystrokes won't be delivered. Grant it in\n"
               "   System Settings → Privacy & Security → Accessibility (add shoryumuxd),\n"
               "   then Stop and Start the daemon.\n");
    g_src=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);

    printf("shoryumuxd ready (pid %d) — config %s\n",(int)getpid(),g_config); fflush(stdout);

    Stick stick={0};
    int open=0, seeded=0, waiting_logged=0;
    int prev[B_COUNT]={0};
    write_status("waiting");

    while(g_run){
        if(g_reload){ g_reload=0; parse_config(g_config); write_status(open?"ready":"waiting"); }

        if(!open){
            int rc=stick_open(&stick, !waiting_logged);
            if(rc==0){
                printf("stick connected\n"); fflush(stdout);
                open=1; seeded=0; waiting_logged=0;
                memset(prev,0,sizeof(prev));
                write_status("ready");
            } else {
                if(!waiting_logged){
                    printf("waiting for stick (VID=0x%04X PID=0x%04X)…\n",VID,PID);
                    fflush(stdout);
                    waiting_logged=1;
                }
                write_status("waiting");
                usleep(500000);
            }
            continue;
        }

        UInt8 b[64]; UInt32 n=sizeof(b);
        kern_return_t kr=(*stick.uif)->ReadPipe(stick.uif,IN_PIPE,b,&n);
        if(g_reload){ g_reload=0; parse_config(g_config); write_status("ready"); }
        if(kr==kIOUSBTransactionTimeout) continue;
        if(kr==kIOUSBPipeStalled){
            (*stick.uif)->ClearPipeStall(stick.uif,IN_PIPE);
            continue;
        }
        if(kr!=KERN_SUCCESS){
            printf("stick disconnected (%s) — waiting to reconnect\n",mach_error_string(kr));
            fflush(stdout);
            stick_release(&stick);
            open=0; seeded=0; waiting_logged=0;
            write_status("waiting");
            continue;
        }
        if(n<15||b[0]!=0x00) continue;

        int cur[B_COUNT];
        decode_report(b+2, cur);
        if(!seeded){ memcpy(prev,cur,sizeof(cur)); seeded=1; continue; }
        for(int i=0;i<B_COUNT;i++){
            if(cur[i]&&!prev[i]){
                const Action *a=find_action(i);
                if(a) fire(a,BTN_NAMES[i]);
                else { printf("[%s] (unmapped)\n",BTN_NAMES[i]); log_event(BTN_NAMES[i],"(unmapped)"); }
            }
        }
        memcpy(prev,cur,sizeof(cur));
    }

    if(open) stick_release(&stick);
    if(g_src) CFRelease(g_src);
    release_singleton();
    printf("bye\n");
    return 0;
}
