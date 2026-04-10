//go:build !windows

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"sync"

	"github.com/creack/pty"
	"github.com/gorilla/websocket"
)

// tryStartShellPTY starts an interactive shell under a PTY. Ubuntu's /bin/sh (dash) does not
// support -l, so we fall back through several argv patterns.
func tryStartShellPTY() (*os.File, *exec.Cmd, error) {
	shell := os.Getenv("SHELL")
	if shell == "" {
		shell = "/bin/bash"
	}
	baseEnv := append(os.Environ(), "TERM=xterm-256color", "COLORTERM=truecolor")

	try := func(name string, args ...string) (*os.File, *exec.Cmd, error) {
		c := exec.Command(name, args...)
		c.Env = baseEnv
		f, err := pty.Start(c)
		if err != nil {
			return nil, nil, err
		}
		return f, c, nil
	}

	var lastErr error
	// User's login shell
	for _, args := range [][]string{{"-l"}, {"-i"}, {}} {
		var f *os.File
		var c *exec.Cmd
		var err error
		if len(args) == 0 {
			f, c, err = try(shell)
		} else {
			f, c, err = try(shell, args[0])
		}
		if err == nil {
			return f, c, nil
		}
		lastErr = err
	}

	// Common fallbacks (no login files, but interactive)
	for _, spec := range []struct {
		name string
		args []string
	}{
		{"/bin/bash", []string{"-l"}},
		{"/bin/bash", nil},
		{"/bin/sh", nil},
	} {
		var f *os.File
		var c *exec.Cmd
		var err error
		if len(spec.args) == 0 {
			f, c, err = try(spec.name)
		} else {
			f, c, err = try(spec.name, spec.args...)
		}
		if err == nil {
			return f, c, nil
		}
		lastErr = err
	}

	return nil, nil, fmt.Errorf("pty shell: %w", lastErr)
}

func handleShellWS(w http.ResponseWriter, r *http.Request) {
	c, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Println("ws/shell upgrade:", err)
		return
	}
	defer func() { _ = c.Close() }()

	ptmx, cmd, err := tryStartShellPTY()
	if err != nil {
		log.Println("pty start:", err)
		_ = c.WriteMessage(websocket.TextMessage, []byte("\r\n\x1b[31m[host] failed to start shell: "+err.Error()+"\x1b[0m\r\n"))
		return
	}
	defer func() {
		_ = ptmx.Close()
		if cmd.Process != nil {
			_ = cmd.Process.Kill()
			_, _ = cmd.Process.Wait()
		}
	}()

	var writeMu sync.Mutex
	write := func(messageType int, data []byte) error {
		writeMu.Lock()
		defer writeMu.Unlock()
		return c.WriteMessage(messageType, data)
	}

	done := make(chan struct{})
	go func() {
		defer close(done)
		buf := make([]byte, 32<<10)
		for {
			n, rerr := ptmx.Read(buf)
			if n > 0 {
				if err := write(websocket.BinaryMessage, buf[:n]); err != nil {
					return
				}
			}
			if rerr != nil {
				if rerr != io.EOF {
					log.Println("pty read:", rerr)
				}
				return
			}
		}
	}()

	for {
		messageType, data, err := c.ReadMessage()
		if err != nil {
			break
		}
		if messageType == websocket.TextMessage {
			var p struct {
				Type string `json:"type"`
				Cols int    `json:"cols"`
				Rows int    `json:"rows"`
			}
			if json.Unmarshal(data, &p) == nil && p.Type == "resize" && p.Cols > 0 && p.Rows > 0 && p.Cols < 512 && p.Rows < 256 {
				_ = pty.Setsize(ptmx, &pty.Winsize{Rows: uint16(p.Rows), Cols: uint16(p.Cols)})
			}
			continue
		}
		if messageType == websocket.BinaryMessage && len(data) > 0 {
			if _, werr := ptmx.Write(data); werr != nil {
				break
			}
		}
	}

	<-done
}
