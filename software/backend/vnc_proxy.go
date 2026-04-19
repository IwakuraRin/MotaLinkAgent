// 模块：/ws/vnc — 已登录浏览器与本机 VNC（RFB/TCP）双向二进制转发，无需用户配置 WebSocket URL
//
package main

import (
	"log"
	"net"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

func handleVNCProxyWS(vncAddr string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		c, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			log.Println("ws/vnc upgrade:", err)
			return
		}

		tcpConn, err := net.DialTimeout("tcp", vncAddr, 8*time.Second)
		if err != nil {
			log.Println("ws/vnc dial", vncAddr, err)
			_ = c.Close()
			return
		}

		var closeOnce sync.Once
		cleanup := func() {
			closeOnce.Do(func() {
				_ = tcpConn.Close()
				_ = c.Close()
			})
		}
		defer cleanup()

		var wg sync.WaitGroup
		wg.Add(2)

		go func() {
			defer wg.Done()
			defer cleanup()
			for {
				mt, data, err := c.ReadMessage()
				if err != nil {
					return
				}
				if mt != websocket.BinaryMessage || len(data) == 0 {
					continue
				}
				if _, err := tcpConn.Write(data); err != nil {
					return
				}
			}
		}()

		go func() {
			defer wg.Done()
			defer cleanup()
			buf := make([]byte, 32768)
			for {
				n, err := tcpConn.Read(buf)
				if n > 0 {
					if werr := c.WriteMessage(websocket.BinaryMessage, buf[:n]); werr != nil {
						return
					}
				}
				if err != nil {
					return
				}
			}
		}()

		wg.Wait()
	}
}
