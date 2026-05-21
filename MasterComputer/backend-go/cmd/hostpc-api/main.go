// 作用：启动 HostPC Go API 层，加载账号文件并监听 HTTP 请求。
package main

import (
	"log"

	"omniroam/hostpc-api/internal/auth"
	"omniroam/hostpc-api/internal/config"
	"omniroam/hostpc-api/internal/httpapi"
)

// ==================== 程序入口 ====================
// 作用：解析配置、初始化鉴权存储，并启动 API 服务。
// ==================================================
func main() {
	cfg := config.Parse()
	authStore, err := auth.NewStore(cfg.Users)
	if err != nil {
		log.Fatalf("load auth store: %v", err)
	}
	server := httpapi.New(cfg, authStore)
	if err := httpapi.ListenAndServe(cfg, server.Handler()); err != nil {
		log.Fatal(err)
	}
}
