// 作用：实现 HostPC Go API 路由、JSON 响应、静态前端托管和 WebSocket 通道。
package httpapi

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/creack/pty"
	"github.com/gorilla/websocket"

	"omniroam/hostpc-api/internal/auth"
	"omniroam/hostpc-api/internal/config"
	"omniroam/hostpc-api/internal/files"
	"omniroam/hostpc-api/internal/serial"
)

// ==================== 服务对象 ====================
// 作用：组合配置和鉴权模块，并注册全部 HTTP 路由。
// ==================================================
type Server struct {
	cfg        config.Config
	auth       *auth.Store
	mux        *http.ServeMux
	updateMu   sync.Mutex
	updateBusy bool
}

func New(cfg config.Config, authStore *auth.Store) *Server {
	server := &Server{cfg: cfg, auth: authStore, mux: http.NewServeMux()}
	server.routes()
	return server
}

func (s *Server) Handler() http.Handler {
	return s.mux
}

func (s *Server) routes() {
	s.mux.HandleFunc("/api/health", s.handleHealth)
	s.mux.HandleFunc("/api/auth/login", s.handleLogin)
	s.mux.HandleFunc("/api/auth/logout", s.handleLogout)
	s.mux.HandleFunc("/api/auth/me", s.handleMe)
	s.mux.HandleFunc("/api/auth/change-password", s.handleChangePassword)
	s.mux.HandleFunc("/api/settings", s.handleSettings)
	s.mux.HandleFunc("/api/serial/devices", s.handleSerialDevices)
	s.mux.HandleFunc("/api/control/health", s.handleControlHealth)
	s.mux.HandleFunc("/api/control/chassis/move", s.handleControlChassisMove)
	s.mux.HandleFunc("/api/control/stop", s.handleControlStop)
	s.mux.HandleFunc("/api/fs/list", s.handleFSList)
	s.mux.HandleFunc("/api/fs/upload", s.handleFSUpload)
	s.mux.HandleFunc("/api/fs/download", s.handleFSDownload)
	s.mux.HandleFunc("/api/fs/delete", s.handleFSDelete)
	s.mux.HandleFunc("/api/fs/copy", s.handleFSCopy)
	s.mux.HandleFunc("/api/fs/move", s.handleFSMove)
	s.mux.HandleFunc("/api/updates/status", s.handleUpdatesStatus)
	s.mux.HandleFunc("/api/updates/apply", s.handleUpdatesApply)
	s.mux.HandleFunc("/ws", s.handleMainWebSocket)
	s.mux.HandleFunc("/ws/shell", s.handleShellWebSocket)
	s.mux.HandleFunc("/ws/vnc", s.handleVNCWebSocket)
	s.mux.HandleFunc("/", s.handleStatic)
}

// ==================== 鉴权接口 ====================
// 作用：保持原前端登录、登出、查询用户和改密路径不变。
// ==================================================
func (s *Server) handleLogin(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodPost) {
		return
	}
	var req struct {
		Username string `json:"username"`
		Password string `json:"password"`
	}
	if err := readJSON(r, &req); err != nil {
		jsonError(w, http.StatusBadRequest, "missing credentials")
		return
	}
	user, ok := s.auth.Login(w, strings.TrimSpace(req.Username), req.Password)
	if !ok {
		jsonError(w, http.StatusUnauthorized, "invalid username or password")
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "username": user.Username, "must_change_password": user.MustChangePassword})
}

func (s *Server) handleLogout(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodPost) {
		return
	}
	s.auth.Logout(w, r)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

func (s *Server) handleMe(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodGet) {
		return
	}
	user, ok := s.requireAuth(w, r)
	if !ok {
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "username": user.Username, "must_change_password": user.MustChangePassword})
}

func (s *Server) handleChangePassword(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodPost) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	var req struct {
		NewPassword        string `json:"new_password"`
		NewPasswordConfirm string `json:"new_password_confirm"`
	}
	if err := readJSON(r, &req); err != nil || req.NewPassword == "" {
		jsonError(w, http.StatusBadRequest, "missing password")
		return
	}
	if req.NewPassword != req.NewPasswordConfirm {
		jsonError(w, http.StatusBadRequest, "passwords do not match")
		return
	}
	if len(req.NewPassword) < 8 {
		jsonError(w, http.StatusBadRequest, "password too short")
		return
	}
	if err := s.auth.ChangePassword(req.NewPassword); err != nil {
		jsonError(w, http.StatusInternalServerError, "server")
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

// ==================== 设置接口 ====================
// 作用：读写前端共享配置 JSON，路径和行为兼容旧后端。
// ==================================================
func (s *Server) handleSettings(w http.ResponseWriter, r *http.Request) {
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	switch r.Method {
	case http.MethodGet:
		body, err := os.ReadFile(s.cfg.Settings)
		if errors.Is(err, os.ErrNotExist) {
			writeJSON(w, http.StatusOK, map[string]any{"camera_url": "", "serial_roles": map[string]any{}})
			return
		}
		if err != nil {
			jsonError(w, http.StatusInternalServerError, "read")
			return
		}
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write(body)
	case http.MethodPost:
		body, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 2*1024*1024))
		if err != nil || !json.Valid(body) {
			jsonError(w, http.StatusBadRequest, "invalid json")
			return
		}
		if err := os.WriteFile(s.cfg.Settings, body, 0600); err != nil {
			jsonError(w, http.StatusInternalServerError, "write")
			return
		}
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write(body)
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	}
}

// ==================== 设备与文件接口 ====================
// 作用：提供串口枚举和文件列表和文件操作入口。
// ======================================================
func (s *Server) handleSerialDevices(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodGet) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"devices": serial.ListDevices()})
}

func (s *Server) handleFSList(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodGet) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	path := r.URL.Query().Get("path")
	if path == "" {
		path = "/"
	}
	entries, err := files.List(path)
	if err != nil {
		jsonError(w, http.StatusBadRequest, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"path": path, "entries": entries})
}

// ==================== 更新与健康检查 ====================
// 作用：保留前端兼容接口，并暴露 C 控制核心是否存在。
// ========================================================
func (s *Server) handleHealth(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodGet) {
		return
	}
	_, controlErr := os.Stat(s.cfg.ControlCore)
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":           true,
		"service":      "amseokbot-go-api",
		"control_core": s.cfg.ControlCore,
		"control_ok":   controlErr == nil,
		"time":         time.Now().Format(time.RFC3339),
	})
}

func (s *Server) handleUpdatesStatus(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodGet) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	writeJSON(w, http.StatusOK, s.buildUpdateStatus(r.Context()))
}

func (s *Server) handleUpdatesApply(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodPost) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}

	s.updateMu.Lock()
	if s.updateBusy {
		s.updateMu.Unlock()
		writeJSON(w, http.StatusConflict, map[string]any{"ok": false, "error": "update busy"})
		return
	}
	s.updateBusy = true
	s.updateMu.Unlock()
	defer func() {
		s.updateMu.Lock()
		s.updateBusy = false
		s.updateMu.Unlock()
	}()

	if _, err := os.Stat(s.cfg.UpdateScript); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"ok": false, "error": "update script unavailable", "output": err.Error()})
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 10*time.Minute)
	defer cancel()
	cmd := exec.CommandContext(ctx, "bash", s.cfg.UpdateScript)
	cmd.Dir = s.cfg.RepoRoot
	cmd.Env = append(os.Environ(), "AMSEOKBOT_REPO_DIR="+s.cfg.RepoRoot)
	output, err := cmd.CombinedOutput()
	exitCode := 0
	if err != nil {
		exitCode = 1
		if exitErr, ok := err.(*exec.ExitError); ok {
			exitCode = exitErr.ExitCode()
		}
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": err == nil, "exit_code": exitCode, "output": string(output)})
}

func (s *Server) buildUpdateStatus(ctx context.Context) map[string]any {
	status := map[string]any{"enabled": false, "update_available": false}
	if s.cfg.RepoRoot == "" {
		status["reason"] = "repo root not configured"
		return status
	}
	if _, err := os.Stat(filepath.Join(s.cfg.RepoRoot, ".git")); err != nil {
		status["reason"] = "repo root is not a git checkout"
		status["git_error"] = err.Error()
		return status
	}
	status["enabled"] = true
	branch, err := s.gitOutput(ctx, "rev-parse", "--abbrev-ref", "HEAD")
	if err != nil {
		status["git_error"] = err.Error()
		return status
	}
	branch = strings.TrimSpace(branch)
	status["branch"] = branch
	localSHA, err := s.gitOutput(ctx, "rev-parse", "HEAD")
	if err != nil {
		status["git_error"] = err.Error()
		return status
	}
	localSHA = strings.TrimSpace(localSHA)
	status["local_sha"] = localSHA
	if _, err := s.gitOutput(ctx, "fetch", "origin", branch); err != nil {
		status["git_error"] = err.Error()
		return status
	}
	remoteSHA, err := s.gitOutput(ctx, "rev-parse", "origin/"+branch)
	if err != nil {
		status["git_error"] = err.Error()
		return status
	}
	remoteSHA = strings.TrimSpace(remoteSHA)
	status["remote_sha"] = remoteSHA
	status["update_available"] = localSHA != remoteSHA
	if body, err := os.ReadFile(filepath.Join(s.cfg.RepoRoot, "CHANGELOG.md")); err == nil {
		status["changelog"] = string(body)
		status["changelog_ok"] = true
	} else {
		status["changelog_error"] = err.Error()
	}
	return status
}

func (s *Server) gitOutput(ctx context.Context, args ...string) (string, error) {
	cmdCtx, cancel := context.WithTimeout(ctx, 30*time.Second)
	defer cancel()
	cmd := exec.CommandContext(cmdCtx, "git", args...)
	cmd.Dir = s.cfg.RepoRoot
	output, err := cmd.CombinedOutput()
	if err != nil {
		return string(output), fmt.Errorf("git %s: %s", strings.Join(args, " "), strings.TrimSpace(string(output)))
	}
	return string(output), nil
}

// ==================== WebSocket 接口 ====================
// 作用：提供主日志通道、真实 Host PTY 和 noVNC TCP 转发。
// ======================================================
func (s *Server) handleMainWebSocket(w http.ResponseWriter, r *http.Request) {
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	conn, err := wsUpgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	defer conn.Close()
	_ = conn.WriteJSON(map[string]any{"type": "log", "line": "INFO  Go HostPC WebSocket connected", "edge": "e_ws"})
	keysHeld := map[string]bool{}
	defer func() {
		_ = s.writeAtmegaLine("CHASSIS 0.000000 0.000000 0.000000")
	}()
	for {
		var msg map[string]any
		if err := conn.ReadJSON(&msg); err != nil {
			return
		}
		if msg["type"] == "key" {
			key, _ := msg["key"].(string)
			down, _ := msg["down"].(bool)
			if key != "w" && key != "a" && key != "s" && key != "d" && key != "q" && key != "e" {
				_ = conn.WriteJSON(map[string]any{"type": "ack", "msg": "ignored key", "edge": "e_ws"})
				continue
			}
			if down {
				keysHeld[key] = true
			} else {
				delete(keysHeld, key)
			}
			frame := chassisFrameFromKeys(keysHeld)
			if err := s.writeAtmegaLine(frame); err != nil {
				_ = conn.WriteJSON(map[string]any{"type": "log", "line": "ERR  ATmega UART write failed: " + err.Error(), "edge": "e_ws"})
				continue
			}
			_ = conn.WriteJSON(map[string]any{"type": "ack", "msg": frame, "edge": "e_ws"})
		}
	}
}

func chassisFrameFromKeys(keysHeld map[string]bool) string {
	const linearMps = 0.25
	const angularRadps = 0.90

	vx := 0.0
	vy := 0.0
	wz := 0.0
	if keysHeld["w"] {
		vx += linearMps
	}
	if keysHeld["s"] {
		vx -= linearMps
	}
	if keysHeld["a"] {
		vy += linearMps
	}
	if keysHeld["d"] {
		vy -= linearMps
	}
	if keysHeld["q"] {
		wz += angularRadps
	}
	if keysHeld["e"] {
		wz -= angularRadps
	}
	return fmt.Sprintf("CHASSIS %.6f %.6f %.6f", vx, vy, wz)
}

func (s *Server) writeAtmegaLine(line string) error {
	port, err := s.atmegaUARTPort()
	if err != nil {
		return err
	}
	if err := configureSerialPort(port); err != nil {
		return err
	}
	file, err := os.OpenFile(port, os.O_WRONLY|syscall.O_NOCTTY, 0)
	if err != nil {
		return err
	}
	defer file.Close()
	if !strings.HasSuffix(line, "\n") {
		line += "\n"
	}
	written, err := file.WriteString(line)
	if err != nil {
		return err
	}
	if written != len(line) {
		return fmt.Errorf("short write %d/%d", written, len(line))
	}
	return nil
}

func (s *Server) atmegaUARTPort() (string, error) {
	body, err := os.ReadFile(s.cfg.Settings)
	if err != nil {
		return "", fmt.Errorf("read settings: %w", err)
	}
	var settings struct {
		SerialRoles map[string]string `json:"serial_roles"`
	}
	if err := json.Unmarshal(body, &settings); err != nil {
		return "", fmt.Errorf("parse settings: %w", err)
	}
	port := strings.TrimSpace(settings.SerialRoles["atmega_uart"])
	if port == "" {
		port = strings.TrimSpace(settings.SerialRoles["esp32_uart"])
	}
	if port == "" {
		return "", fmt.Errorf("serial_roles.atmega_uart is empty")
	}
	return port, nil
}

func configureSerialPort(port string) error {
	cmd := exec.Command("stty", "-F", port, "115200", "raw", "-echo", "-icanon", "min", "0", "time", "1")
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("stty %s: %s", port, strings.TrimSpace(string(output)))
	}
	return nil
}

func (s *Server) handleShellWebSocket(w http.ResponseWriter, r *http.Request) {
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	conn, err := wsUpgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	defer conn.Close()

	shell := os.Getenv("SHELL")
	if shell == "" {
		shell = "/bin/bash"
	}
	cmd := exec.Command(shell, "-l")
	cmd.Env = append(os.Environ(), "TERM=xterm-256color")
	tty, err := pty.Start(cmd)
	if err != nil {
		_ = conn.WriteMessage(websocket.BinaryMessage, []byte("\r\n[host] failed to start shell\r\n"))
		return
	}
	defer tty.Close()
	defer cmd.Process.Kill()
	defer cmd.Wait()

	_ = conn.WriteMessage(websocket.BinaryMessage, []byte("\r\n[host] Go shell connected\r\n"))
	done := make(chan struct{})
	go func() {
		defer close(done)
		buf := make([]byte, 8192)
		for {
			n, err := tty.Read(buf)
			if err != nil {
				return
			}
			if err := conn.WriteMessage(websocket.BinaryMessage, buf[:n]); err != nil {
				return
			}
		}
	}()
	for {
		select {
		case <-done:
			return
		default:
		}
		msgType, payload, err := conn.ReadMessage()
		if err != nil {
			return
		}
		if msgType == websocket.BinaryMessage || msgType == websocket.TextMessage {
			if _, err := tty.Write(payload); err != nil {
				return
			}
		}
	}
}

func (s *Server) handleVNCWebSocket(w http.ResponseWriter, r *http.Request) {
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	conn, err := wsUpgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	defer conn.Close()

	tcp, err := net.Dial("tcp", "127.0.0.1:5900")
	if err != nil {
		_ = conn.WriteMessage(websocket.TextMessage, []byte(`{"error":"vnc target 127.0.0.1:5900 unavailable"}`))
		return
	}
	defer tcp.Close()

	done := make(chan struct{})
	go func() {
		defer close(done)
		buf := make([]byte, 32768)
		for {
			n, err := tcp.Read(buf)
			if err != nil {
				return
			}
			if err := conn.WriteMessage(websocket.BinaryMessage, buf[:n]); err != nil {
				return
			}
		}
	}()
	for {
		select {
		case <-done:
			return
		default:
		}
		msgType, payload, err := conn.ReadMessage()
		if err != nil {
			return
		}
		if msgType == websocket.BinaryMessage || msgType == websocket.TextMessage {
			if _, err := tcp.Write(payload); err != nil {
				return
			}
		}
	}
}

// ==================== 静态前端 ====================
// 作用：托管 Vite 构建产物，并为 SPA 路由回退到 index.html。
// ==================================================
func (s *Server) handleStatic(w http.ResponseWriter, r *http.Request) {
	if strings.HasPrefix(r.URL.Path, "/api/") {
		jsonError(w, http.StatusNotFound, "not found")
		return
	}
	cleanPath := filepath.Clean(strings.TrimPrefix(r.URL.Path, "/"))
	if cleanPath == "." {
		cleanPath = "index.html"
	}
	fullPath := filepath.Join(s.cfg.StaticDir, cleanPath)
	if !strings.HasPrefix(fullPath, filepath.Clean(s.cfg.StaticDir)) {
		http.NotFound(w, r)
		return
	}
	info, err := os.Stat(fullPath)
	if err != nil || info.IsDir() {
		fullPath = filepath.Join(s.cfg.StaticDir, "index.html")
	}
	http.ServeFile(w, r, fullPath)
}

// ==================== 通用 HTTP 工具 ====================
// 作用：统一 JSON 编码、鉴权检查和方法限制。
// ======================================================
func (s *Server) requireAuth(w http.ResponseWriter, r *http.Request) (auth.UserRecord, bool) {
	user, ok := s.auth.CurrentUser(r)
	if !ok {
		jsonError(w, http.StatusUnauthorized, "unauthorized")
		return auth.UserRecord{}, false
	}
	return user, true
}

func method(w http.ResponseWriter, r *http.Request, want string) bool {
	if r.Method == want {
		return true
	}
	http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
	return false
}

func readJSON(r *http.Request, out any) error {
	defer r.Body.Close()
	return json.NewDecoder(io.LimitReader(r.Body, 2*1024*1024)).Decode(out)
}

func writeJSON(w http.ResponseWriter, status int, body any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(body)
}

func jsonError(w http.ResponseWriter, status int, message string) {
	writeJSON(w, status, map[string]any{"error": message})
}

var wsUpgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

func ListenAndServe(cfg config.Config, handler http.Handler) error {
	fmt.Printf("AmseokBot Go API listening on %s static=%s control=%s\n", cfg.Addr, cfg.StaticDir, cfg.ControlCore)
	return http.ListenAndServe(cfg.Addr, handler)
}
