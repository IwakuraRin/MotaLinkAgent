# 作用：编译 C 语言版 OmniRoam HostPC 后端服务。

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
CPPFLAGS ?= -Iinclude
LDLIBS ?= -lssl -lcrypto -lpthread -lutil

TARGET := hostpc-c
SRC := src/hostpc_server.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC) include/hostpc_server.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

run: $(TARGET)
	./$(TARGET) -addr 0.0.0.0:8080 -static ../frontend/dist

clean:
	rm -f $(TARGET)
