
// http://www.gweep.net/~prefect/eng/reference/protocol/midispec.html

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "midio.h"
#include "mproc.h"


static void _msg_handler(void *ctx, MIDIO_MSG *msg)
{
    MPROC *me = ctx;
    // midio_print_msg(msg);
    mproc_msg_handler(me, msg);
}

int main(int argc, char **argv)
{
    MPROC mproc;

    MIDIO *midio = midio_create();
    midio_open(midio);

    mproc_init(&mproc, midio);

    midio_start_pump(midio, &mproc, _msg_handler);

    for (;;)
        sleep(1000);

    return 0;
}
