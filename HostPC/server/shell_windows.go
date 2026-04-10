//go:build windows

package main

import (
	"log"
	"net/http"

	"github.com/gorilla/websocket"
)

func handleShellWS(w http.ResponseWriter, r *http.Request) {
	c, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Println("ws/shell upgrade:", err)
		return
	}
	defer func() { _ = c.Close() }()
	msg := []byte("\r\n\x1b[31m[host] interactive shell is not available on Windows builds (PTY is Unix-only).\x1b[0m\r\n")
	_ = c.WriteMessage(websocket.TextMessage, msg)
}
