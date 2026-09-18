#include "shoryu.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/wait.h>

static int g_lockfd = -1;

static void on_term(int s){ (void)s; g_run=0; }
static void on_hup(int s){ (void)s; g_reload=1; }
static void on_chld(int s){ (void)s; while(waitpid(-1,NULL,WNOHANG)>0) {} }

void platform_init(void){
    signal(SIGINT,on_term);
    signal(SIGTERM,on_term);
    signal(SIGHUP,on_hup);
    signal(SIGPIPE,SIG_IGN);
    signal(SIGCHLD,on_chld);
}

void sleep_ms(int ms){
    usleep((useconds_t)ms * 1000);
}

int shoryu_pid(void){ return (int)getpid(); }

int shoryu_pid_alive(int pid){
    if(pid<=0) return 1;
    return kill((pid_t)pid, 0)==0;
}

void fire_shell(const char *cmd){
    pid_t pid=fork();
    if(pid<0){ fprintf(stderr,"fork failed: %s\n",strerror(errno)); return; }
    if(pid==0){
        int z;
        setsid();
        z=open("/dev/null",O_RDWR);
        if(z>=0){ dup2(z,STDIN_FILENO); if(z>2) close(z); }
        signal(SIGINT,SIG_DFL); signal(SIGTERM,SIG_DFL); signal(SIGHUP,SIG_DFL); signal(SIGCHLD,SIG_DFL);
        execl("/bin/sh","sh","-c",cmd,(char*)NULL);
        _exit(127);
    }
}

int acquire_singleton(void){
    char p[1200];
    snprintf(p,sizeof(p),"%s/shoryumux.pid",g_dir);
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
    dprintf(g_lockfd,"%d\n",shoryu_pid());
    return 0;
}

void release_singleton(void){
    char p[1200]; snprintf(p,sizeof(p),"%s/shoryumux.pid",g_dir);
    char st[1200]; snprintf(st,sizeof(st),"%s/status",g_dir);
    if(g_lockfd>=0){ flock(g_lockfd,LOCK_UN); close(g_lockfd); g_lockfd=-1; }
    unlink(p);
    unlink(st);
}
