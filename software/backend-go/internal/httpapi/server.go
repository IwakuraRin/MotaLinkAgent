// 作用：实现 HostPC Go API 路由、JSON 响应、静态前端托管和 WebSocket 占位通道。
package httpapi

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"omniroam/hostpc-api/internal/auth"
	"omniroam/hostpc-api/internal/config"
	"omniroam/hostpc-api/internal/files"
	"omniroam/hostpc-api/internal/serial"
)

// ==================== 服务对象 ====================
// 作用：组合配置和鉴权模块，并注册全部 HTTP 路由。
// ==================================================
type Server struct {
	cfg  config.Config
	auth *auth.Store
	mux  *http.ServeMux
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
	s.mux.HandleFunc("/api/fs/list", s.handleFSList)
	s.mux.HandleFunc("/api/updates/status", s.handleUpdatesStatus)
	s.mux.HandleFunc("/api/updates/apply", s.handleUpdatesApply)
	s.mux.HandleFunc("/ws", s.handleMainWebSocket)
	s.mux.HandleFunc("/ws/shell", s.handleNotImplementedWebSocket)
	s.mux.HandleFunc("/ws/vnc", s.handleNotImplementedWebSocket)
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
// 作用：提供串口枚举和只读文件列表。
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
		"service":      "hostpc-go-api",
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
	writeJSON(w, http.StatusOK, map[string]any{"available": false, "message": "Go API layer is running"})
}

func (s *Server) handleUpdatesApply(w http.ResponseWriter, r *http.Request) {
	if !method(w, r, http.MethodPost) {
		return
	}
	if _, ok := s.requireAuth(w, r); !ok {
		return
	}
	jsonError(w, http.StatusBadRequest, "no update available")
}

// ==================== WebSocket 占位接口 ====================
// 作用：保留路径，主通道返回 501，后续接 C 控制核心事件流。
// ==========================================================
func (s *Server) handleMainWebSocket(w http.ResponseWriter, r *http.Request) {
	jsonError(w, http.StatusNotImplemented, "websocket bridge is not connected yet")
}

func (s *Server) handleNotImplementedWebSocket(w http.ResponseWriter, r *http.Request) {
	jsonError(w, http.StatusNotImplemented, "websocket endpoint is not implemented in Go API layer")
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

func ListenAndServe(cfg config.Config, handler http.Handler) error {
	fmt.Printf("OmniRoam Go API listening on %s static=%s control=%s\n", cfg.Addr, cfg.StaticDir, cfg.ControlCore)
	return http.ListenAndServe(cfg.Addr, handler)
}
