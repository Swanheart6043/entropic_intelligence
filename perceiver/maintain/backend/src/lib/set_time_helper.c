// set_time_helper.c
//
// Privileged helper：把毫秒级 Unix 时间戳同时写入系统时钟和硬件时钟（RTC）。
//
// Build:    gcc -Wall -Wextra -O2 -o set_time_helper set_time_helper.c
// Install:  sudo install -o root -g root -m 0755 set_time_helper /usr/local/bin/
//           sudo setcap 'cap_sys_time,cap_dac_override+ep' /usr/local/bin/set_time_helper
//
// Usage:    set_time_helper <unix_timestamp_milliseconds>
//             例: 1778322927324
//
// Exit codes:
//   0  success
//   2  bad arguments
//   3  timestamp out of sane range [2020, 2100)
//   4  clock_settime failed (likely missing CAP_SYS_TIME)
//   5  open /dev/rtc0 failed (likely missing CAP_DAC_OVERRIDE)
//   6  RTC ioctl failed

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>

#define MIN_MS 1577808000000LL   // 2020-01-01 00:00:00 UTC
#define MAX_MS 4102444800000LL   // 2100-01-01 00:00:00 UTC

enum {
    EXIT_OK         = 0,
    EXIT_ARGS       = 2,
    EXIT_RANGE      = 3,
    EXIT_CLOCK_SET  = 4,
    EXIT_RTC_OPEN   = 5,
    EXIT_RTC_IOCTL  = 6,
};

static int sync_rtc_from_system(void) {
    int fd = open("/dev/rtc0", O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "open /dev/rtc0: %s\n", strerror(errno));
        return EXIT_RTC_OPEN;
    }

    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        fprintf(stderr, "clock_gettime: %s\n", strerror(errno));
        close(fd);
        return EXIT_RTC_IOCTL;
    }

    // 现代 Ubuntu/systemd 默认 RTC 存 UTC。如果你的板子做了
    // `timedatectl set-local-rtc 1`，把 gmtime_r 换成 localtime_r。
    struct tm tmv;
    if (gmtime_r(&now.tv_sec, &tmv) == NULL) {
        fprintf(stderr, "gmtime_r failed\n");
        close(fd);
        return EXIT_RTC_IOCTL;
    }

    struct rtc_time rt = {
        .tm_sec   = tmv.tm_sec,
        .tm_min   = tmv.tm_min,
        .tm_hour  = tmv.tm_hour,
        .tm_mday  = tmv.tm_mday,
        .tm_mon   = tmv.tm_mon,
        .tm_year  = tmv.tm_year,
        .tm_wday  = tmv.tm_wday,
        .tm_yday  = tmv.tm_yday,
        .tm_isdst = 0,
    };

    if (ioctl(fd, RTC_SET_TIME, &rt) != 0) {
        fprintf(stderr, "ioctl RTC_SET_TIME: %s\n", strerror(errno));
        close(fd);
        return EXIT_RTC_IOCTL;
    }

    close(fd);
    return EXIT_OK;
}

int main(int argc, char **argv) {
    const char *prog = (argc > 0 && argv[0]) ? argv[0] : "set_time_helper";

    if (argc != 2) {
        fprintf(stderr, "usage: %s <unix_timestamp_milliseconds>\n"
                        "  例: %s 1778322927324\n",
                prog, prog);
        return EXIT_ARGS;
    }

    char *endp = NULL;
    errno = 0;
    long long ms = strtoll(argv[1], &endp, 10);
    if (errno != 0 || endp == argv[1] || *endp != '\0') {
        fprintf(stderr, "invalid timestamp: %s\n", argv[1]);
        return EXIT_ARGS;
    }

    if (ms < MIN_MS || ms >= MAX_MS) {
        fprintf(stderr, "timestamp out of sane range [2020, 2100): %lld ms\n", ms);
        return EXIT_RANGE;
    }

    struct timespec ts = {
        .tv_sec  = (time_t)(ms / 1000LL),
        .tv_nsec = (long)((ms % 1000LL) * 1000000L),
    };

    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        fprintf(stderr, "clock_settime: %s (errno=%d)\n", strerror(errno), errno);
        return EXIT_CLOCK_SET;
    }

    return sync_rtc_from_system();
}
