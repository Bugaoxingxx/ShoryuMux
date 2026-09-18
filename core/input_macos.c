#include "shoryu.h"

#include <stdio.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <CoreFoundation/CoreFoundation.h>

#define IN_PIPE 1

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

static Stick g_stick;

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

int input_open(int verbose){
    Stick *s=&g_stick;
    SInt32 vid=VID, pidv=PID, score=0;
    CFNumberRef nvid, npid;
    CFMutableDictionaryRef match;
    kern_return_t kr;
    HRESULT hr;
    UInt8 cfg=0;
    IOUSBFindInterfaceRequest req={kIOUSBFindInterfaceDontCare,kIOUSBFindInterfaceDontCare,
                                   kIOUSBFindInterfaceDontCare,kIOUSBFindInterfaceDontCare};

    stick_release(s);
    nvid=CFNumberCreate(kCFAllocatorDefault,kCFNumberSInt32Type,&vid);
    npid=CFNumberCreate(kCFAllocatorDefault,kCFNumberSInt32Type,&pidv);
    match=IOServiceMatching("IOUSBHostDevice");
    if(!match||!nvid||!npid){
        if(nvid) CFRelease(nvid);
        if(npid) CFRelease(npid);
        if(verbose) fprintf(stderr,"IOServiceMatching failed\n");
        return -1;
    }
    CFDictionarySetValue(match,CFSTR(kUSBVendorID),nvid);
    CFDictionarySetValue(match,CFSTR(kUSBProductID),npid);
    CFRelease(nvid); CFRelease(npid);

    kr=IOServiceGetMatchingServices(kIOMainPortDefault,match,&s->it);
    if(kr!=KERN_SUCCESS){
        s->it=0;
        if(verbose) fprintf(stderr,"IOServiceGetMatchingServices: %s\n",mach_error_string(kr));
        return -1;
    }
    s->dev=IOIteratorNext(s->it);
    if(!s->dev){ stick_release(s); return 1; }

    kr=IOCreatePlugInInterfaceForService(s->dev,kIOUSBDeviceUserClientTypeID,kIOCFPlugInInterfaceID,&s->plug,&score);
    if(kr!=KERN_SUCCESS||!s->plug){
        if(verbose) fprintf(stderr,"device plugin: %s\n",mach_error_string(kr));
        s->plug=NULL; stick_release(s); return -1;
    }
    hr=(*s->plug)->QueryInterface(s->plug,CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID320),(LPVOID*)&s->udev);
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

    (*s->udev)->GetConfiguration(s->udev,&cfg);
    if(cfg!=1){
        kr=(*s->udev)->SetConfiguration(s->udev,1);
        if(kr && verbose) fprintf(stderr,"SetConfiguration: %s (continuing)\n",mach_error_string(kr));
    }

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

void input_close(void){ stick_release(&g_stick); }

int input_poll(int cur[B_COUNT]){
    UInt8 b[64]; UInt32 n=sizeof(b);
    kern_return_t kr;
    if(!g_stick.uif) return -1;
    kr=(*g_stick.uif)->ReadPipe(g_stick.uif,IN_PIPE,b,&n);
    if(kr==kIOUSBTransactionTimeout) return 1;
    if(kr==kIOUSBPipeStalled){
        (*g_stick.uif)->ClearPipeStall(g_stick.uif,IN_PIPE);
        return 1;
    }
    if(kr!=KERN_SUCCESS) return -1;
    if(n<15||b[0]!=0x00) return 1;
    xinput_decode_report(b+2, cur);
    return 0;
}
