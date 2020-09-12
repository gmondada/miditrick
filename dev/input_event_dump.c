#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>

const char *type_to_str(int type)
{
    switch (type) {
        case EV_SYN:       return "EV_SYN";
        case EV_KEY:       return "EV_KEY";
        case EV_REL:       return "EV_REL";
        case EV_ABS:       return "EV_ABS";
        case EV_MSC:       return "EV_MSC";
        case EV_SW:        return "EV_SW";
        case EV_LED:       return "EV_LED";
        case EV_SND:       return "EV_SND";
        case EV_REP:       return "EV_REP";
        case EV_FF:        return "EV_FF";
        case EV_PWR:       return "EV_PWR";
        case EV_FF_STATUS: return "EV_FF_STATUS";
        case EV_MAX:       return "EV_MAX";
        default:           return NULL;
    }
}

int main(int argc, char *argv[])
{
    struct input_event ev;
    char *fn = "/dev/input/event0";

    int fd = open(fn, O_RDONLY);
    if (fd == -1) {
        printf("ERROR: cannot open %s\n", fn);
        return -1;
    }

    for (;;) {
        read(fd, &ev, sizeof(ev));

        char *type_str = type_to_str(ev.type);
        char type_str_buf[16];
        if (!type_str) {
            snprintf(type_str_buf, sizeof(type_str_buf), "%d", ev.type);
            type_str = type_str_buf;
        }

        printf("type=%s code=%d value=%d\n", type_str, ev.code, ev.value);

        if (ev.type == EV_KEY && ev.code == BTN_LEFT)
            printf("    => mouse button\n");
    }

    close(fd);

    return 0;
}
