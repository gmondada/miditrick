
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>

#include "gmidi.h"


//#define GMIDI_DEV_PATH "/dev/snd/midiC0D0"  // x-box
#define GMIDI_DEV_PATH "/dev/snd/midiC1D0"  // rpi1


static int dev = -1;
static bool read_only;

int gmidi_open_in_dev(GMIDI_DEV *in, int dev_number) {
	if (dev != -1) {
	    *in = dev;
	    return 0;
	}
    *in = open(GMIDI_DEV_PATH, O_RDONLY);
    if (*in < 0)
    	return GMIDI_EOPEN;
    dev = *in;
    read_only = true;
    return 0;
}

int gmidi_open_out_dev(GMIDI_DEV *out, int dev_number) {
	if (dev != -1) {
		if (read_only) {
			close(dev);
		} else {
		    *out = dev;
		    return 0;
		}
	}
    *out = open(GMIDI_DEV_PATH, O_RDWR);
    if (*out < 0)
    	return GMIDI_EOPEN;
    dev = *out;
    read_only = false;
   	return 0;
}

int gmidi_get(GMIDI_DEV *in, GMIDI_MSG *msg, int timeout) {
    int ret = read(*in, msg->bytes, 3);
//    printf("read = %d\n", ret);
    if (ret <= 0)
    	return GMIDI_EIO;
    msg->size = ret;
//    gmidi_show_msg(msg);
    return 0;
}

int gmidi_put(GMIDI_DEV *out, GMIDI_MSG *msg, int timeout) {
	int ret = write(*out, msg->bytes, msg->size);
	if (ret != msg->size)
		return GMIDI_EIO;
    return 0;
}

int gmidi_input_available(GMIDI_DEV *in, bool *available) {
	return -1;
}

int gmidi_show_msg(GMIDI_MSG *msg) {
    if (msg->size == 2)
        printf("[%02X;%02X]\n", msg->bytes[0] & 0xFF, msg->bytes[1] & 0xFF);
    else
        printf("[%02X;%02X;%02X]\n", msg->bytes[0] & 0xFF, msg->bytes[1] & 0xFF, msg->bytes[2] & 0xFF);
}
