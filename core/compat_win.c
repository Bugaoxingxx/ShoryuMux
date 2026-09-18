#include "shoryu.h"

#include <errno.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static HANDLE g_lockh = INVALID_HANDLE_VALUE;

static BOOL WINAPI on_ctrl(DWORD t){
    if(t==CTRL_C_EVENT || t==CTRL_BREAK_EVENT || t==CTRL_CLOSE_EVENT){
        g_run=0;
        return TRUE;
    }
    return FALSE;
}

void platform_init(void){
    SetConsoleCtrlHandler(on_ctrl, TRUE);
}

void sleep_ms(int ms){ Sleep((DWORD)ms); }

int shoryu_pid(void){ return (int)GetCurrentProcessId(); }

void fire_shell(const char *cmd){
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmdline[1024];
    memset(&si,0,sizeof(si)); si.cb=sizeof(si);
    memset(&pi,0,sizeof(pi));
    snprintf(cmdline,sizeof(cmdline),"cmd.exe /c %s", cmd);
    if(!CreateProcessA(NULL,cmdline,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)){
        fprintf(stderr,"CreateProcess failed: %lu\n",(unsigned long)GetLastError());
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

int acquire_singleton(void){
    char p[1200];
    OVERLAPPED ov;
    DWORD n;
    snprintf(p,sizeof(p),"%s\\shoryumux.pid",g_dir);
    g_lockh=CreateFileA(p,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                        NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(g_lockh==INVALID_HANDLE_VALUE){
        fprintf(stderr,"cannot open pidfile %s: %lu\n",p,(unsigned long)GetLastError());
        return -1;
    }
    memset(&ov,0,sizeof(ov));
    if(!LockFileEx(g_lockh,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&ov)){
        fprintf(stderr,"already running\n");
        CloseHandle(g_lockh); g_lockh=INVALID_HANDLE_VALUE;
        return -1;
    }
    SetFilePointer(g_lockh,0,NULL,FILE_BEGIN);
    SetEndOfFile(g_lockh);
    { char buf[32]; n=(DWORD)snprintf(buf,sizeof(buf),"%d\n",shoryu_pid());
      WriteFile(g_lockh,buf,n,&n,NULL); }
    return 0;
}

void release_singleton(void){
    char p[1200]; snprintf(p,sizeof(p),"%s\\shoryumux.pid",g_dir);
    char st[1200]; snprintf(st,sizeof(st),"%s\\status",g_dir);
    if(g_lockh!=INVALID_HANDLE_VALUE){
        OVERLAPPED ov; memset(&ov,0,sizeof(ov));
        UnlockFileEx(g_lockh,0,1,0,&ov);
        CloseHandle(g_lockh); g_lockh=INVALID_HANDLE_VALUE;
    }
    DeleteFileA(p);
    DeleteFileA(st);
}
