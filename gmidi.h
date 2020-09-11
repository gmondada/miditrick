
#ifndef _GMIDI_H_
#define _GMIDI_H_

typedef int GMIDI_DEV;
typedef struct gmidi_msg GMIDI_MSG;

struct gmidi_msg {
    int size;
    union {
	    char bytes[3];
	};
};

#define GMIDI_EOPEN -1100
//#define GMIDI_EEOF -1101
#define GMIDI_EIO -1102

int gmidi_open_in_dev(GMIDI_DEV *dev, int dev_number);
int gmidi_open_out_dev(GMIDI_DEV *out, int dev_number);
int gmidi_get(GMIDI_DEV *in, GMIDI_MSG *msg, int timeout);
int gmidi_put(GMIDI_DEV *out, GMIDI_MSG *msg, int timeout);
int gmidi_input_available(GMIDI_DEV *in, bool *available);
int gmidi_show_msg(GMIDI_MSG *msg);

#endif
