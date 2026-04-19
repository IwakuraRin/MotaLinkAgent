<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { t } from '../../i18n'

type FSEntry = {
  name: string
  is_dir: boolean
  size: number
  mod_time: string
  mode: number
}

const currentPath = ref('/')
const entries = ref<FSEntry[]>([])
const parentPath = ref('/')
const loading = ref(false)
const errMsg = ref('')
const truncated = ref(false)

function joinPath(base: string, name: string): string {
  if (base === '/') return `/${name}`
  return `${base.replace(/\/+$/, '')}/${name}`
}

function parentOf(p: string): string {
  if (p === '/' || p === '') return '/'
  const x = p.replace(/\/+$/, '')
  const i = x.lastIndexOf('/')
  if (i <= 0) return '/'
  return x.slice(0, i) || '/'
}

/** Path segments after root, for breadcrumb (root `/` rendered separately). */
const breadcrumbs = computed(() => {
  const p = currentPath.value.replace(/\/+$/, '') || '/'
  if (p === '/') return [] as { label: string; path: string }[]
  const parts = p.split('/').filter(Boolean)
  const segs: { label: string; path: string }[] = []
  let acc = ''
  for (const part of parts) {
    acc += `/${part}`
    segs.push({ label: part, path: acc })
  }
  return segs
})

async function loadDir(path: string) {
  loading.value = true
  errMsg.value = ''
  try {
    const r = await fetch(`/api/fs/list?path=${encodeURIComponent(path)}`, { credentials: 'include' })
    if (r.status === 401) {
      errMsg.value = t('fs.unauthorized')
      return
    }
    if (!r.ok) {
      const j = (await r.json().catch(() => ({}))) as { error?: string }
      errMsg.value = typeof j.error === 'string' ? j.error : t('fs.loadError')
      return
    }
    const j = (await r.json()) as {
      path?: string
      parent?: string
      entries?: FSEntry[]
      truncated?: boolean
    }
    currentPath.value = typeof j.path === 'string' ? j.path : path
    parentPath.value = typeof j.parent === 'string' ? j.parent : parentOf(currentPath.value)
    entries.value = Array.isArray(j.entries) ? j.entries : []
    truncated.value = !!j.truncated
  } catch {
    errMsg.value = t('fs.loadError')
  } finally {
    loading.value = false
  }
}

function goUp() {
  void loadDir(parentPath.value)
}

function openEntry(e: FSEntry) {
  if (!e.is_dir) return
  void loadDir(joinPath(currentPath.value, e.name))
}

function goPath(p: string) {
  void loadDir(p)
}

function fmtSize(n: number, isDir: boolean): string {
  if (isDir) return '—'
  if (n < 1024) return `${n} B`
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`
  if (n < 1024 * 1024 * 1024) return `${(n / (1024 * 1024)).toFixed(1)} MB`
  return `${(n / (1024 * 1024 * 1024)).toFixed(1)} GB`
}

function fmtMode(m: number): string {
  const o = m & 0o777
  return o.toString(8).padStart(3, '0')
}

onMounted(() => {
  void loadDir(currentPath.value)
})
</script>

<template>
  <div class="flex h-full min-h-[200px] flex-col bg-[#0f141c] text-[#d4dce6]">
    <div class="flex shrink-0 flex-wrap items-center gap-1 border-b border-white/10 px-2 py-1.5 text-[11px]">
      <button
        type="button"
        class="rounded border border-white/15 bg-white/5 px-2 py-0.5 hover:bg-white/10"
        :disabled="loading"
        @click="goUp"
      >
        {{ t('fs.up') }}
      </button>
      <button
        type="button"
        class="rounded border border-white/15 bg-white/5 px-2 py-0.5 hover:bg-white/10"
        :disabled="loading"
        @click="loadDir(currentPath)"
      >
        {{ t('fs.refresh') }}
      </button>
      <span class="mx-1 text-white/25">|</span>
      <button
        v-for="shortcut in ['/', '/home', '/mnt', '/media']"
        :key="shortcut"
        type="button"
        class="rounded border border-white/10 bg-transparent px-1.5 py-0.5 font-mono text-[10px] text-[#8ab4e8] hover:bg-white/10"
        :disabled="loading"
        @click="goPath(shortcut)"
      >
        {{ shortcut }}
      </button>
    </div>

    <div class="shrink-0 border-b border-white/10 px-2 py-1 font-mono text-[10px] text-[#8a9aaa]">
      <span class="text-white/40">{{ t('fs.path') }} </span>
      <button
        type="button"
        class="hover:text-[#8ab4e8]"
        :class="breadcrumbs.length === 0 ? 'font-semibold text-[#c8d8e8]' : ''"
        @click="goPath('/')"
      >
        /
      </button>
      <template v-for="(s, i) in breadcrumbs" :key="s.path">
        <span class="text-white/35">/</span>
        <button
          type="button"
          class="hover:text-[#8ab4e8]"
          :class="i === breadcrumbs.length - 1 ? 'font-semibold text-[#c8d8e8]' : ''"
          @click="goPath(s.path)"
        >
          {{ s.label }}
        </button>
      </template>
    </div>

    <p v-if="errMsg" class="shrink-0 bg-red-950/50 px-2 py-1 text-[11px] text-red-200">{{ errMsg }}</p>
    <p v-else-if="truncated" class="shrink-0 bg-amber-950/40 px-2 py-1 text-[10px] text-amber-100">
      {{ t('fs.truncated') }}
    </p>

    <div class="min-h-0 flex-1 overflow-auto">
      <table class="w-full border-collapse text-left text-[11px]">
        <thead class="sticky top-0 z-[1] bg-[#1a2332] text-[#8a9aaa]">
          <tr>
            <th class="border-b border-white/10 px-2 py-1 font-medium">{{ t('fs.col.name') }}</th>
            <th class="border-b border-white/10 px-2 py-1 font-medium">{{ t('fs.col.type') }}</th>
            <th class="border-b border-white/10 px-2 py-1 font-medium">{{ t('fs.col.size') }}</th>
            <th class="border-b border-white/10 px-2 py-1 font-medium">{{ t('fs.col.mode') }}</th>
            <th class="border-b border-white/10 px-2 py-1 font-medium">{{ t('fs.col.modified') }}</th>
          </tr>
        </thead>
        <tbody>
          <tr v-if="loading">
            <td colspan="5" class="px-2 py-6 text-center text-white/40">{{ t('fs.loading') }}</td>
          </tr>
          <tr
            v-for="e in entries"
            v-else
            :key="e.name"
            class="border-b border-white/5"
            :class="e.is_dir ? 'cursor-pointer hover:bg-white/5' : 'hover:bg-white/[0.02]'"
            @click="openEntry(e)"
          >
            <td class="max-w-[200px] truncate px-2 py-1 font-mono text-[#e8eef4]">
              <span class="mr-1 text-base leading-none" aria-hidden="true">{{ e.is_dir ? '📁' : '📄' }}</span>
              {{ e.name }}
            </td>
            <td class="whitespace-nowrap px-2 py-1 text-[#9aacbc]">
              {{ e.is_dir ? t('fs.folder') : t('fs.file') }}
            </td>
            <td class="whitespace-nowrap px-2 py-1 font-mono text-[#9aacbc]">{{ fmtSize(e.size, e.is_dir) }}</td>
            <td class="whitespace-nowrap px-2 py-1 font-mono text-[#6a7a8a]">{{ fmtMode(e.mode) }}</td>
            <td class="whitespace-nowrap px-2 py-1 font-mono text-[#7a8a9a]">{{ e.mod_time || '—' }}</td>
          </tr>
          <tr v-if="!loading && !entries.length && !errMsg">
            <td colspan="5" class="px-2 py-6 text-center text-white/35">{{ t('fs.empty') }}</td>
          </tr>
        </tbody>
      </table>
    </div>

    <p class="shrink-0 border-t border-white/10 px-2 py-1 text-[10px] text-white/35">
      {{ t('fs.hint') }}
    </p>
  </div>
</template>
