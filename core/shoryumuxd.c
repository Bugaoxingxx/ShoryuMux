// shoryumuxd.c — HORI XInput fight stick -> macOS keystrokes / shell actions.
//
// Reads the stick over raw USB (IOKit; no driver, no entitlement needed — verified),
// detects button press edges, and fires actions from a config file.
//
//   keys: <token> [token...]   synthesize keystrokes to the FRONTMOST app (CGEvent)
//   shell: <command>           run a shell command
//
// Config path resolution (first that exists):
//   argv[1]  >  $STICKMUX_CONFIG  >  ~/.config/shoryumux/config.conf  >  ./shoryumux.conf
//
// Signals:  SIGHUP = reload config   SIGINT/SIGTERM = quit
// Files (in the config's directory):  shoryumux.pid   events.log
//
// Build (see Makefile):
//   clang -O2 -o shoryumuxd shoryumuxd.c -framework IOKit -framework CoreFoundation \
//     -framework ApplicationServices -framework CoreGraphics

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <libgen.h>
#include <time.h>
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
    CGEventRef kd=CGEventCreateKeyboardEvent(g_src,code,true);
    CGEventSetFlags(kd,flags); CGEventPost(kCGHIDEventTap,kd); usleep(12000);
    CGEventRef ku=CGEventCreateKeyboardEvent(g_src,code,false);
    CGEventSetFlags(ku,flags); CGEventPost(kCGHIDEventTap,ku);
    CFRelease(kd); CFRelease(ku); usleep(12000);
}

static char g_dir[1024]=".";
static void log_event(const char *btn, const char *desc){
    char path[1200]; snprintf(path,sizeof(path),"%s/events.log",g_dir);
    FILE *f=fopen(path,"a"); if(!f) return;
    long long ms=(long long)time(NULL)*1000;
    fprintf(f,"%lld\t%s\t%s\n",ms,btn,desc);
    fclose(f);
    // keep it small
    off_t sz=0; FILE *c=fopen(path,"r"); if(c){ fseek(c,0,SEEK_END); sz=ftello(c); fclose(c); }
    if(sz>256*1024){ FILE *t=fopen(path,"w"); if(t) fclose(t); }
}
static void fire(const Action *a, const char *btn){
    if(a->is_shell){
        printf("[%s] shell: %s\n",btn,a->shellcmd); fflush(stdout);
        log_event(btn,a->raw);
        system(a->shellcmd);
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

static char g_config[1200];
static void resolve_config(int argc,char**argv){
    const char *cand=NULL;
    if(argc>1) cand=argv[1];
    else if(getenv("STICKMUX_CONFIG")) cand=getenv("STICKMUX_CONFIG");
    if(cand){ strncpy(g_config,cand,sizeof(g_config)-1); g_config[sizeof(g_config)-1]=0; return; }
    const char *home=getenv("HOME");
    if(home){
        snprintf(g_config,sizeof(g_config),"%s/.config/shoryumux/config.conf",home);
        if(access(g_config,F_OK)==0) return;
    }
    strncpy(g_config,"./shoryumux.conf",sizeof(g_config)-1);
}
static void write_pidfile(void){
    char p[1200]; snprintf(p,sizeof(p),"%s/shoryumux.pid",g_dir);
    FILE *f=fopen(p,"w"); if(f){ fprintf(f,"%d\n",(int)getpid()); fclose(f); }
}
static void rm_pidfile(void){
    char p[1200]; snprintf(p,sizeof(p),"%s/shoryumux.pid",g_dir); unlink(p);
}

int main(int argc,char**argv){
    signal(SIGINT,on_term); signal(SIGTERM,on_term); signal(SIGHUP,on_hup);
    signal(SIGPIPE,SIG_IGN);

    resolve_config(argc,argv);
    // config dir = dir of g_config
    { char tmp[1200]; strncpy(tmp,g_config,sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
      char *d=dirname(tmp); strncpy(g_dir,d,sizeof(g_dir)-1); g_dir[sizeof(g_dir)-1]=0; }

    parse_config(g_config);
    write_pidfile();

    const void *k[]={CFSTR("AXTrustedCheckOptionPrompt")};
    const void *v[]={kCFBooleanTrue};
    CFDictionaryRef opts=CFDictionaryCreate(NULL,k,v,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!AXIsProcessTrustedWithOptions(opts))
        printf("!! Accessibility not granted — keystrokes won't be delivered. Grant it in\n"
               "   System Settings → Privacy & Security → Accessibility (add your terminal/app).\n");
    CFRelease(opts);
    g_src=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);

    CFMutableDictionaryRef match=IOServiceMatching("IOUSBHostDevice");
    CFDictionarySetValue(match,CFSTR(kUSBVendorID),CFNumberCreate(0,kCFNumberSInt32Type,&(SInt32){VID}));
    CFDictionarySetValue(match,CFSTR(kUSBProductID),CFNumberCreate(0,kCFNumberSInt32Type,&(SInt32){PID}));
    io_iterator_t it;
    if(IOServiceGetMatchingServices(kIOMainPortDefault,match,&it)!=KERN_SUCCESS){ printf("match failed\n"); rm_pidfile(); return 1; }
    io_service_t dev=IOIteratorNext(it);
    if(!dev){ printf("stick not found — plug it in and retry\n"); rm_pidfile(); return 2; }
    IOCFPlugInInterface **plug=NULL; SInt32 sc=0;
    IOCreatePlugInInterfaceForService(dev,kIOUSBDeviceUserClientTypeID,kIOCFPlugInInterfaceID,&plug,&sc);
    IOUSBDeviceInterface **udev=NULL;
    (*plug)->QueryInterface(plug,CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320),(LPVOID*)&udev);
    if((*udev)->USBDeviceOpen(udev)){ printf("USBDeviceOpen failed\n"); rm_pidfile(); return 3; }
    (*udev)->SetConfiguration(udev,1);
    IOUSBFindInterfaceRequest req={kIOUSBFindInterfaceDontCare,kIOUSBFindInterfaceDontCare,
                                   kIOUSBFindInterfaceDontCare,kIOUSBFindInterfaceDontCare};
    io_iterator_t iit; (*udev)->CreateInterfaceIterator(udev,&req,&iit);
    io_service_t isvc=IOIteratorNext(iit);
    IOCFPlugInInterface **iplug=NULL; SInt32 sc2=0;
    IOCreatePlugInInterfaceForService(isvc,kIOUSBInterfaceUserClientTypeID,kIOCFPlugInInterfaceID,&iplug,&sc2);
    IOUSBInterfaceInterface **uif=NULL;
    (*iplug)->QueryInterface(iplug,CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300),(LPVOID*)&uif);
    if((*uif)->USBInterfaceOpen(uif)){ printf("USBInterfaceOpen failed\n"); rm_pidfile(); return 4; }
    (*uif)->SetPipePolicy(uif,IN_PIPE,0,200);

    printf("shoryumuxd ready (pid %d) — config %s\n",(int)getpid(),g_config); fflush(stdout);
    int prev[B_COUNT]={0};
    while(g_run){
        UInt8 b[64]; UInt32 n=sizeof(b);
        kern_return_t kr=(*uif)->ReadPipe(uif,IN_PIPE,b,&n);
        if(g_reload){ g_reload=0; parse_config(g_config); }   // reload before decoding this report
        if(kr==kIOUSBTransactionTimeout) continue;
        if(kr!=KERN_SUCCESS){ printf("ReadPipe kr=%d (%s)\n",kr,mach_error_string(kr)); break; }
        if(n<15||b[0]!=0x00) continue;
        const UInt8 *d=b+2;
        int cur[B_COUNT]={0};
        cur[B_UP]=(d[0]&0x01)?1:0;cur[B_DOWN]=(d[0]&0x02)?1:0;cur[B_LEFT]=(d[0]&0x04)?1:0;cur[B_RIGHT]=(d[0]&0x08)?1:0;
        cur[B_START]=(d[0]&0x10)?1:0;cur[B_BACK]=(d[0]&0x20)?1:0;cur[B_L3]=(d[0]&0x40)?1:0;cur[B_R3]=(d[0]&0x80)?1:0;
        cur[B_LB]=(d[1]&0x01)?1:0;cur[B_RB]=(d[1]&0x02)?1:0;cur[B_GUIDE]=(d[1]&0x04)?1:0;
        cur[B_A]=(d[1]&0x10)?1:0;cur[B_B]=(d[1]&0x20)?1:0;cur[B_X]=(d[1]&0x40)?1:0;cur[B_Y]=(d[1]&0x80)?1:0;
        cur[B_LT]=(d[2]>0)?1:0;cur[B_RT]=(d[3]>0)?1:0;
        for(int i=0;i<B_COUNT;i++){
            if(cur[i]&&!prev[i]){ const Action *a=find_action(i);
                if(a) fire(a,BTN_NAMES[i]); else { printf("[%s] (unmapped)\n",BTN_NAMES[i]); log_event(BTN_NAMES[i],"(unmapped)"); } }
        }
        memcpy(prev,cur,sizeof(cur));
    }
    (*uif)->USBInterfaceClose(uif);
    (*udev)->USBDeviceClose(udev);
    rm_pidfile();
    printf("bye\n");
    return 0;
}
