// OmniRoam HostPC — HTTP static + WebSocket log/control hub for LAN access.
// Run from repo: go run . -addr 0.0.0.0:8080 -static ../web/dist
// (Build web first: cd ../web && pnpm install && pnpm build)
//
// Wails: this stack is browser-first for 局域网; you can embed the same `web/dist`
// in a Wails shell later (separate wails init) if you need a desktop window.
package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	CheckOrigin: func(r *http.Request) bool { return true },
}

type hub struct {
	mu    sync.Mutex
	conns map[*websocket.Conn]struct{}
}

func (h *hub) add(c *websocket.Conn) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.conns[c] = struct{}{}
}

func (h *hub) remove(c *websocket.Conn) {
	h.mu.Lock()
	defer h.mu.Unlock()
	delete(h.conns, c)
}

func (h *hub) broadcastJSON(v any) {
	b, err := json.Marshal(v)
	if err != nil {
		return
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	for c := range h.conns {
		_ = c.WriteMessage(websocket.TextMessage, b)
	}
}

func (h *hub) sendLog(line string) {
	h.sendLogEdge(line, "")
}

// sendLogEdge broadcasts a log line; if edge is non-empty, clients route it to that topology link panel.
func (h *hub) sendLogEdge(line, edge string) {
	payload := map[string]any{"type": "log", "line": line}
	if edge != "" {
		payload["edge"] = edge
	}
	h.broadcastJSON(payload)
}

// persistedSettings is a JSON file so LAN clients share camera URL and serial role → device path bindings.
// ROS / scripts can read the same file (e.g. jq .serial_roles.esp32_uart hostpc-settings.json).
type persistedSettings struct {
	mu          sync.Mutex
	path        string
	CameraURL   string            `json:"camera_url"`
	SerialRoles map[string]string `json:"serial_roles"`
}

func (s *persistedSettings) load() {
	s.mu.Lock()
	defer s.mu.Unlock()
	b, err := os.ReadFile(s.path)
	if err != nil {
		s.SerialRoles = map[string]string{}
		return
	}
	var p persistedSettings
	if json.Unmarshal(b, &p) != nil {
		s.SerialRoles = map[string]string{}
		return
	}
	s.CameraURL = strings.TrimSpace(p.CameraURL)
	if p.SerialRoles != nil {
		s.SerialRoles = map[string]string{}
		for k, v := range p.SerialRoles {
			s.SerialRoles[k] = strings.TrimSpace(v)
		}
	} else {
		s.SerialRoles = map[string]string{}
	}
}

func (s *persistedSettings) snapshot() map[string]any {
	s.mu.Lock()
	defer s.mu.Unlock()
	roles := make(map[string]string)
	for k, v := range s.SerialRoles {
		roles[k] = v
	}
	return map[string]any{
		"camera_url":   s.CameraURL,
		"serial_roles": roles,
	}
}

var allowedSerialRoles = map[string]struct{}{
	"esp32_uart": {},
	"aux_serial": {},
}

func (s *persistedSettings) saveAll(cameraURL string, roles map[string]string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.CameraURL = strings.TrimSpace(cameraURL)
	s.SerialRoles = map[string]string{}
	if roles != nil {
		for k, v := range roles {
			if _, ok := allowedSerialRoles[k]; !ok {
				continue
			}
			vv := strings.TrimSpace(v)
			if vv == "" {
				continue
			}
			s.SerialRoles[k] = vv
		}
	}
	out, err := json.MarshalIndent(struct {
		CameraURL   string            `json:"camera_url"`
		SerialRoles map[string]string `json:"serial_roles"`
	}{CameraURL: s.CameraURL, SerialRoles: s.SerialRoles}, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(s.path, out, 0o600)
}

func main() {
	addr := flag.String("addr", "0.0.0.0:8080", "listen address")
	staticDir := flag.String("static", "../web/dist", "path to Vue build output (pnpm build)")
	settingsPath := flag.String("settings", "hostpc-settings.json", "JSON file for UI defaults (camera_url); created next to cwd when server runs")
	mysqlDSN := flag.String("mysql-dsn", "", "MySQL DSN (overrides env MYSQL_DSN); use with production MySQL")
	sqliteUsers := flag.String("sqlite-users", "", "SQLite file for web users (dev/LAN without MySQL); overrides HOSTPC_SQLITE_USERS env")
	authSecretPath := flag.String("auth-secret", "hostpc-auth-secret", "file holding 32-byte session signing key (created if missing); override with AUTH_SECRET env")
	gitRepo := flag.String("git-repo", "", "git repository root for /api/repo/* update checks (default: walk up from cwd for .git)")
	flag.Parse()

	hostGitRepoRoot = resolveGitRepoRoot(*gitRepo)
	if hostGitRepoRoot != "" {
		log.Printf("Git repo for updates: %s", hostGitRepoRoot)
	}

	st, err := os.Stat(*staticDir)
	if err != nil || !st.IsDir() {
		log.Fatalf("static dir not found: %s — run: cd web && pnpm install && pnpm build", *staticDir)
	}

	h := &hub{conns: make(map[*websocket.Conn]struct{})}
	store := &persistedSettings{path: *settingsPath}
	store.load()

	sqlitePath := strings.TrimSpace(*sqliteUsers)
	if sqlitePath == "" {
		sqlitePath = strings.TrimSpace(os.Getenv("HOSTPC_SQLITE_USERS"))
	}
	var db *sql.DB
	usersBackend := "mysql"
	switch {
	case sqlitePath != "":
		db = openSQLiteUsers(sqlitePath)
		usersBackend = "sqlite"
	default:
		db = openMySQLFromEnvOrFlag(*mysqlDSN)
	}
	if db == nil {
		log.Fatal("Set MYSQL_DSN (or -mysql-dsn) for MySQL, or pass -sqlite-users / HOSTPC_SQLITE_USERS for a local SQLite user database.")
	}
	defer func() { _ = db.Close() }()

	secret, err := loadOrCreateAuthSecret(*authSecretPath)
	if err != nil {
		log.Fatalf("auth secret: %v", err)
	}
	authCtx, cancelAuth := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancelAuth()
	if usersBackend == "sqlite" {
		if err := ensureHostUsersTableSQLite(authCtx, db); err != nil {
			log.Fatalf("sqlite schema host_users: %v", err)
		}
	} else {
		if err := ensureHostUsersTableMySQL(authCtx, db); err != nil {
			log.Fatalf("mysql schema host_users: %v", err)
		}
	}
	if err := ensureDefaultWebUser(authCtx, db); err != nil {
		log.Fatalf("default web user: %v", err)
	}
	ar := &authRuntime{db: db, secret: secret}

	mux := http.NewServeMux()

	// Demo + placeholder for ROS/serial: push synthetic lines periodically
	go func() {
		t := time.NewTicker(4 * time.Second)
		n := 0
		for range t.C {
			n++
			h.sendLogEdge(fmt.Sprintf("INFO  [demo] host tick #%d — replace with `rostopic echo` / serial reader", n), "e_ros_host")
		}
	}()

	mux.HandleFunc("/api/auth/login", ar.handleLogin)
	mux.HandleFunc("/api/auth/logout", ar.handleLogout)
	mux.HandleFunc("/api/auth/me", ar.requireAuth(ar.handleMe))
	mux.HandleFunc("/api/auth/change-password", ar.requireAuth(ar.handleChangePassword))

	mux.HandleFunc("/ws/shell", ar.requireAuthWS(handleShellWS))

	mux.HandleFunc("/ws", ar.requireAuthWS(func(w http.ResponseWriter, r *http.Request) {
		c, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			log.Println("ws upgrade:", err)
			return
		}
		h.add(c)
		h.sendLogEdge("INFO  client connected from "+r.RemoteAddr, "e_ws")
		defer func() {
			h.sendLogEdge("INFO  WebSocket client disconnected "+r.RemoteAddr, "e_ws")
			h.remove(c)
			_ = c.Close()
		}()
		for {
			_, msg, err := c.ReadMessage()
			if err != nil {
				return
			}
			var p struct {
				Type string `json:"type"`
				Key  string `json:"key"`
				Down bool   `json:"down"`
			}
			if json.Unmarshal(msg, &p) == nil && p.Type == "key" {
				state := "release"
				if p.Down {
					state = "press"
				}
				line := fmt.Sprintf("CTRL  key %s %s — (wire to chassis /cmd_vel or ESP32)", strings.ToUpper(p.Key), state)
				h.sendLogEdge(line, "e_ws")
				h.sendLogEdge(fmt.Sprintf("PIPE  chassis/serial ← key %s %s", strings.ToUpper(p.Key), state), "e_serial")
				_ = c.WriteJSON(map[string]string{
					"type": "ack",
					"msg":  "key " + p.Key + " " + state,
					"edge": "e_ws",
				})
			}
		}
	}))

	mux.HandleFunc("/api/health", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		mh := mysqlHealth(db)
		mh["users_backend"] = usersBackend
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok":      true,
			"service": "omniroam-host",
			"mysql":   mh,
		})
	})

	mux.HandleFunc("/api/settings", ar.requireAuth(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		switch r.Method {
		case http.MethodGet:
			_ = json.NewEncoder(w).Encode(store.snapshot())
		case http.MethodPost:
			body, err := io.ReadAll(io.LimitReader(r.Body, 1<<20))
			if err != nil {
				http.Error(w, `{"error":"read"}`, http.StatusBadRequest)
				return
			}
			var in struct {
				CameraURL   string            `json:"camera_url"`
				SerialRoles map[string]string `json:"serial_roles"`
			}
			if json.Unmarshal(body, &in) != nil {
				http.Error(w, `{"error":"invalid json"}`, http.StatusBadRequest)
				return
			}
			if in.SerialRoles == nil {
				in.SerialRoles = map[string]string{}
			}
			if err := store.saveAll(in.CameraURL, in.SerialRoles); err != nil {
				log.Println("settings save:", err)
				http.Error(w, `{"error":"write"}`, http.StatusInternalServerError)
				return
			}
			h.sendLogEdge("INFO  POST /api/settings OK — camera_url + serial_roles saved", "e_http_api")
			h.sendLogEdge("INFO  hostpc-settings.json updated on disk", "e_file_settings")
			_ = json.NewEncoder(w).Encode(store.snapshot())
		default:
			w.WriteHeader(http.StatusMethodNotAllowed)
		}
	}))

	mux.HandleFunc("/api/fs/list", ar.requireAuth(handleFSList))

	mux.HandleFunc("/api/repo/status", ar.requireAuth(handleRepoStatus))
	mux.HandleFunc("/api/repo/pull", ar.requireAuth(handleRepoPull))

	mux.HandleFunc("/api/serial/devices", ar.requireAuth(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			w.WriteHeader(http.StatusMethodNotAllowed)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		devs := listSerialDevicesForAPI()
		_ = json.NewEncoder(w).Encode(map[string]any{
			"os":      runtime.GOOS,
			"devices": devs,
		})
	}))

	fileServer := http.FileServer(http.Dir(*staticDir))
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/ws" || r.URL.Path == "/ws/shell" || strings.HasPrefix(r.URL.Path, "/api/") {
			http.NotFound(w, r)
			return
		}
		rel := strings.TrimPrefix(r.URL.Path, "/")
		if rel == "" {
			http.ServeFile(w, r, filepath.Join(*staticDir, "index.html"))
			return
		}
		full := filepath.Join(*staticDir, filepath.Clean("/"+rel))
		if !strings.HasPrefix(full, filepath.Clean(*staticDir)) {
			http.NotFound(w, r)
			return
		}
		fi, err := os.Stat(full)
		if err != nil || fi.IsDir() {
			http.ServeFile(w, r, filepath.Join(*staticDir, "index.html"))
			return
		}
		fileServer.ServeHTTP(w, r)
	})

	log.Printf("OmniRoam HostPC listening on %s (static=%s)", *addr, *staticDir)
	logAccessURLs(*addr)
	log.Fatal(http.ListenAndServe(*addr, mux))
}

// logAccessURLs prints concrete http://IP:port/ hints for 0.0.0.0 / :: binds.
func logAccessURLs(listenAddr string) {
	host, port, err := net.SplitHostPort(listenAddr)
	if err != nil {
		log.Printf("  (could not parse -addr for URL hints: %v)", err)
		return
	}
	if host != "0.0.0.0" && host != "::" && host != "" {
		log.Printf("  http://%s:%s/", host, port)
		return
	}
	log.Print("  LAN / local URLs:")
	addrs, _ := net.InterfaceAddrs()
	for _, a := range addrs {
		ipNet, ok := a.(*net.IPNet)
		if !ok || ipNet.IP.IsLoopback() {
			continue
		}
		ip4 := ipNet.IP.To4()
		if ip4 == nil {
			continue
		}
		log.Printf("    http://%s:%s/", ip4.String(), port)
	}
	log.Printf("    http://127.0.0.1:%s/", port)
}
