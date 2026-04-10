//go:build !linux

// 展示代码结构：
//   · 非 Linux：返回空串口列表（类型定义与 linux 版一致以便编译）
//
package main

type serialDeviceEntry struct {
	Path   string `json:"path"`
	Target string `json:"target"`
	Kind   string `json:"kind"`
}

//--------//
// 模块：串口枚举桩 — 非 Linux 空实现
func listSerialDevicesForAPI() []serialDeviceEntry {
	return nil
}
