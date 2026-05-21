# 作用：编译 AmseokBot C 控制核心，C 层只保留硬件控制和低延迟命令能力。

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
CPPFLAGS ?= -Iinclude
LDLIBS ?= -lm

TARGET := amseokbot-control-core
COMPAT_TARGET := hostpc-c
SRC := \
	src/main.c \
	src/safety_guard.c \
	src/chassis_control.c \
	src/arm_control.c \
	src/motor_control.c \
	src/serial_protocol.c \
	src/file_management.c

.PHONY: all clean run demo

all: $(TARGET) $(COMPAT_TARGET)

$(TARGET): $(SRC) include/control_core.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

$(COMPAT_TARGET): $(TARGET)
	ln -sf $(TARGET) $(COMPAT_TARGET)

run: $(TARGET)
	./$(TARGET) health

demo: $(TARGET)
	./$(TARGET) chassis --vx 0.2 --vy 0 --wz 0
	./$(TARGET) arm --shoulder-yaw 0 --shoulder-pitch 30 --elbow 45 --wrist 0
	./$(TARGET) stop

clean:
	rm -f $(TARGET) $(COMPAT_TARGET)
