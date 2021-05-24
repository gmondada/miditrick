
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <poll.h>
#include "midio.h"


/*** types ***/

struct midio_private {
    struct midio public;

    int dev_count;
    int devs[16];
    char names[16][32];

    struct pollfd pollfds[16];
};


/*** functions ***/

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
    struct midio_private *priv = (struct midio_private *)me;

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

            priv->devs[priv->dev_count] = fd;
            priv->pollfds[priv->dev_count].fd = fd;
            priv->pollfds[priv->dev_count].events = POLLIN;
            _strlcpy(priv->names[i], id, sizeof(priv->names[i]));
            priv->dev_count++;
        }
    }
}

void midio_close(MIDIO *me)
{
    struct midio_private *priv = (struct midio_private *)me;

    for (int i = 0; i < priv->dev_count; i++)
        close(priv->devs[i]);
    priv->dev_count = 0;
}

int midio_get_port_by_name(MIDIO *me, const char *name)
{
    struct midio_private *priv = (struct midio_private *)me;

    for (int i = 0; i < priv->dev_count; i++) {
        if (!strcmp(priv->names[i], name))
            return i;
    }
    return -1;
}

void midio_start_pump(MIDIO *me, void *ctx, void (* handler)(void *ctx, MIDIO_MSG *msg))
{
    MIDIO_MSG msg;

    for (;;) {
        // get next midi message
        midio_recv(me, &msg);
        if (msg.size == 0)
            continue;

        // dispatch the message
        handler(ctx, &msg);
    }
}

void midio_recv(MIDIO *me, MIDIO_MSG *msg)
{
    struct midio_private *priv = (struct midio_private *)me;

    for (int i = 0; i < priv->dev_count; i++)
        priv->pollfds[i].revents = 0;

    do_poll:;
    int rv = poll(priv->pollfds, priv->dev_count, -1);
    if (rv == -1) {
        if (errno == EINTR)
            goto do_poll;
        _fatal_error("poll error: errno=%d", errno);
    }

    for (int i = 0; i < priv->dev_count; i++) {
        if (priv->pollfds[i].revents) {
            do_read:;
            ssize_t rx = read(priv->devs[i], msg->bytes, 3);
            if (rx == -1) {
                if (errno == EINTR)
                    goto do_read;
                _fatal_error("read error: errno=%d", errno);
            }
            msg->port = i;
            msg->size = (int)rx;
            return;
        }
    }

    msg->port = -1;
    msg->size = 0;
}

void midio_send(MIDIO *me, MIDIO_MSG *msg)
{
    struct midio_private *priv = (struct midio_private *)me;

    if (msg->port == -1) {
        for (int i = 0; i < priv->dev_count; i++) {
            ssize_t ret = write(priv->devs[i], msg->bytes, msg->size);
            if (ret != msg->size)
                _fatal_error("write error: ret=%d errno=%d", ret, errno);
        }
    } else {
        ssize_t ret = write(priv->devs[msg->port], msg->bytes, msg->size);
        if (ret != msg->size)
            _fatal_error("write error: ret=%d errno=%d", ret, errno);
    }
}
