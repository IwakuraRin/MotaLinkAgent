# 作用：编译 Go 语言 HostPC API 层，并保留清理入口。

GO ?= go
TARGET := hostpc-api

.PHONY: all clean run

all: $(TARGET)

$(TARGET):
	$(GO) build -o $(TARGET) ./cmd/hostpc-api

run:
	$(GO) run ./cmd/hostpc-api -addr 0.0.0.0:8080 -static ../frontend/dist

clean:
	rm -f $(TARGET)
