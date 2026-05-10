// 作用：扫描 Linux 串口设备，为前端设备绑定面板提供候选列表。
package serial

import (
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// ==================== 串口数据结构 ====================
// 作用：描述一个可展示给前端的串口设备。
// ======================================================
type Device struct {
	Path   string `json:"path"`
	Target string `json:"target"`
	Kind   string `json:"kind"`
}

// ==================== 串口扫描 ====================
// 作用：优先读取 /dev/serial/by-id，再补充常见 ttyUSB/ttyACM 设备。
// ================================================
func ListDevices() []Device {
	devices := make([]Device, 0)
	seen := map[string]bool{}

	for _, path := range glob("/dev/serial/by-id/*") {
		target, _ := filepath.EvalSymlinks(path)
		addDevice(&devices, seen, path, target, "by-id")
	}
	for _, pattern := range []string{"/dev/ttyUSB*", "/dev/ttyACM*", "/dev/ttyAMA*", "/dev/ttyS*"} {
		for _, path := range glob(pattern) {
			addDevice(&devices, seen, path, path, "tty")
		}
	}

	sort.Slice(devices, func(i, j int) bool {
		return devices[i].Path < devices[j].Path
	})
	return devices
}

func addDevice(devices *[]Device, seen map[string]bool, path string, target string, kind string) {
	if path == "" || seen[path] {
		return
	}
	if _, err := os.Stat(path); err != nil {
		return
	}
	seen[path] = true
	if target == "" {
		target = path
	}
	*devices = append(*devices, Device{Path: path, Target: target, Kind: kind})
}

func glob(pattern string) []string {
	matches, err := filepath.Glob(pattern)
	if err != nil {
		return nil
	}
	out := make([]string, 0, len(matches))
	for _, item := range matches {
		if strings.TrimSpace(item) != "" {
			out = append(out, item)
		}
	}
	return out
}
