
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>

#include "midio.h"


static size_t _strlcpy(char *dst, const char *src, size_t siz)
{
    char *d = dst;
    const char *s = src;
    size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0) {
        while (--n != 0) {
            if ((*d++ = *s++) == '\0')
                break;
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (siz != 0)
            *d = '\0';                /* NUL-terminate dst */
        while (*s++)
            ;
    }

    return(s - src - 1);        /* count does not include NUL */
}

static void _fatal_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    abort();
}

MIDIO *midio_create(void)
{
    MIDIO *me = calloc(1, sizeof(*me));
    return me;
}

void midio_open(MIDIO *me)
{
    for (int i = 0; i < 4; i++) {
        char fn[256];
        snprintf(fn, sizeof(fn), "/dev/snd/midiC%dD0", i);

        int fd = open(fn, O_RDWR);
        if (fd >= 0) {
            char id_fn[256];
            char id[32] = {0};

            snprintf(id_fn, sizeof(id_fn), "/sys/class/sound/midiC%dD0/device/id", i);
            int id_fd = open(id_fn, O_RDONLY);
            read(id_fd, id, sizeof(id) - 1);
            close(id_fd);

            int n = (int)strlen(id);
            for (;;) {
                if (n == 0)
                    break;
                n--;
                if (id[n] > 32)
                    break;
                id[n] = 0;
            }

            printf("midio: open device %s (%s)\n", fn, id);

            me->devs[me->dev_count] = fd;
            me->pollfds[me->dev_count].fd = fd;
            me->pollfds[me->dev_count].events = POLLIN;
            _strlcpy(me->names[i], id, sizeof(me->names[i]));
            me->dev_count++;
        }
    }
}

void midio_close(MIDIO *me)
{
    for (int i = 0; i < me->dev_count; i++)
        close(me->devs[i]);
    me->dev_count = 0;
}

int midio_get_port_by_name(MIDIO *me, const char *name)
{
    for (int i = 0; i < me->dev_count; i++) {
        if (!strcmp(me->names[i], name))
            return i;
    }
    return -1;
}

void midio_recv(MIDIO *me, MIDIO_MSG *msg) {

    for (int i = 0; i < me->dev_count; i++)
        me->pollfds[i].revents = 0;

    do_poll:;
    int rv = poll(me->pollfds, me->dev_count, -1);
    if (rv == -1) {
        if (errno == EINTR)
            goto do_poll;
        _fatal_error("poll error: errno=%d", errno);
    }

    for (int i = 0; i < me->dev_count; i++) {
        if (me->pollfds[i].revents) {
            do_read:;
            int rx = read(me->devs[i], msg->bytes, 3);
            if (rx == -1) {
                if (errno == EINTR)
                    goto do_read;
                _fatal_error("read error: errno=%d", errno);
            }
            msg->port = i;
            msg->size = rx;
            return;
        }
    }

    msg->port = -1;
    msg->size = 0;
}

void midio_send(MIDIO *me, MIDIO_MSG *msg) {
    if (msg->port == -1) {
        for (int i = 0; i < me->dev_count; i++) {
            int ret = write(me->devs[i], msg->bytes, msg->size);
            if (ret != msg->size)
                _fatal_error("write error: ret=%d errno=%d", ret, errno);
        }
    } else {
        int ret = write(me->devs[msg->port], msg->bytes, msg->size);
        if (ret != msg->size)
            _fatal_error("write error: ret=%d errno=%d", ret, errno);
    }
}
