// tx.c - 송신측 프로그램 (GPIO 26번 제어)
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

#define GPIO_PIN 26
#define GPIO_BASE_PATH "/sys/class/sysprog_gpio"
#define GPIO_EXPORT_PATH "/sys/class/sysprog_gpio/export"
#define GPIO_UNEXPORT_PATH "/sys/class/sysprog_gpio/unexport"

static volatile int running = 1;
static char gpio_direction_path[64];
static char gpio_value_path[64];

// 신호 핸들러
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n[TX] Shutting down...\n");
        running = 0;
    }
}

// GPIO 초기화
int gpio_init() {
    FILE *export_file;
    
    // 먼저 기존에 export된 GPIO가 있는지 확인하고 unexport
    snprintf(gpio_direction_path, sizeof(gpio_direction_path), 
             "%s/gpio%d/direction", GPIO_BASE_PATH, GPIO_PIN);
    
    if (access(gpio_direction_path, F_OK) == 0) {
        printf("[TX] GPIO %d already exported in sysprog_gpio\n", GPIO_PIN);
    } else {
        printf("[TX] Exporting GPIO %d to sysprog_gpio...\n", GPIO_PIN);
        // GPIO export
        export_file = fopen(GPIO_EXPORT_PATH, "w");
        if (export_file == NULL) {
            perror("fopen export");
            return -1;
        }
        fprintf(export_file, "%d", GPIO_PIN);
        fclose(export_file);
        
        // export 후 안정화 대기
        usleep(500000); // 500ms 대기
    }
    
    // 경로 설정
    snprintf(gpio_direction_path, sizeof(gpio_direction_path), 
             "%s/gpio%d/direction", GPIO_BASE_PATH, GPIO_PIN);
    snprintf(gpio_value_path, sizeof(gpio_value_path), 
             "%s/gpio%d/value", GPIO_BASE_PATH, GPIO_PIN);
    
    // export 후 안정화 대기
    usleep(500000); // 500ms 대기
    
    // direction을 output으로 설정 (여러 번 시도)
    int retry_count = 0;
    while (retry_count < 5) {
        FILE *direction_file = fopen(gpio_direction_path, "w");
        if (direction_file != NULL) {
            if (fprintf(direction_file, "out") > 0) {
                fclose(direction_file);
                break;
            }
            fclose(direction_file);
        }
        retry_count++;
        printf("[TX] Retry setting direction (%d/5)...\n", retry_count);
        usleep(200000); // 200ms 대기 후 재시도
    }
    
    if (retry_count >= 5) {
        fprintf(stderr, "[TX] Failed to set GPIO direction after 5 attempts\n");
        return -1;
    }
    
    printf("[TX] GPIO %d initialized as output\n", GPIO_PIN);
    return 0;
}

// GPIO 정리
void gpio_cleanup() {
    FILE *unexport_file = fopen(GPIO_UNEXPORT_PATH, "w");
    if (unexport_file != NULL) {
        fprintf(unexport_file, "%d", GPIO_PIN);
        fclose(unexport_file);
        printf("[TX] GPIO %d unexported\n", GPIO_PIN);
    }
}

// GPIO 값 설정
int gpio_set_value(int value) {
    FILE *value_file = fopen(gpio_value_path, "w");
    if (value_file == NULL) {
        perror("fopen value");
        return -1;
    }
    fprintf(value_file, "%d", value);
    fclose(value_file);
    return 0;
}

// 정밀한 마이크로초 대기
void precise_usleep(int microseconds) {
    struct timespec req, rem;
    req.tv_sec = microseconds / 1000000;
    req.tv_nsec = (microseconds % 1000000) * 1000;
    nanosleep(&req, &rem);
}

// 입장 신호 생성 (100ms 펄스)
void send_entry_signal() {
    printf("[TX] Sending ENTRY signal (100ms pulse)...\n");
    gpio_set_value(1);
    precise_usleep(100000); // 100ms
    gpio_set_value(0);
}

// 퇴장 신호 생성 (200ms 펄스)
void send_exit_signal() {
    printf("[TX] Sending EXIT signal (200ms pulse)...\n");
    gpio_set_value(1);
    precise_usleep(200000); // 200ms
    gpio_set_value(0);
}

// 대화형 모드
void interactive_mode() {
    char input[10];
    
    printf("\n=== Interactive Mode ===\n");
    printf("Commands:\n");
    printf("  e / entry  - Send entry signal\n");
    printf("  x / exit   - Send exit signal\n");
    printf("  q / quit   - Quit program\n");
    printf("  h / help   - Show this help\n");
    printf("========================\n\n");
    
    while (running) {
        printf("[TX] Enter command: ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        
        // 개행 문자 제거
        input[strcspn(input, "\n")] = 0;
        
        if (strcmp(input, "e") == 0 || strcmp(input, "entry") == 0) {
            send_entry_signal();
        } else if (strcmp(input, "x") == 0 || strcmp(input, "exit") == 0) {
            send_exit_signal();
        } else if (strcmp(input, "q") == 0 || strcmp(input, "quit") == 0) {
            break;
        } else if (strcmp(input, "h") == 0 || strcmp(input, "help") == 0) {
            printf("Commands: e(entry), x(exit), q(quit), h(help)\n");
        } else if (strlen(input) > 0) {
            printf("Unknown command: %s\n", input);
        }
    }
}

// 자동 테스트 모드
void auto_test_mode() {
    printf("\n=== Auto Test Mode ===\n");
    printf("Sending test sequence...\n");
    
    for (int i = 0; i < 5 && running; i++) {
        printf("[TX] Test %d: Entry signal\n", i + 1);
        send_entry_signal();
        sleep(2);
        
        if (!running) break;
        
        printf("[TX] Test %d: Exit signal\n", i + 1);
        send_exit_signal();
        sleep(2);
    }
    
    printf("[TX] Auto test completed\n");
}

int main(int argc, char *argv[]) {
    int auto_mode = 0;
    
    // 명령행 인수 처리
    if (argc > 1 && strcmp(argv[1], "-auto") == 0) {
        auto_mode = 1;
    }
    
    printf("[TX] People Counter Signal Transmitter\n");
    printf("[TX] GPIO Pin: %d\n", GPIO_PIN);
    
    // 신호 핸들러 등록
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // GPIO 초기화
    if (gpio_init() < 0) {
        fprintf(stderr, "[TX] GPIO initialization failed\n");
        return 1;
    }
    
    // 초기 상태를 LOW로 설정
    gpio_set_value(0);
    
    if (auto_mode) {
        auto_test_mode();
    } else {
        interactive_mode();
    }
    
    // 정리
    gpio_set_value(0); // 최종 상태를 LOW로
    gpio_cleanup();
    
    printf("[TX] Program terminated\n");
    return 0;
}
