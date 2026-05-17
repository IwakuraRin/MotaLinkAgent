// 作用：调用 AmseokBot C 控制核心，把 HTTP API 请求转换为本地低延迟控制命令。
package control

import (
	"context"
	"encoding/json"
	"fmt"
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
}

func NewClient(binaryPath string) Client {
	return Client{binaryPath: binaryPath, timeout: 2 * time.Second}
}

// ==================== 控制命令结构 ====================
// 作用：描述 Go API 接收的底盘、机械臂和 C 核心返回结果。
// ====================================================
type ChassisCommand struct {
	VXMps   float64 `json:"vx_mps"`
	VYMps   float64 `json:"vy_mps"`
	WZRadps float64 `json:"wz_radps"`
}

type ArmCommand struct {
	ShoulderYawDeg   float64 `json:"shoulder_yaw_deg"`
	ShoulderPitchDeg float64 `json:"shoulder_pitch_deg"`
	ElbowDeg         float64 `json:"elbow_deg"`
	WristDeg         float64 `json:"wrist_deg"`
}

type Result map[string]any

// ==================== 健康检查命令 ====================
// 作用：确认 C 控制核心可执行，并返回它声明的底层能力。
// ====================================================
func (c Client) Health(ctx context.Context) (Result, error) {
	return c.run(ctx, "health")
}

// ==================== 底盘运动命令 ====================
// 作用：把 vx/vy/wz 交给 C 层计算三全向轮轮速和串口协议帧。
// ====================================================
func (c Client) MoveChassis(ctx context.Context, command ChassisCommand) (Result, error) {
	return c.run(ctx, "chassis", "--vx", formatFloat(command.VXMps), "--vy", formatFloat(command.VYMps), "--wz", formatFloat(command.WZRadps))
}

// ==================== 机械臂关节命令 ====================
// 作用：把肩部、肘部和腕部目标角度交给 C 层限幅并组包。
// ======================================================
func (c Client) MoveArm(ctx context.Context, command ArmCommand) (Result, error) {
	return c.run(ctx, "arm", "--shoulder-yaw", formatFloat(command.ShoulderYawDeg), "--shoulder-pitch", formatFloat(command.ShoulderPitchDeg), "--elbow", formatFloat(command.ElbowDeg), "--wrist", formatFloat(command.WristDeg))
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
	ctx, cancel := context.WithTimeout(ctx, c.timeout)
	defer cancel()

	cmd := exec.CommandContext(ctx, c.binaryPath, args...)
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
