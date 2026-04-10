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

export function emptyEdgeLogMap(): Record<string, string[]> {
  return Object.fromEntries(TOPOLOGY_EDGE_IDS.map((id) => [id, [] as string[]])) as Record<string, string[]>
}

export function isTopologyEdgeId(s: string): s is TopologyEdgeId {
  return (TOPOLOGY_EDGE_IDS as readonly string[]).includes(s)
}
