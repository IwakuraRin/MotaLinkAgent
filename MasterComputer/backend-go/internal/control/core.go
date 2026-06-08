// 作用：调用 AmseokBot C 控制核心，把 HTTP API 请求转换为本地低延迟控制命令。
package control

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os/exec"
	"strconv"
	"time"
)

// ==================== 控制核心客户端 ====================
// 作用：保存 C 控制核心路径，并统一封装进程调用超时和 JSON 解析。
// ======================================================
type Client struct {
	binaryPath string
	timeout    time.Duration
	fsTimeout  time.Duration
}

func NewClient(binaryPath string) Client {
	return Client{binaryPath: binaryPath, timeout: 2 * time.Second, fsTimeout: 10 * time.Minute}
}

// ==================== 控制命令结构 ====================
// 作用：描述 Go API 接收的底盘和 C 核心返回结果。
// ====================================================
type ChassisCommand struct {
	VXMps   float64 `json:"vx_mps"`
	VYMps   float64 `json:"vy_mps"`
	WZRadps float64 `json:"wz_radps"`
}

type Result map[string]any

type FSStatResult struct {
	OK      bool   `json:"ok"`
	Type    string `json:"type"`
	Path    string `json:"path"`
	Name    string `json:"name"`
	IsDir   bool   `json:"is_dir"`
	Size    int64  `json:"size"`
	Mode    uint32 `json:"mode"`
	ModTime string `json:"mod_time"`
}

type FSReadStream struct {
	Body io.ReadCloser
	Wait func() error
}

// ==================== 文件系统命令 ====================
// 作用：把文件删除、复制、移动、上传写入和下载读取交给 C 层执行。
// ======================================================
func (c Client) FSStat(ctx context.Context, path string) (FSStatResult, error) {
	var stat FSStatResult
	result, err := c.runWithTimeout(ctx, c.fsTimeout, "fs", "--op", "stat", "--path", path)
	if err != nil {
		return stat, err
	}
	body, _ := json.Marshal(result)
	if err := json.Unmarshal(body, &stat); err != nil {
		return stat, fmt.Errorf("parse fs stat response: %w", err)
	}
	return stat, nil
}

func (c Client) FSDelete(ctx context.Context, path string) (Result, error) {
	return c.runWithTimeout(ctx, c.fsTimeout, "fs", "--op", "delete", "--path", path)
}

func (c Client) FSCopy(ctx context.Context, src string, dst string) (Result, error) {
	return c.runWithTimeout(ctx, c.fsTimeout, "fs", "--op", "copy", "--src", src, "--dst", dst)
}

func (c Client) FSMove(ctx context.Context, src string, dst string) (Result, error) {
	return c.runWithTimeout(ctx, c.fsTimeout, "fs", "--op", "move", "--src", src, "--dst", dst)
}

func (c Client) FSWrite(ctx context.Context, path string, body io.Reader, overwrite bool) (Result, error) {
	overwriteValue := "0"
	if overwrite {
		overwriteValue = "1"
	}
	return c.runWithInput(ctx, c.fsTimeout, body, "fs", "--op", "write", "--path", path, "--overwrite", overwriteValue)
}

func (c Client) FSRead(ctx context.Context, path string) (FSReadStream, error) {
	ctx, cancel := context.WithTimeout(ctx, c.fsTimeout)
	cmd := exec.CommandContext(ctx, c.binaryPath, "fs", "--op", "read", "--path", path)
	stderr := &bytes.Buffer{}
	cmd.Stderr = stderr
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		cancel()
		return FSReadStream{}, err
	}
	if err := cmd.Start(); err != nil {
		cancel()
		return FSReadStream{}, err
	}
	wait := func() error {
		defer cancel()
		err := cmd.Wait()
		if ctx.Err() != nil {
			return fmt.Errorf("file read timeout")
		}
		if err != nil {
			return fmt.Errorf("file read failed: %s", stderr.String())
		}
		return nil
	}
	return FSReadStream{Body: stdout, Wait: wait}, nil
}

// ==================== 健康检查命令 ====================
// 作用：确认 C 控制核心可执行，并返回它声明的底层能力。
// ====================================================
func (c Client) Health(ctx context.Context) (Result, error) {
	return c.run(ctx, "health")
}

// ==================== 底盘运动命令 ====================
// 作用：把 vx/vy/wz 交给 C 层生成下位机底盘速度协议帧。
// ====================================================
func (c Client) MoveChassis(ctx context.Context, command ChassisCommand) (Result, error) {
	return c.run(ctx, "chassis", "--vx", formatFloat(command.VXMps), "--vy", formatFloat(command.VYMps), "--wz", formatFloat(command.WZRadps))
}

// ==================== 停止命令 ====================
// 作用：统一调用 C 层急停/停止协议帧。
// ================================================
func (c Client) Stop(ctx context.Context) (Result, error) {
	return c.run(ctx, "stop")
}

// ==================== 通用 C 核心调用 ====================
// 作用：执行 C 控制核心并解析 JSON，所有模块专属 API 都复用这里。
// ======================================================
func (c Client) run(ctx context.Context, args ...string) (Result, error) {
	return c.runWithTimeout(ctx, c.timeout, args...)
}

func (c Client) runWithTimeout(ctx context.Context, timeout time.Duration, args ...string) (Result, error) {
	return c.runWithInput(ctx, timeout, nil, args...)
}

func (c Client) runWithInput(ctx context.Context, timeout time.Duration, input io.Reader, args ...string) (Result, error) {
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	cmd := exec.CommandContext(ctx, c.binaryPath, args...)
	if input != nil {
		cmd.Stdin = input
	}
	output, err := cmd.CombinedOutput()
	if ctx.Err() != nil {
		return nil, fmt.Errorf("control core timeout")
	}
	if err != nil {
		return nil, fmt.Errorf("control core failed: %s", string(output))
	}
	var result Result
	if err := json.Unmarshal(output, &result); err != nil {
		return nil, fmt.Errorf("parse control core response: %w", err)
	}
	return result, nil
}

func formatFloat(value float64) string {
	return strconv.FormatFloat(value, 'f', 6, 64)
}
