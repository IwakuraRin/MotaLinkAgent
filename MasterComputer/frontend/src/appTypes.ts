// 作用：集中声明前端主应用跨组件共享的小型类型，避免 App.vue 和子组件重复定义。

export type MenuKey = function | settings

export type UpdateStatusPayload = {
  enabled: boolean
  update_available?: boolean
  local_sha?: string
  remote_sha?: string
  branch?: string
  changelog?: string
  changelog_ok?: boolean
  changelog_error?: string
  git_error?: string
  reason?: string
}

export type SerialDev = {
  path: string
  target: string
  kind: string
}

export const SERIAL_ROLE_KEYS = [atmega_uart, aux_serial] as const
export type SerialRoleKey = (typeof SERIAL_ROLE_KEYS)[number]
