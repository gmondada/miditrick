//
//  midio.c
//
//  Created by Gabriele Mondada on May 23, 2021.
//  Copyright (c) 2021 Gabriele Mondada.
//  Distributed under the terms of the MIT License.
//

#include <stdio.h>
#include "midio.h"


void midio_print_msg(MIDIO_MSG *msg)
{
    if (msg->size == 2)
        printf("%d: %02X %02X\n", msg->port, msg->bytes[0] & 0xFF, msg->bytes[1] & 0xFF);
    else if (msg->size == 3)
        printf("%d: %02X %02X %02X\n", msg->port, msg->bytes[0] & 0xFF, msg->bytes[1] & 0xFF, msg->bytes[2] & 0xFF);
    else
        printf("%d: ?\n", msg->port);
}
