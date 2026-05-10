// 作用：提供受限的只读目录列表能力，供前端文件浏览器使用。
package files

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
)

// ==================== 文件条目 ====================
// 作用：描述目录中的文件或子目录。
// ================================================
type Entry struct {
	Name    string `json:"name"`
	Path    string `json:"path"`
	IsDir   bool   `json:"is_dir"`
	Size    int64  `json:"size"`
	Mode    string `json:"mode"`
	ModTime string `json:"mod_time"`
}

// ==================== 目录枚举 ====================
// 作用：只允许绝对路径，返回目录下一级文件列表。
// ================================================
func List(path string) ([]Entry, error) {
	if !filepath.IsAbs(path) {
		return nil, fmt.Errorf("path must be absolute")
	}
	info, err := os.Stat(path)
	if err != nil {
		return nil, err
	}
	if !info.IsDir() {
		return nil, fmt.Errorf("not a directory")
	}
	items, err := os.ReadDir(path)
	if err != nil {
		return nil, err
	}
	entries := make([]Entry, 0, len(items))
	for _, item := range items {
		fullPath := filepath.Join(path, item.Name())
		info, err := item.Info()
		if err != nil {
			continue
		}
		entries = append(entries, Entry{
			Name:    item.Name(),
			Path:    fullPath,
			IsDir:   item.IsDir(),
			Size:    info.Size(),
			Mode:    info.Mode().String(),
			ModTime: info.ModTime().Format("2006-01-02 15:04:05"),
		})
	}
	sort.Slice(entries, func(i, j int) bool {
		if entries[i].IsDir != entries[j].IsDir {
			return entries[i].IsDir
		}
		return entries[i].Name < entries[j].Name
	})
	return entries, nil
}
