//go:build linux

package main

import (
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"strings"
)

// handleCameraMJPEG streams V4L2 capture via ffmpeg (multipart MJPEG). Requires ffmpeg on PATH.
func handleCameraMJPEG(store *persistedSettings, fallbackDevice string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			w.WriteHeader(http.StatusMethodNotAllowed)
			return
		}
		dev := sanitizeV4L2Path(store.cameraDeviceLocked(), fallbackDevice)
		if _, err := exec.LookPath("ffmpeg"); err != nil {
			http.Error(w, `{"error":"ffmpeg not found on PATH"}`, http.StatusServiceUnavailable)
			return
		}
		ctx := r.Context()
		args := []string{
			"-hide_banner",
			"-loglevel", "warning",
			"-nostdin",
			"-f", "v4l2",
			"-thread_queue_size", "512",
			"-i", dev,
			"-vf", "fps=15,scale='min(1280,iw)':-2,format=yuvj420p",
			"-c:v", "mjpeg",
			"-q:v", "4",
			"-f", "mpjpeg",
			"-",
		}
		cmd := exec.CommandContext(ctx, "ffmpeg", args...)
		stdout, err := cmd.StdoutPipe()
		if err != nil {
			log.Println("camera ffmpeg pipe:", err)
			http.Error(w, `{"error":"camera pipe"}`, http.StatusInternalServerError)
			return
		}
		cmd.Stderr = os.Stderr
		if err := cmd.Start(); err != nil {
			log.Println("camera ffmpeg start:", err)
			http.Error(w, `{"error":"camera start"}`, http.StatusInternalServerError)
			return
		}
		defer func() {
			_ = cmd.Process.Kill()
			_ = cmd.Wait()
		}()

		w.Header().Set("Content-Type", "multipart/x-mixed-replace; boundary=ffmpeg")
		w.Header().Set("Cache-Control", "no-cache, no-store, must-revalidate")
		w.Header().Set("Pragma", "no-cache")
		w.Header().Set("X-Accel-Buffering", "no")

		fl, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, `{"error":"streaming unsupported"}`, http.StatusInternalServerError)
			return
		}
		fl.Flush()

		_, copyErr := io.Copy(w, stdout)
		if copyErr != nil && ctx.Err() == nil {
			log.Println("camera stream copy:", copyErr)
		}
	}
}

func sanitizeV4L2Path(p, fallback string) string {
	p = strings.TrimSpace(p)
	if p == "" {
		return fallback
	}
	if !strings.HasPrefix(p, "/dev/") || strings.Contains(p, "..") {
		return fallback
	}
	return p
}

func (s *persistedSettings) cameraDeviceLocked() string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return strings.TrimSpace(s.CameraDevice)
}
