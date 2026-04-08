// OmniRoam HostPC — HTTP static + WebSocket log/control hub for LAN access.
// Run from repo: go run . -addr 0.0.0.0:8080 -static ../web/dist
// (Build web first: cd ../web && pnpm install && pnpm build)
//
// Wails: this stack is browser-first for 局域网; you can embed the same `web/dist`
// in a Wails shell later (separate wails init) if you need a desktop window.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
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
	h.broadcastJSON(map[string]string{"type": "log", "line": line})
}

func main() {
	addr := flag.String("addr", "0.0.0.0:8080", "listen address")
	staticDir := flag.String("static", "../web/dist", "path to Vue build output (pnpm build)")
	flag.Parse()

	st, err := os.Stat(*staticDir)
	if err != nil || !st.IsDir() {
		log.Fatalf("static dir not found: %s — run: cd web && pnpm install && pnpm build", *staticDir)
	}

	h := &hub{conns: make(map[*websocket.Conn]struct{})}

	// Demo + placeholder for ROS/serial: push synthetic lines periodically
	go func() {
		t := time.NewTicker(4 * time.Second)
		n := 0
		for range t.C {
			n++
			h.sendLog(fmt.Sprintf("INFO  [demo] host tick #%d — replace with `rostopic echo` / serial reader", n))
		}
	}()

	http.HandleFunc("/ws", func(w http.ResponseWriter, r *http.Request) {
		c, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			log.Println("ws upgrade:", err)
			return
		}
		h.add(c)
		h.sendLog("INFO  client connected from " + r.RemoteAddr)
		defer func() {
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
				h.sendLog(line)
				_ = c.WriteJSON(map[string]string{"type": "ack", "msg": "key " + p.Key + " " + state})
			}
		}
	})

	http.HandleFunc("/api/health", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"ok":true,"service":"omniroam-host"}`))
	})

	fileServer := http.FileServer(http.Dir(*staticDir))
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/ws" || strings.HasPrefix(r.URL.Path, "/api/") {
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
	log.Fatal(http.ListenAndServe(*addr, nil))
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
