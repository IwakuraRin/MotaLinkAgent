// 展示代码结构：
//   · openMySQLFromEnvOrFlag：生产 MySQL 连接池
//   · mysqlHealth：/api/health 中返回连接状态摘要
//
package main

import (
	"context"
	"database/sql"
	"log"
	"os"
	"strings"
	"time"

	_ "github.com/go-sql-driver/mysql"
)

//--------//
// 模块：MySQL 连接
// openMySQLFromEnvOrFlag returns a pooled *sql.DB when -mysql-dsn or MYSQL_DSN is set.
// Empty DSN means MySQL is disabled (embedded static UI still works).
func openMySQLFromEnvOrFlag(flagDSN string) *sql.DB {
	dsn := strings.TrimSpace(flagDSN)
	if dsn == "" {
		dsn = strings.TrimSpace(os.Getenv("MYSQL_DSN"))
	}
	if dsn == "" {
		return nil
	}
	db, err := sql.Open("mysql", dsn)
	if err != nil {
		log.Fatalf("mysql open: %v", err)
	}
	db.SetMaxOpenConns(8)
	db.SetMaxIdleConns(4)
	db.SetConnMaxLifetime(5 * time.Minute)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := db.PingContext(ctx); err != nil {
		log.Fatalf("mysql ping: %v", err)
	}
	log.Println("mysql: connected")
	return db
}

//--------//
// 模块：健康检查 — Ping MySQL
func mysqlHealth(db *sql.DB) map[string]any {
	if db == nil {
		return map[string]any{"enabled": false}
	}
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	if err := db.PingContext(ctx); err != nil {
		return map[string]any{"enabled": true, "ok": false, "error": err.Error()}
	}
	return map[string]any{"enabled": true, "ok": true}
}
