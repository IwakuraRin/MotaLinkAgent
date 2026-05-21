// 作用：解析 HostPC Go API 层启动参数，集中保存监听地址、静态目录、控制核心和更新路径。
package config

import (
	"flag"
	"os"
)

// ==================== 服务配置 ====================
// 作用：保存 Go API 层启动所需的路径和网络参数。
// ==================================================
type Config struct {
	Addr         string
	StaticDir    string
	Settings     string
	Users        string
	ControlCore  string
	RepoRoot     string
	UpdateScript string
}

// ==================== 参数解析 ====================
// 作用：从命令行读取路径配置，并提供与旧 C 后端兼容的默认值。
// ==================================================
func Parse() Config {
	cfg := Config{}
	flag.StringVar(&cfg.Addr, "addr", "0.0.0.0:8080", "HTTP listen address")
	flag.StringVar(&cfg.StaticDir, "static", "../frontend/dist", "frontend static directory")
	flag.StringVar(&cfg.Settings, "settings", "../backend/hostpc-settings.json", "settings JSON path")
	flag.StringVar(&cfg.Users, "users", "../backend/hostpc-users.cauth", "user auth file path")
	flag.StringVar(&cfg.ControlCore, "control-core", "../backend/amseokbot-control-core", "C control core binary path")
	flag.StringVar(&cfg.RepoRoot, "repo-root", getenvDefault("AMSEOKBOT_REPO_DIR", "."), "Git repository root used by software update checks")
	flag.StringVar(&cfg.UpdateScript, "update-script", getenvDefault("AMSEOKBOT_UPDATE_SCRIPT", "./scripts/update.sh"), "software update script path")
	flag.Parse()
	return cfg
}

// ==================== 环境变量工具 ====================
// 作用：允许 Shell 部署脚本通过环境变量覆盖默认路径。
// ====================================================
func getenvDefault(key string, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}
