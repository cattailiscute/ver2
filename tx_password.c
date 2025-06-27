#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>

// GPIO 디바이스 경로
#define GPIO_RX_DATA "/dev/gpio17"
#define GPIO_RX_CLK  "/dev/gpio19"

// IOCTL 명령
#define GPIO_IOCTL_MAGIC       'G'
#define GPIO_IOCTL_ENABLE_IRQ  _IOW(GPIO_IOCTL_MAGIC, 1, int)
#define GPIO_IOCTL_DISABLE_IRQ _IOW(GPIO_IOCTL_MAGIC, 2, int)

// 시스템 설정
#define MAX_FAIL 5
#define LOCK_TIME_SEC 30
#define PASSWORD_LEN 4
#define MAX_BITS (PASSWORD_LEN * 8)

// 전역 변수
int fd_data, fd_clk;
unsigned char bits[MAX_BITS];
int bit_index = 0;
int fail_count = 0;
time_t lock_until = 0;
int unlocked = 0;
int synced = 0;
unsigned char sync_buf[8] = {0};
char correct_pw[] = "1234";

// 상태 표시용
int total_received = 0;
int sync_attempts = 0;

// 화면 지우기 및 커서 이동
void clear_screen() {
    printf("\033[2J\033[H");
}

void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

// 색상 정의
#define COLOR_RESET     "\033[0m"
#define COLOR_RED       "\033[31m"
#define COLOR_GREEN     "\033[32m"
#define COLOR_YELLOW    "\033[33m"
#define COLOR_BLUE      "\033[34m"
#define COLOR_MAGENTA   "\033[35m"
#define COLOR_CYAN      "\033[36m"
#define COLOR_WHITE     "\033[37m"
#define COLOR_BOLD      "\033[1m"

// 상태 화면 업데이트
void update_status_display() {
    move_cursor(1, 1);
    printf(COLOR_BOLD COLOR_CYAN "╔════════════════════════════════════════════════════════════╗\n");
    printf("║" COLOR_WHITE "                    🔐 PASSWORD LOCK SYSTEM                   " COLOR_CYAN "║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    
    // 시스템 상태
    time_t now = time(NULL);
    if (unlocked) {
        printf("║ " COLOR_GREEN "Status: ✅ UNLOCKED                                        " COLOR_CYAN "║\n");
    } else if (now < lock_until) {
        printf("║ " COLOR_RED "Status: 🔒 LOCKED (%ld seconds remaining)                  " COLOR_CYAN "║\n", lock_until - now);
    } else if (synced) {
        printf("║ " COLOR_YELLOW "Status: 📥 RECEIVING PASSWORD... (%d/%d bits)              " COLOR_CYAN "║\n", bit_index, MAX_BITS);
    } else {
        printf("║ " COLOR_BLUE "Status: 📡 WAITING FOR TRANSMISSION...                    " COLOR_CYAN "║\n");
    }
    
    printf("╠════════════════════════════════════════════════════════════╣\n");
    
    // 통계
    printf("║ " COLOR_WHITE "Failed Attempts: %d/%d                                    " COLOR_CYAN "║\n", fail_count, MAX_FAIL);
    printf("║ " COLOR_WHITE "Signals Received: %-4d                                     " COLOR_CYAN "║\n", total_received);
    printf("║ " COLOR_WHITE "Expected Password: %s                                     " COLOR_CYAN "║\n", correct_pw);
    
    printf("╠════════════════════════════════════════════════════════════╣\n");
    
    // 진행률 표시 (비밀번호 수신 중일 때)
    if (synced && bit_index > 0) {
        printf("║ " COLOR_WHITE "Progress: ");
        int progress = (bit_index * 40) / MAX_BITS;
        for (int i = 0; i < 40; i++) {
            if (i < progress) printf(COLOR_GREEN "█");
            else printf(COLOR_WHITE "▒");
        }
        printf(" " COLOR_CYAN "║\n");
        
        // 받은 비트들을 문자별로 표시
        printf("║ " COLOR_WHITE "Data: ");
        for (int i = 0; i < bit_index; i += 8) {
            if (i + 7 < bit_index) {
                char c = 0;
                for (int j = 0; j < 8; j++) {
                    c = (c << 1) | (bits[i + j] & 1);
                }
                printf(COLOR_GREEN "'%c'" COLOR_WHITE " ", c);
            } else {
                printf(COLOR_YELLOW "..." COLOR_WHITE " ");
            }
        }
        for (int i = 0; i < 50 - bit_index/2; i++) printf(" ");
        printf(COLOR_CYAN "║\n");
    } else {
        printf("║ " COLOR_WHITE "Progress: " COLOR_WHITE "Waiting for start signal...                     " COLOR_CYAN "║\n");
        printf("║ " COLOR_WHITE "Data:                                                      " COLOR_CYAN "║\n");
    }
    
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    // 로그 영역 헤더
    printf(COLOR_BOLD COLOR_WHITE "\n📋 ACTIVITY LOG:\n" COLOR_RESET);
    printf("────────────────────────────────────────────────────────────\n");
}

char bits_to_char(unsigned char *bits) {
    char c = 0;
    for (int i = 0; i < 8; ++i)
        c = (c << 1) | (bits[i] & 1);
    return c;
}

void check_password() {
    char recv[16] = {0};
    for (int i = 0; i < MAX_BITS; i += 8)
        recv[i / 8] = bits_to_char(&bits[i]);
    recv[PASSWORD_LEN] = '\0';
    
    printf(COLOR_BOLD "🔍 Password received: '%s'\n" COLOR_RESET, recv);
    
    if (strcmp(recv, correct_pw) == 0) {
        printf(COLOR_GREEN COLOR_BOLD "🎉 SUCCESS! Password correct - System unlocked!\n" COLOR_RESET);
        unlocked = 1;
        fail_count = 0;
    } else {
        fail_count++;
        if (fail_count >= MAX_FAIL) {
            lock_until = time(NULL) + LOCK_TIME_SEC;
            printf(COLOR_RED COLOR_BOLD "🚫 SYSTEM LOCKED! Too many failed attempts (%d)\n" COLOR_RESET, fail_count);
            printf(COLOR_RED "⏰ Please wait %d seconds before next attempt.\n" COLOR_RESET, LOCK_TIME_SEC);
        } else {
            printf(COLOR_RED "❌ Incorrect password. Attempt %d/%d\n" COLOR_RESET, fail_count, MAX_FAIL);
        }
    }
    
    // 상태 초기화
    bit_index = 0;
    memset(bits, 0, sizeof(bits));
    synced = 0;
    memset(sync_buf, 0, sizeof(sync_buf));
    sync_attempts = 0;
}

void sigio_handler(int signo) {
    total_received++;
    time_t now = time(NULL);
    
    if (now < lock_until || unlocked) {
        // 잠금 상태이거나 이미 해제된 경우 무시
        bit_index = 0;
        memset(bits, 0, sizeof(bits));
        synced = 0;
        memset(sync_buf, 0, sizeof(sync_buf));
        return;
    }
    
    // 데이터 안정화 대기
    usleep(2000);
    
    char buf[2] = {0};
    ssize_t read_result;
    
    // 시그널 안전한 읽기
    do {
        read_result = read(fd_data, buf, 1);
    } while (read_result < 0 && errno == EINTR);
    
    if (read_result <= 0) {
        return;
    }
    
    int bit = (buf[0] == '1') ? 1 : 0;
    
    if (!synced) {
        // 동기화 시도
        for (int i = 0; i < 7; ++i)
            sync_buf[i] = sync_buf[i+1];
        sync_buf[7] = bit;
        
        unsigned char pattern = 0;
        for (int i = 0; i < 8; ++i)
            pattern = (pattern << 1) | sync_buf[i];
        
        sync_attempts++;
        
        if (pattern == 0xAA) {  // 시작 패턴 감지
            synced = 1;
            bit_index = 0;
            memset(bits, 0, sizeof(bits));
            printf(COLOR_GREEN "🎯 Start signal detected! Receiving password...\n" COLOR_RESET);
        }
        
        // 화면 업데이트 (10번에 한 번)
        if (sync_attempts % 10 == 0) {
            update_status_display();
        }
        return;
    }
    
    // 동기화된 상태에서 데이터 수집
    if (synced && bit_index < MAX_BITS) {
        bits[bit_index] = bit;
        bit_index++;
        
        // 화면 업데이트 (매 비트마다)
        update_status_display();
        
        if (bit_index >= MAX_BITS) {
            printf(COLOR_CYAN "📦 All bits received. Checking password...\n" COLOR_RESET);
            check_password();
            update_status_display();
        }
    }
}

int main() {
    // 화면 초기화
    clear_screen();
    
    printf(COLOR_BOLD COLOR_CYAN "Initializing Password Lock System...\n" COLOR_RESET);
    
    // GPIO 설정
    system("echo '17' > /sys/class/password_gpio/export 2>/dev/null");
    system("echo '19' > /sys/class/password_gpio/export 2>/dev/null");
    system("echo 'in' > /sys/class/password_gpio/gpio17/direction");
    system("echo 'in' > /sys/class/password_gpio/gpio19/direction");
    
    // 디바이스 파일 열기
    fd_data = open(GPIO_RX_DATA, O_RDONLY);
    if (fd_data < 0) {
        printf(COLOR_RED "❌ Error: Cannot open data GPIO device\n" COLOR_RESET);
        printf("Make sure the driver is loaded and GPIO is exported\n");
        return 1;
    }
    
    fd_clk = open(GPIO_RX_CLK, O_RDONLY | O_NONBLOCK);
    if (fd_clk < 0) {
        printf(COLOR_RED "❌ Error: Cannot open clock GPIO device\n" COLOR_RESET);
        close(fd_data);
        return 1;
    }
    
    // 시그널 핸들러 설정
    struct sigaction sa;
    sa.sa_handler = sigio_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGIO, &sa, NULL);
    
    fcntl(fd_clk, F_SETOWN, getpid());
    int flags = fcntl(fd_clk, F_GETFL);
    fcntl(fd_clk, F_SETFL, flags | O_ASYNC | O_NONBLOCK);
    
    // IRQ 활성화
    if (ioctl(fd_clk, GPIO_IOCTL_ENABLE_IRQ, 0) < 0) {
        printf(COLOR_RED "❌ Error: Cannot enable GPIO interrupt\n" COLOR_RESET);
        close(fd_data);
        close(fd_clk);
        return 1;
    }
    
    printf(COLOR_GREEN "✅ System initialized successfully!\n" COLOR_RESET);
    sleep(2);
    
    // 메인 화면 표시
    clear_screen();
    update_status_display();
    
    printf(COLOR_GREEN "🔐 Password Lock System is ready!\n" COLOR_RESET);
    printf(COLOR_YELLOW "Waiting for password transmission from TX device...\n" COLOR_RESET);
    printf(COLOR_WHITE "Press Ctrl+C to exit\n\n" COLOR_RESET);
    
    // 메인 루프
    while (!unlocked) {
        sleep(1);
        update_status_display();
        
        if (lock_until > 0 && time(NULL) < lock_until) {
            printf(COLOR_RED "⏰ System locked. Please wait...\n" COLOR_RESET);
        } else if (synced) {
            printf(COLOR_YELLOW "📥 Receiving data...\n" COLOR_RESET);
        } else {
            printf(COLOR_BLUE "📡 Listening for signals...\n" COLOR_RESET);
        }
    }
    
    // 성공 화면
    clear_screen();
    printf(COLOR_GREEN COLOR_BOLD "\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                                                            ║\n");
    printf("║                    🎉 ACCESS GRANTED! 🎉                   ║\n");
    printf("║                                                            ║\n");
    printf("║                 Password lock system unlocked              ║\n");
    printf("║                                                            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET "\n");
    
    // 정리
    ioctl(fd_clk, GPIO_IOCTL_DISABLE_IRQ, 0);
    close(fd_data);
    close(fd_clk);
    
    return 0;
}
