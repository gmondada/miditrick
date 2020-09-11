
// http://www.gweep.net/~prefect/eng/reference/protocol/midispec.html

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "gmidi.h"


void beep(GMIDI_DEV *out, int note)
{
    GMIDI_MSG msg;
    msg.size = 3;
    msg.bytes[0] = 0x90;
    msg.bytes[1] = note;
    msg.bytes[2] = 0x20;
    gmidi_put(out, &msg, 1000);
    usleep(10 * 1000);  // 0.1 s
    msg.bytes[2] = 0x00;
    gmidi_put(out, &msg, 1000);
}

void scale(GMIDI_DEV *out)
{
    int i;
    for (i=0; i<12 ;i++) {
        beep(out, i + 0x3C);
    }
}

void send_note_off(GMIDI_DEV *out, int note)
{
	GMIDI_MSG msg = { 3 };
	int err;

	msg.bytes[0] = 0x90;
	msg.bytes[1] = (char)(note & 0x7F);
	err = gmidi_put(out, &msg, 1000);
	if (err) {
		printf("error %d\n", err);
		exit(1);
	}
}

int main(char *argv, int argc)
{

	GMIDI_DEV in, out;
	GMIDI_MSG msg;
	int err;
	bool console = false;
	int shift = 0;
	int exit_count = 0;

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
	 * The array content is the transposed note as it hab been sent to
	 * the synthetiser.
	 */
	char fwd_note[128] = {0};

	err = gmidi_open_in_dev(&in, 0);
	if (err) {
		printf("error %d\n", err);
		exit(1);
	}
	err = gmidi_open_out_dev(&out, 0);
	if (err) {
		printf("error %d\n", err);
		exit(1);
	}

	while(1) {
	    int cmd;   // midi command
		int note;  // when cmd = 8 or 9
		int vel;   // when cmd = 9
		bool note_on;
		bool note_off;
		bool fwd = true;
		int fnote; // forwarded (transposed) note

		// get next midi message
		err = gmidi_get(&in, &msg, 1000);
		if (err) {
			printf("error %d\n", err);
			exit(1);
		}
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
					scale(&out);
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
		    // send midi command out
    		err = gmidi_put(&out, &msg, 1000);
    		if (err) {
    			printf("error %d\n", err);
    			exit(1);
    		}
			// gmidi_show_msg(&msg);
		}
	}

	return 0;
}
