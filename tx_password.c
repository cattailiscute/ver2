#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>

// GPIO device paths
#define GPIO_TX_DATA "/sys/class/password_gpio/gpio26/value"
#define GPIO_TX_CLK  "/sys/class/password_gpio/gpio27/value"

// Transmission settings
#define BIT_DELAY_US 50000    // 50ms
#define SETUP_TIME_US 5000    // Data setup time
#define LOCK_DURATION 30
#define MAX_FAIL 5

int fd_data, fd_clk;
int total_sent = 0;
int transmission_progress = 0;

// Color definitions
#define COLOR_RESET     "\033[0m"
#define COLOR_RED       "\033[31m"
#define COLOR_GREEN     "\033[32m"
#define COLOR_YELLOW    "\033[33m"
#define COLOR_BLUE      "\033[34m"
#define COLOR_MAGENTA   "\033[35m"
#define COLOR_CYAN      "\033[36m"
#define COLOR_WHITE     "\033[37m"
#define COLOR_BOLD      "\033[1m"

// Screen control functions
void clear_screen() {
    printf("\033[2J\033[H");
}

void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
}

// Progress bar display
void show_progress(int current, int total, const char* message) {
    int width = 40;
    int progress = (current * width) / total;
    
    printf("\r" COLOR_CYAN "%s: [", message);
    for (int i = 0; i < width; i++) {
        if (i < progress) printf(COLOR_GREEN "█");
        else printf(COLOR_WHITE "▒");
    }
    printf(COLOR_CYAN "] %d/%d" COLOR_RESET, current, total);
    fflush(stdout);
}

void write_gpio_value(int fd, int value) {
    const char *val = value ? "1" : "0";
    lseek(fd, 0, SEEK_SET);
    if (write(fd, val, 1) < 0) {
        printf(COLOR_RED "\n❌ GPIO write error: %s\n" COLOR_RESET, strerror(errno));
    }
}

void send_bit(int bit) {
    total_sent++;
    
    // Data setup
    write_gpio_value(fd_data, bit);
    usleep(SETUP_TIME_US);
    
    // Clock pulse
    write_gpio_value(fd_clk, 1);
    usleep(BIT_DELAY_US / 2);
    write_gpio_value(fd_clk, 0);
    usleep(BIT_DELAY_US / 2);
}

void send_start_sequence() {
    printf(COLOR_YELLOW "\n🚀 Sending start sequence...\n" COLOR_RESET);
    unsigned char start_byte = 0xAA; // 10101010
    
    for (int i = 7; i >= 0; --i) {
        int bit = (start_byte >> i) & 1;
        send_bit(bit);
        show_progress(7-i+1, 8, "Start Signal");
        usleep(10000);
    }
    printf(COLOR_GREEN "\n✅ Start sequence sent successfully!\n" COLOR_RESET);
}

void send_password(const char* password) {
    printf(COLOR_CYAN "\n📤 Transmitting password: '%s'\n" COLOR_RESET);
    
    int total_bits = 32; // 4 characters * 8 bits
    int bit_count = 0;
    
    for (int i = 0; i < 4; ++i) {
        unsigned char ch = password[i];
        printf(COLOR_WHITE "Sending character '%c' (0x%02X): ", ch, ch);
        
        for (int b = 7; b >= 0; --b) {
            int bit = (ch >> b) & 1;
            send_bit(bit);
            bit_count++;
            
            // Real-time progress update
            show_progress(bit_count, total_bits, "Password");
            usleep(5000);
        }
        printf(COLOR_GREEN " ✓\n" COLOR_RESET);
    }
    printf(COLOR_GREEN "\n🎯 Password transmission complete!\n" COLOR_RESET);
}

void display_header() {
    clear_screen();
    printf(COLOR_BOLD COLOR_CYAN);
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║" COLOR_WHITE "                  📡 PASSWORD TRANSMITTER                    " COLOR_CYAN "║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(COLOR_RESET "\n");
}

void display_status(int fail_count, time_t lock_until) {
    time_t now = time(NULL);
    
    printf(COLOR_BOLD COLOR_BLUE "📊 SYSTEM STATUS:\n" COLOR_RESET);
    printf("─────────────────────────────────────────────\n");
    
    if (now < lock_until) {
        printf(COLOR_RED "🔒 Status: LOCKED (%ld seconds remaining)\n" COLOR_RESET, lock_until - now);
    } else {
        printf(COLOR_GREEN "🔓 Status: READY TO TRANSMIT\n" COLOR_RESET);
    }
    
    printf(COLOR_WHITE "Failed attempts: %d/%d\n", fail_count, MAX_FAIL);
    printf("Expected password: 1234\n");
    printf("Total bits sent: %d\n" COLOR_RESET, total_sent);
    printf("─────────────────────────────────────────────\n\n");
}

void display_instructions() {
    printf(COLOR_BOLD COLOR_YELLOW "📋 INSTRUCTIONS:\n" COLOR_RESET);
    printf("• Enter a 4-digit password to transmit\n");
    printf("• Type 'exit' to quit the program\n");
    printf("• Correct password: " COLOR_GREEN "1234" COLOR_RESET "\n");
    printf("• Invalid input will send '0000' as dummy\n\n");
}

int main() {
    display_header();
    
    printf(COLOR_CYAN "🔧 Initializing GPIO pins...\n" COLOR_RESET);
    
    // GPIO setup
    system("echo '26' > /sys/class/password_gpio/export 2>/dev/null");
    system("echo '27' > /sys/class/password_gpio/export 2>/dev/null");
    system("echo 'out' > /sys/class/password_gpio/gpio26/direction");
    system("echo 'out' > /sys/class/password_gpio/gpio27/direction");
    
    // GPIO file opening
    fd_data = open(GPIO_TX_DATA, O_WRONLY);
    if (fd_data < 0) {
        printf(COLOR_RED "❌ Error: Cannot open data GPIO (%s)\n" COLOR_RESET, GPIO_TX_DATA);
        return 1;
    }
    
    fd_clk = open(GPIO_TX_CLK, O_WRONLY);
    if (fd_clk < 0) {
        printf(COLOR_RED "❌ Error: Cannot open clock GPIO (%s)\n" COLOR_RESET, GPIO_TX_CLK);
        close(fd_data);
        return 1;
    }
    
    // Initial state setup
    write_gpio_value(fd_data, 0);
    write_gpio_value(fd_clk, 0);
    
    printf(COLOR_GREEN "✅ GPIO initialized successfully!\n" COLOR_RESET);
    sleep(1);
    
    // Main variables
    int fail_count = 0;
    time_t lock_until = 0;
    
    while (1) {
        display_header();
        display_status(fail_count, lock_until);
        
        time_t now = time(NULL);
        if (now < lock_until) {
            printf(COLOR_RED "⏰ System is locked. Please wait %ld seconds...\n" COLOR_RESET, lock_until - now);
            sleep(1);
            continue;
        }
        
        display_instructions();
        
        // Clear input buffer
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        
        // User input
        char input[32];
        printf(COLOR_BOLD COLOR_WHITE "Enter 4-digit password: " COLOR_RESET);
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) continue;
        input[strcspn(input, "\n")] = '\0';
        
        // Exit check
        if (strcmp(input, "exit") == 0) {
            printf(COLOR_CYAN "\n👋 Goodbye!\n" COLOR_RESET);
            break;
        }
        
        // Input validation
        char to_send[5];
        if (strlen(input) != 4 || strspn(input, "0123456789") != 4) {
            printf(COLOR_RED "\n⚠️  Invalid input! Sending '0000' as dummy.\n" COLOR_RESET);
            strcpy(to_send, "0000");
            fail_count++;
        } else {
            strcpy(to_send, input);
            printf(COLOR_GREEN "\n✅ Valid input received!\n" COLOR_RESET);
        }
        
        // Start transmission
        printf(COLOR_BOLD COLOR_MAGENTA "\n" "="*60 "\n");
        printf("🎯 STARTING TRANSMISSION\n");
        printf("="*60 "\n" COLOR_RESET);
        
        send_start_sequence();
        sleep(1);
        send_password(to_send);
        
        // Result processing
        if (strcmp(to_send, "1234") == 0) {
            printf(COLOR_GREEN COLOR_BOLD "\n🎉 SUCCESS! Correct password transmitted!\n");
            printf("The receiving system should now unlock.\n" COLOR_RESET);
            printf(COLOR_CYAN "\nPress Enter to exit..." COLOR_RESET);
            getchar();
            break;
        } else if (strcmp(to_send, "0000") != 0) {
            fail_count++;
            printf(COLOR_RED "\n❌ Incorrect password sent.\n" COLOR_RESET);
        }
        
        // Lock check
        if (fail_count >= MAX_FAIL) {
            lock_until = time(NULL) + LOCK_DURATION;
            printf(COLOR_RED COLOR_BOLD "\n🚫 TOO MANY FAILED ATTEMPTS!\n");
            printf("System locked for %d seconds.\n" COLOR_RESET, LOCK_DURATION);
        }
        
        printf(COLOR_CYAN "\nPress Enter to continue..." COLOR_RESET);
        getchar();
    }
    
    // Cleanup
    write_gpio_value(fd_data, 0);
    write_gpio_value(fd_clk, 0);
    close(fd_data);
    close(fd_clk);
    
    printf(COLOR_GREEN "\n✨ Thank you for using Password Transmitter!\n" COLOR_RESET);
    return 0;
}
