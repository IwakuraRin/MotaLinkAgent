//go:build !linux

package main

type serialDeviceEntry struct {
	Path   string `json:"path"`
	Target string `json:"target"`
	Kind   string `json:"kind"`
}

func listSerialDevicesForAPI() []serialDeviceEntry {
	return nil
}
