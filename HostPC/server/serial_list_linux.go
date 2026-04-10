//go:build linux

// 展示代码结构：
//   · listSerialDevicesForAPI：扫描 /dev/serial/by-id、ttyUSB、ttyACM 等并去重
//
package main

import (
	"os"
	"path/filepath"
	"sort"
)

type serialDeviceEntry struct {
	Path   string `json:"path"`
	Target string `json:"target"`
	Kind   string `json:"kind"`
}

//--------//
// 模块：串口枚举 — 供 /api/serial/devices
// listSerialDevicesForAPI enumerates TTY-style USB serial nodes. Prefer /dev/serial/by-id/*
// so the same physical cable keeps one path across ttyUSB0 vs ttyACM0 re-enumeration.
func listSerialDevicesForAPI() []serialDeviceEntry {
	seenTarget := make(map[string]struct{})
	var out []serialDeviceEntry

	add := func(path, target, kind string) {
		if path == "" {
			return
		}
		if _, ok := seenTarget[target]; ok {
			return
		}
		seenTarget[target] = struct{}{}
		out = append(out, serialDeviceEntry{Path: path, Target: filepath.Base(target), Kind: kind})
	}

	byID := "/dev/serial/by-id"
	if de, err := os.ReadDir(byID); err == nil {
		var names []string
		for _, e := range de {
			if e.IsDir() {
				continue
			}
			names = append(names, e.Name())
		}
		sort.Strings(names)
		for _, name := range names {
			p := filepath.Join(byID, name)
			tgt, err := filepath.EvalSymlinks(p)
			if err != nil {
				continue
			}
			if !filepath.IsAbs(tgt) {
				tgt = filepath.Join(byID, tgt)
				tgt, err = filepath.EvalSymlinks(tgt)
				if err != nil {
					continue
				}
			}
			add(p, tgt, "by-id")
		}
	}

	for _, pattern := range []string{"/dev/ttyUSB*", "/dev/ttyACM*"} {
		matches, _ := filepath.Glob(pattern)
		sort.Strings(matches)
		for _, p := range matches {
			tgt, err := filepath.EvalSymlinks(p)
			if err != nil {
				tgt = p
			}
			if _, ok := seenTarget[tgt]; ok {
				continue
			}
			seenTarget[tgt] = struct{}{}
			out = append(out, serialDeviceEntry{Path: p, Target: filepath.Base(tgt), Kind: "tty"})
		}
	}

	sort.Slice(out, func(i, j int) bool { return out[i].Path < out[j].Path })
	return out
}
