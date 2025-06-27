# Makefile for GPIO People Counter Driver
obj-m += count.o

# 커널 소스 디렉토리
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# 사용자 프로그램
RX_PROG := rx
RX_SRC := rx.c
TX_PROG := tx
TX_SRC := tx.c

# 기본 타겟
all: module userspace

# 커널 모듈 빌드
module:
	@echo "Building kernel module..."
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# 사용자 프로그램 빌드
userspace: $(RX_PROG) $(TX_PROG)

$(RX_PROG): $(RX_SRC)
	@echo "Building receiver program..."
	gcc -Wall -Wextra -O2 -o $(RX_PROG) $(RX_SRC)

$(TX_PROG): $(TX_SRC)
	@echo "Building transmitter program..."
	gcc -Wall -Wextra -O2 -o $(TX_PROG) $(TX_SRC)

# 모듈 설치 (root 권한 필요)
install: module
	@echo "Installing kernel module..."
	sudo insmod count.ko
	@echo "Module installed. Check with: lsmod | grep count"

# 모듈 제거
uninstall:
	@echo "Removing kernel module..."
	-sudo rmmod count
	@echo "Module removed."

# GPIO export/unexport 헬퍼
export-gpio:
	@echo "Exporting GPIO 17..."
	@echo 17 | sudo tee /sys/class/sysprog_gpio/export

unexport-gpio:
	@echo "Unexporting GPIO 17..."
	@echo 17 | sudo tee /sys/class/sysprog_gpio/unexport

# 테스트 실행 (수신측)
test-rx: install export-gpio
	@echo "Starting receiver test program..."
	./$(RX_PROG) /dev/gpio17

# 테스트 실행 (송신측)
test-tx:
	@echo "Starting transmitter test program..."
	./$(TX_PROG)

# 완전 정리
clean: uninstall
	@echo "Cleaning build files..."
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f $(RX_PROG) $(TX_PROG)
	@echo "Clean complete."

# 개발용 타겟들
rebuild: clean all

reload: uninstall install

# 디버그 정보 출력
debug:
	@echo "Kernel version: $(shell uname -r)"
	@echo "Kernel dir: $(KDIR)"
	@echo "PWD: $(PWD)"
	@echo "Module file: count.ko"
	@echo "User programs: $(RX_PROG), $(TX_PROG)"

# 도움말
help:
	@echo "Available targets:"
	@echo "  all         - Build both kernel module and user programs (default)"
	@echo "  module      - Build kernel module only"
	@echo "  userspace   - Build user programs only"
	@echo "  install     - Install kernel module"
	@echo "  uninstall   - Remove kernel module"
	@echo "  export-gpio - Export GPIO 17"
	@echo "  unexport-gpio - Unexport GPIO 17"
	@echo "  test-rx     - Install module, export GPIO, and run receiver"
	@echo "  test-tx     - Run transmitter program"
	@echo "  clean       - Remove module and clean build files"
	@echo "  rebuild     - Clean and build everything"
	@echo "  reload      - Uninstall and reinstall module"
	@echo "  debug       - Show debug information"
	@echo "  help        - Show this help"

.PHONY: all module userspace install uninstall export-gpio unexport-gpio test-rx test-tx clean rebuild reload debug help
