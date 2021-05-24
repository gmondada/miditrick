
#ifndef _MIDIO_H_
#define _MIDIO_H_

#include <stdint.h>


/*** types ***/

typedef struct midio MIDIO;
typedef struct midio_msg MIDIO_MSG;

struct midio_msg {
    int port; // -1 == all ports
    int size;
    union {
        char bytes[3];
        uint8_t u8[3];
    };
};

struct midio {
};


/*** prototypes ***/

MIDIO *midio_create(void);
void midio_open(MIDIO *me);
void midio_close(MIDIO *me);
int midio_get_port_by_name(MIDIO *me, const char *name);
void midio_start_pump(MIDIO *me, void *ctx, void (* handler)(void *ctx, MIDIO_MSG *msg));
void midio_recv(MIDIO *me, MIDIO_MSG *msg);
void midio_send(MIDIO *me, MIDIO_MSG *msg);
void midio_print_msg(MIDIO_MSG *msg);


#endif
