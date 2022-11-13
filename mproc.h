//
//  mproc.h
//  miditrick
//
//  Created by Gabriele Mondada on 23.05.21.
//

#ifndef _MPROC_H_
#define _MPROC_H_

#include <stdbool.h>
#include "midio.h"


typedef struct mproc MPROC;

struct mproc {
    MIDIO *midio;

    int ui_mode;
    bool console;
    int shift;
    int exit_count;
    int beatstep_port;
    int virtual_port;

    /**
     * Array containing the state of the forwarded notes, i.e. the state of
     * notes as seen by the synthetiser connected to the output.
     * The array index is the untransposed note.
     * The array content is the note velocity.
     * A null velocity means that the note is off.
     */
    char fwd_vel[128];

    /**
     * Array containing the state of the forwarded notes, i.e. the state of
     * notes as seen by the synthetiser connected to the output.
     * The array index is the untransposed note.
     * The array content is the transposed note as it has been sent to
     * the synthetiser.
     */
    char fwd_note[128];
};


void mproc_init(MPROC *me, MIDIO *midio);
void mproc_msg_handler(MPROC *me, MIDIO_MSG *msg_in);


#endif
