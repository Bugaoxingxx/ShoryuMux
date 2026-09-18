#include "shoryu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_parent = 0;

static void bind_parent(void){
    const char *s=getenv("SHORYUMUX_PARENT_PID");
    if(s && *s) g_parent=atoi(s);
    if(g_parent>0){
        printf("bound to parent pid %d — will exit if it disappears\n", g_parent);
        fflush(stdout);
    }
}

static int parent_ok(void){
    return shoryu_pid_alive(g_parent);
}

int main(int argc, char **argv){
    int open=0, seeded=0, waiting_logged=0;
    int prev[B_COUNT];

    platform_init();
    bind_parent();
    resolve_config(argc, argv);
    if(acquire_singleton()!=0) return 5;
    parse_config(g_config);

    if(output_init()!=0){
        fprintf(stderr,"output_init failed\n");
        release_singleton();
        return 6;
    }
    if(!output_is_trusted()){
        const char *hint=output_trust_hint();
        if(hint){ printf("%s\n", hint); fflush(stdout); }
    }

    printf("shoryumuxd ready (pid %d) — config %s\n", shoryu_pid(), g_config);
    fflush(stdout);
    memset(prev,0,sizeof(prev));
    write_status("waiting");

    while(g_run && parent_ok()){
        if(g_reload || config_file_changed()){
            g_reload=0;
            parse_config(g_config);
            write_status(open?"ready":"waiting");
        }

        if(!open){
            int rc=input_open(!waiting_logged);
            if(rc==0){
                printf("stick connected\n"); fflush(stdout);
                open=1; seeded=0; waiting_logged=0;
                memset(prev,0,sizeof(prev));
                write_status("ready");
            } else {
                if(!waiting_logged){
                    printf("waiting for stick (VID=0x%04X PID=0x%04X)…\n", VID, PID);
                    fflush(stdout);
                    waiting_logged=1;
                }
                write_status("waiting");
                sleep_ms(500);
            }
            continue;
        }

        {
            int cur[B_COUNT];
            int pr=input_poll(cur);
            if(g_reload || config_file_changed()){
                g_reload=0;
                parse_config(g_config);
                write_status("ready");
            }
            if(pr==1) continue;
            if(pr<0){
                printf("stick disconnected — waiting to reconnect\n");
                fflush(stdout);
                input_close();
                open=0; seeded=0; waiting_logged=0;
                write_status("waiting");
                continue;
            }
            if(!seeded){ memcpy(prev,cur,sizeof(cur)); seeded=1; continue; }
            {
                int i;
                for(i=0;i<B_COUNT;i++){
                    if(cur[i]&&!prev[i]){
                        const Action *a=find_action(i);
                        if(a) fire(a,BTN_NAMES[i]);
                        else { printf("[%s] (unmapped)\n",BTN_NAMES[i]); log_event(BTN_NAMES[i],"(unmapped)"); }
                    }
                }
            }
            memcpy(prev,cur,sizeof(cur));
        }
    }

    if(!parent_ok() && g_run)
        printf("parent process gone — exiting\n");

    if(open) input_close();
    output_shutdown();
    release_singleton();
    printf("bye\n");
    return 0;
}
