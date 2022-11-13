//
//  mproc.c
//  miditrick
//
//  Created by Gabriele Mondada on 23.05.21.
//

#include "mproc.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


#define GMU_ASYM_MOD(n, d) (((n)<0) ? ((d)-1+((n)+1)%(d)) : ((n)%(d)))


static void _ding(MIDIO *out, int note)
{
    MIDIO_MSG msg = {
        .port = -1,
        .size = 3,
        .bytes = {0x90, note, 0x20},
    };
    midio_send(out, &msg);
    usleep(10 * 1000);  // 0.1 s
    msg.bytes[2] = 0x00;
    midio_send(out, &msg);
}

static void _scale(MIDIO *out)
{
    int i;
    for (i=0; i<12 ;i++) {
        _ding(out, i + 0x3C);
    }
}

//static void _send_note_off(MIDIO *out, int note)
//{
//    MIDIO_MSG msg = {
//        .port = -1,
//        .size = 3,
//    };
//
//    msg.bytes[0] = 0x90;
//    msg.bytes[1] = (char)(note & 0x7F);
//    midio_send(out, &msg);
//}

void mproc_init(MPROC *me, MIDIO *midio)
{
    memset(me, 0, sizeof(*me));
    me->midio = midio;
    me->beatstep_port = midio_get_port_by_name(midio, "BeatStep");
    if (me->beatstep_port == -1)
        me->beatstep_port = midio_get_port_by_name(midio, "Arturia BeatStep");
    printf("BeatStep: port=%d\n", me->beatstep_port);
    me->virtual_port = midio_get_port_by_name(midio, "Virtual Output");
    printf("Virtual Output: port=%d\n", me->virtual_port);
}

int beatstep_get_pad_index(int note)
{
    int key = note - 0x24;
    int col = key % 8;
    int row = key >= 8 ? 0 : 1;
    return col + row * 8;
}

void beatstep_set_pad_color(MPROC *me, int pad_index, int color_index)
{
    /*
     from: https://forum.arturia.com/index.php?topic=92480.0
     from: https://www.untergeek.de/2014/11/taming-arturias-beatstep-sysex-codes-for-programming-via-ipad/

     In order to change the Pad color send this SysEx message to "Arturia MiniLab mkII" MIDI output port
      F0 00 20 6B 7F 42 02 00 10 7n cc F7

     where:
       n is the pad number, 0 to F, corresponding to Pad1 to Pad16
       cc is the color:
         00 - black
         01 - red
         04 - green
         05 - yellow
         10 - blue
         11 - magenta
         14 - cyan
         7F - white
    */

    uint8_t buf[] = {0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x00, 0x10, 0x70 + pad_index, color_index, 0xF7};
    midio_send_sysex(me->midio, me->beatstep_port, buf, sizeof(buf));
}

void beatstep_update_ui(MPROC *me, int pad_index, bool down)
{
    if (me->ui_mode == 0) {
        int shifts[] = {
            1000, 1000, 1000, 1000, -3, -8, -1, -6,
            1, -4, 3, -2, 5, 0, 7, 2
        };
        if (shifts[pad_index] != 1000 && down) {
            me->shift = shifts[pad_index] - 12;
        }
        int key = 1000;
        for (int i=0; i<sizeof(shifts); i++) {
            if (shifts[i] == GMU_ASYM_MOD(me->shift, 12)) {
                key = i;
                break;
            }
        }
        for (int i=0; i<16; i++) {
            if (i == pad_index && down) {
                // do nothing
            } else if (i == key) {
                beatstep_set_pad_color(me, i, 0x10);
            } else {
                beatstep_set_pad_color(me, i, 0);
            }
        }
    }
}

void mproc_msg_handler(MPROC *me, MIDIO_MSG *msg_in)
{
    MIDIO_MSG msg = *msg_in;

    // midio_print_msg(&msg);

    int cmd;   // midi command
    int channel;
    int note;  // when cmd = 8 or 9
    int vel;   // when cmd = 9
    bool note_on;
    bool note_off;
    bool fwd = true;
    int fnote; // forwarded (transposed) note

    cmd = (msg.bytes[0] >> 4) & 0x0F;
    channel = msg.bytes[0] & 0x0F;
    note = msg.bytes[1] & 0x7F;
    vel = msg.bytes[2];
    note_on = ((cmd == 9) && (vel != 0));
    note_off = ((cmd == 8) || ((cmd == 9) && (vel == 0)));

    if (me->virtual_port >= 0 && msg.port == me->beatstep_port)
        fwd = false;

    // beatstep control
    if (msg.port == me->beatstep_port) {
        if (note_on || note_off) {
            int pad_index = beatstep_get_pad_index(note);
            beatstep_update_ui(me, pad_index, note_on);
            return;
        }
    }

    // changement du pedale de gauche ?
    if (cmd == 0xB && msg.bytes[1] == 0x43) {
        fwd = false;
        me->console = msg.bytes[2] != 0;
        printf("console = %d\n", me->console);
        me->exit_count = 0;
        if (me->console) {
            // pedale da gauche de haut en bas
            // update current chord
            int i;
            int chord = 0; // 12 bit chord before transposition
            for (i=0; i<128; i++)
                if (me->fwd_vel[i] != 0)
                    chord |= 1 << (i % 12);
            printf("chord = 0x%03x\n", chord);
            if (chord == 0x122 || chord == 0x922) {
                // accord de transition
                me->shift += 2;
                printf("shift = %d\n", me->shift);
            } else if (chord == 0x092 || chord == 0x292 || chord == 0x212) {
                // accord de transition
                me->shift -= 2;
                printf("shift = %d\n", me->shift);
            } else if (chord == 0x910 || chord == 0x914) {
                // accord de transition
                me->shift -= 7;
                printf("shift = %d\n", me->shift);
            } else if (chord == 0x452 || chord == 0x442) {
                // accord de transition
                me->shift += 7;
                printf("shift = %d\n", me->shift);
            }
        } else {
            // pedale da gauche de bas en haut
        }
    }

    // handle console commands
    if (me->console && note_on) {
        // note on avec pedale de gauche en bas
        int rel_note = note - 60; // 0 = DO in the middle
        if (rel_note > (-12) && rel_note < 12) {
            me->shift = rel_note - 4;
            printf("shift = %d\n", me->shift);
        }
        if (rel_note == -12) {
            me->exit_count++;
            if (me->exit_count >= 5) {
                _scale(me->midio);
                exit(2); // exit and halt
            }
        }
        if (rel_note == -13) {
            me->exit_count++;
            if (me->exit_count >= 5) {
                exit(3); // exit only
            }
        }
    }

    // transpose
    if (note_on) {
        if (me->console) {
            fwd = false;
        } else {
            if (me->fwd_vel[note] != 0)
                printf("WARNING: unexpected note on message\n");
            fnote = (note + me->shift);
            if (fnote < 0 || fnote >= 128) {
                fwd = false;
            } else {
                me->fwd_vel[note] = vel;
                me->fwd_note[note] = fnote;
                msg.bytes[1] = fnote;
            }
        }
    }
    if (note_off) {
        if (me->fwd_vel[note] == 0) {
            fwd = false;
        } else {
            fnote = me->fwd_note[note];
            me->fwd_vel[note] = 0;
            msg.bytes[1] = fnote;
        }
    }

    // forward
    if (fwd) {
        if (me->virtual_port >= 0) {
            msg.port = me->virtual_port;
        } else {
            if (msg.port == me->beatstep_port)
                msg.port = -1;
        }

        // send midi command out
        midio_send(me->midio, &msg);

        // midio_print_msg(&msg);
    }
}
