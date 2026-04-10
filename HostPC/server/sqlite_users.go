// 展示代码结构：
//   · openSQLiteUsers：开发/LAN 无 MySQL 时的用户库
//   · ensureHostUsersTableSQLite：host_users 表（与 auth.go 中逻辑共用）
//
package main

import (
	"context"
	"database/sql"
	"log"
	"time"

	_ "modernc.org/sqlite"
)

//--------//
// 模块：连接 SQLite 用户库
// openSQLiteUsers opens (or creates) a SQLite file for host_users only (dev / LAN without MySQL).
func openSQLiteUsers(path string) *sql.DB {
	db, err := sql.Open("sqlite", path)
	if err != nil {
		log.Fatalf("sqlite open %s: %v", path, err)
	}
	db.SetMaxOpenConns(1)
	db.SetMaxIdleConns(1)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := db.PingContext(ctx); err != nil {
		log.Fatalf("sqlite ping: %v", err)
	}
	log.Printf("sqlite users: %s", path)
	return db
}

//--------//
// 模块：建表 — host_users（SQLite）
func ensureHostUsersTableSQLite(ctx context.Context, db *sql.DB) error {
	_, err := db.ExecContext(ctx, `CREATE TABLE IF NOT EXISTS host_users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  username TEXT NOT NULL UNIQUE,
  password_hash BLOB NOT NULL,
  must_change_password INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  updated_at TEXT NOT NULL DEFAULT (datetime('now'))
)`)
	return err
}
