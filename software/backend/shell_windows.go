//go:build windows

// 展示代码结构：
//   · Windows 构建无 PTY：/ws/shell 仅返回提示信息
//
package main

import (
	"log"
	"net/http"

	"github.com/gorilla/websocket"
)

//--------//
// 模块：WebSocket /ws/shell — 占位（不支持交互 shell）
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
