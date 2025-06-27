// rx.c - 깔끔한 출력 버전
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define GPIO_IOCTL_MAGIC       'G'
#define GPIO_IOCTL_ENABLE_IRQ  _IOW(GPIO_IOCTL_MAGIC, 1, int)
#define GPIO_IOCTL_DISABLE_IRQ _IOW(GPIO_IOCTL_MAGIC, 2, int)
#define GPIO_IOCTL_GET_COUNT   _IOR(GPIO_IOCTL_MAGIC, 3, int)
#define DEFAULT_GPIO_DEV "/dev/gpio17"

static volatile int running = 1;
static int gpio_fd = -1;

// 현재 시간 문자열 반환
void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    strftime(buffer, size, "%H:%M:%S", local);
}

// 신호 핸들러
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n[RX] Shutting down gracefully...\n");
        running = 0;
    }
    // SIGIO는 조용히 처리
}

// 현재 카운트 조회
int get_current_count(int fd) {
    int count = -1;
    if (ioctl(fd, GPIO_IOCTL_GET_COUNT, &count) < 0) {
        perror("ioctl - get count");
        return -1;
    }
    return count;
}

// 정리 함수
void cleanup() {
    if (gpio_fd >= 0) {
        printf("[RX] Disabling IRQ...\n");
        int dummy = 0;
        ioctl(gpio_fd, GPIO_IOCTL_DISABLE_IRQ, &dummy);
        close(gpio_fd);
        gpio_fd = -1;
    }
}

int main(int argc, char *argv[]) {
    const char *dev_path = DEFAULT_GPIO_DEV;
    char timestamp[16];

    if (argc >= 2) {
        dev_path = argv[1];
    } else {
        printf("Usage: %s [device_path]\n", argv[0]);
        printf("Using default: %s\n", dev_path);
    }

    // 신호 핸들러 등록
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGIO, signal_handler);

    // GPIO 디바이스 열기
    gpio_fd = open(dev_path, O_RDONLY);
    if (gpio_fd < 0) {
        perror("open");
        return 1;
    }

    // IRQ 활성화
    int dummy = 0;
    if (ioctl(gpio_fd, GPIO_IOCTL_ENABLE_IRQ, &dummy) < 0) {
        perror("ioctl - enable irq");
        close(gpio_fd);
        return 1;
    }

    // 비동기 신호 설정
    if (fcntl(gpio_fd, F_SETOWN, getpid()) < 0) {
        perror("fcntl F_SETOWN");
        cleanup();
        return 1;
    }

    int flags = fcntl(gpio_fd, F_GETFL);
    if (flags < 0 || fcntl(gpio_fd, F_SETFL, flags | O_ASYNC) < 0) {
        perror("fcntl O_ASYNC");
        cleanup();
        return 1;
    }

    printf("[RX] People Counter Monitor Started\n");
    printf("[RX] Device: %s\n", dev_path);
    
    // 초기 카운트 출력
    int initial_count = get_current_count(gpio_fd);
    if (initial_count >= 0) {
        get_timestamp(timestamp, sizeof(timestamp));
        printf("%s | Initial count: %d people\n", timestamp, initial_count);
    }

    printf("=====================================\n");

    int last_displayed_count = initial_count;
    
    while (running) {
        // 500ms마다 체크
        usleep(500000);
        
        int current_count = get_current_count(gpio_fd);
        if (current_count >= 0 && current_count != last_displayed_count) {
            get_timestamp(timestamp, sizeof(timestamp));
            
            if (current_count > last_displayed_count) {
                printf("%s | 👤➡️  ENTRY detected | Count: %d (+%d)\n", 
                       timestamp, current_count, current_count - last_displayed_count);
            } else {
                printf("%s | 👤⬅️  EXIT detected  | Count: %d (%d)\n", 
                       timestamp, current_count, current_count - last_displayed_count);
            }
            
            last_displayed_count = current_count;
            fflush(stdout);
        }
    }

    cleanup();
    printf("\n[RX] Monitor stopped.\n");
    return 0;
}
