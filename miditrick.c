
// http://www.gweep.net/~prefect/eng/reference/protocol/midispec.html

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "midio.h"


void beep(MIDIO *out, int note)
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

void scale(MIDIO *out)
{
    int i;
    for (i=0; i<12 ;i++) {
        beep(out, i + 0x3C);
    }
}

void send_note_off(MIDIO *out, int note)
{
    MIDIO_MSG msg = {
        .port = -1,
        .size = 3,
    };

    msg.bytes[0] = 0x90;
    msg.bytes[1] = (char)(note & 0x7F);
    midio_send(out, &msg);
}

int main(char *argv, int argc)
{
    MIDIO *midio;
    MIDIO_MSG msg;
    int err;
    bool console = false;
    int shift = 0;
    int exit_count = 0;
    int beatstep_port = -1;

    /**
     * Array containing the state of the forwarded notes, i.e. the state of
     * notes as seen by the synthetiser connected to the output.
     * The array index is the untransposed note.
     * The array content is the note velocity.
     * A null velocity means that the note is off.
     */
    char fwd_vel[128] = {0};

    /**
     * Array containing the state of the forwarded notes, i.e. the state of
     * notes as seen by the synthetiser connected to the output.
     * The array index is the untransposed note.
     * The array content is the transposed note as it has been sent to
     * the synthetiser.
     */
    char fwd_note[128] = {0};

    midio = midio_create();
    midio_open(midio);

    beatstep_port = midio_get_port_by_name(midio, "BeatStep");
    printf("BeatStep port=%d\n", beatstep_port);

    for (;;) {
        int cmd;   // midi command
        int note;  // when cmd = 8 or 9
        int vel;   // when cmd = 9
        bool note_on;
        bool note_off;
        bool fwd = true;
        int fnote; // forwarded (transposed) note

        // get next midi message
        midio_recv(midio, &msg);
        if (msg.size == 0)
            continue;

        cmd = (msg.bytes[0] >> 4) & 0x0F;
        note = msg.bytes[1] & 0x7F;
        vel = msg.bytes[2];
        note_on = ((cmd == 9) && (vel != 0));
        note_off = ((cmd == 8) || ((cmd == 9) && (vel == 0)));

        // changement du pedale de gauche ?
        if (cmd == 0xB && msg.bytes[1] == 0x43) {
            fwd = false;
            console = msg.bytes[2] != 0;
            printf("console = %d\n", console);
            exit_count = 0;
            if (console) {
                // pedale da gauche de haut en bas
                // update current chord
                int i;
                int chord = 0; // 12 bit chord before transposition
                for (i=0; i<128; i++)
                    if (fwd_vel[i] != 0)
                        chord |= 1 << (i % 12);
                printf("chord = 0x%03x\n", chord);
                if (chord == 0x122 || chord == 0x922) {
                    // accord de transition
                    shift += 2;
                    printf("shift = %d\n", shift);
                } else if (chord == 0x092 || chord == 0x292 || chord == 0x212) {
                    // accord de transition
                    shift -= 2;
                    printf("shift = %d\n", shift);
                } else if (chord == 0x910 || chord == 0x914) {
                    // accord de transition
                    shift -= 7;
                    printf("shift = %d\n", shift);
                } else if (chord == 0x452 || chord == 0x442) {
                    // accord de transition
                    shift += 7;
                    printf("shift = %d\n", shift);
                }
            } else {
                // pedale da gauche de bas en haut
            }
        }

        // handle console commands
        if (console && note_on) {
            // note on avec pedale de gauche en bas
            int rel_note = note - 60; // 0 = DO in the middle
            if (rel_note > (-12) && rel_note < 12) {
                shift = rel_note - 4;
                printf("shift = %d\n", shift);
            }
            if (rel_note == -12) {
                exit_count++;
                if (exit_count >= 5) {
                    scale(midio);
                    exit(2); // exit and halt
                }
            }
            if (rel_note == -13) {
                exit_count++;
                if (exit_count >= 5) {
                    exit(3); // exit only
                }
            }
        }

        // transpose
        if (note_on) {
            if (console) {
                fwd = false;
            } else {
                if (fwd_vel[note] != 0)
                    printf("WARNING: unexpected note on message\n");
                fnote = (note + shift);
                if (fnote < 0 || fnote >= 128) {
                    fwd = false;
                } else {
                    fwd_vel[note] = vel;
                    fwd_note[note] = fnote;
                    msg.bytes[1] = fnote;
                }
            }
        }
        if (note_off) {
            if (fwd_vel[note] == 0) {
                fwd = false;
            } else {
                fnote = fwd_note[note];
                fwd_vel[note] = 0;
                msg.bytes[1] = fnote;
            }
        }

        // forward
        if (fwd) {
            if (msg.port == beatstep_port)
                msg.port = -1;

            // send midi command out
            midio_send(midio, &msg);

            // midio_print_msg(&msg);
        }
    }

    return 0;
}
