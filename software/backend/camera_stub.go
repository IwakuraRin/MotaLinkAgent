//go:build !linux

package main

import "net/http"

func handleCameraMJPEG(store *persistedSettings, fallbackDevice string) http.HandlerFunc {
	_ = store
	_ = fallbackDevice
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			w.WriteHeader(http.StatusMethodNotAllowed)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusNotImplemented)
		_, _ = w.Write([]byte(`{"error":"host camera stream is only available on Linux (V4L2 + ffmpeg)"}`))
	}
}
