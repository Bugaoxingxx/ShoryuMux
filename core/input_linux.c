#include "shoryu.h"

#include <stdio.h>
#include <string.h>
#include <libusb.h>

static libusb_context *g_ctx = NULL;
static libusb_device_handle *g_h = NULL;
static unsigned char g_ep_in = 0x81;

static int find_intr_in(libusb_device_handle *h, unsigned char *ep_out){
    struct libusb_config_descriptor *cfg=NULL;
    const struct libusb_interface *iface;
    const struct libusb_interface_descriptor *alt;
    const struct libusb_endpoint_descriptor *ep;
    int i,j,k,r;
    r=libusb_get_active_config_descriptor(libusb_get_device(h), &cfg);
    if(r!=0 || !cfg) return -1;
    for(i=0;i<cfg->bNumInterfaces;i++){
        iface=&cfg->interface[i];
        for(j=0;j<iface->num_altsetting;j++){
            alt=&iface->altsetting[j];
            for(k=0;k<alt->bNumEndpoints;k++){
                ep=&alt->endpoint[k];
                if((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK)==LIBUSB_TRANSFER_TYPE_INTERRUPT &&
                   (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN)){
                    *ep_out=ep->bEndpointAddress;
                    libusb_free_config_descriptor(cfg);
                    return 0;
                }
            }
        }
    }
    libusb_free_config_descriptor(cfg);
    return -1;
}

int input_open(int verbose){
    int r;
    if(!g_ctx){
        r=libusb_init(&g_ctx);
        if(r){ if(verbose) fprintf(stderr,"libusb_init: %s\n",libusb_strerror(r)); return -1; }
    }
    g_h=libusb_open_device_with_vid_pid(g_ctx, VID, PID);
    if(!g_h) return 1;
    (void)libusb_set_auto_detach_kernel_driver(g_h, 1);
    r=libusb_claim_interface(g_h, 0);
    if(r){
        if(verbose){
            fprintf(stderr,"USBInterface claim failed: %s\n", libusb_strerror(r));
            fprintf(stderr,"  If xpad owns the stick: echo '%04x:%04x' is VID:PID; unbind xpad or install linux/99-shoryumux.rules\n", VID, PID);
        }
        libusb_close(g_h); g_h=NULL;
        return -1;
    }
    if(find_intr_in(g_h, &g_ep_in)!=0) g_ep_in=0x81;
    return 0;
}

void input_close(void){
    if(g_h){
        libusb_release_interface(g_h, 0);
        libusb_close(g_h);
        g_h=NULL;
    }
}

int input_poll(int cur[B_COUNT]){
    unsigned char b[64];
    int n=0, r;
    if(!g_h) return -1;
    r=libusb_interrupt_transfer(g_h, g_ep_in, b, (int)sizeof(b), &n, 200);
    if(r==LIBUSB_ERROR_TIMEOUT) return 1;
    if(r) return -1;
    if(n<15 || b[0]!=0x00) return 1;
    xinput_decode_report(b+2, cur);
    return 0;
}
