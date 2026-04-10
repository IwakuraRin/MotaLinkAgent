// 展示代码结构：
//   · 拓扑边 ID 常量（与 Go sendLogEdge 一致）· 空日志 map · 类型守卫
//
//--------//
// 模块：边 ID 列表 — 与后端日志路由键一致
/** IDs must match HostPC/server log routing (sendLogEdge). */
export const TOPOLOGY_EDGE_IDS = [
  'e_ws',
  'e_http_api',
  'e_file_settings',
  'e_ros_host',
  'e_serial',
  'e_cam',
  'e_vision',
  'e_video_ui',
] as const

export type TopologyEdgeId = (typeof TOPOLOGY_EDGE_IDS)[number]

//--------//
// 模块：日志映射工具 — 初始化各边空数组、判断合法 edge id
export function emptyEdgeLogMap(): Record<string, string[]> {
  return Object.fromEntries(TOPOLOGY_EDGE_IDS.map((id) => [id, [] as string[]])) as Record<string, string[]>
}

export function isTopologyEdgeId(s: string): s is TopologyEdgeId {
  return (TOPOLOGY_EDGE_IDS as readonly string[]).includes(s)
}
