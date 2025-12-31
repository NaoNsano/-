<template>
  <v-app>
    <v-app-bar color="surface" elevation="1" density="comfortable">
      <template v-slot:prepend>
        <v-avatar color="primary" variant="tonal" size="40" class="ml-2">
          <v-icon icon="mdi-google-analytics" color="primary"></v-icon>
        </v-avatar>
      </template>
      <v-app-bar-title class="font-weight-bold text-grey-darken-3">
        C++ 弹幕热词实时监控
      </v-app-bar-title>
      
      <v-chip class="mr-4 font-weight-bold font-mono" color="primary" variant="flat" label>
        <v-icon start icon="mdi-clock-outline"></v-icon>
        Simulated Time: {{ currentSystemTime }}
      </v-chip>
    </v-app-bar>

    <v-main class="bg-background">
      <v-container fluid class="pa-4 h-100 d-flex flex-column">
        
        <v-row dense class="mb-2">
          <v-col cols="12" md="4">
            <v-card elevation="0" border rounded="xl" class="h-100">
              <v-card-item>
                <template v-slot:prepend>
                  <v-icon color="primary" icon="mdi-file-document-outline" class="mr-2"></v-icon>
                </template>
                <v-card-title class="text-subtitle-1 font-weight-bold">数据源回放 (Ingest)</v-card-title>
              </v-card-item>
              <v-card-text>
                <v-file-input
                  v-model="selectedFile"
                  label="选择 .txt 日志文件"
                  variant="outlined"
                  density="compact"
                  accept=".txt"
                  prepend-icon=""
                  prepend-inner-icon="mdi-paperclip"
                  hide-details
                  class="mb-3"
                ></v-file-input>
                
                <div class="d-flex gap-2">
                  <v-btn 
                    color="primary" 
                    variant="flat" 
                    rounded="pill"
                    class="flex-grow-1"
                    :loading="isProcessing"
                    :disabled="!selectedFile"
                    @click="processFile"
                  >
                    <v-icon start icon="mdi-play"></v-icon>
                    开始解析回放
                  </v-btn>
                  <v-btn 
                    color="grey-darken-1" 
                    variant="tonal" 
                    icon="mdi-stop" 
                    rounded="circle"
                    :disabled="!isProcessing"
                    @click="stopProcessing"
                  ></v-btn>
                </div>
                <v-progress-linear
                  v-if="isProcessing"
                  v-model="progress"
                  color="primary"
                  height="4"
                  rounded
                  class="mt-3"
                  striped
                  indeterminate
                ></v-progress-linear>
              </v-card-text>
            </v-card>
          </v-col>

          <v-col cols="12" md="4">
            <v-card elevation="0" border rounded="xl" class="h-100">
              <v-card-item>
                <template v-slot:prepend>
                  <v-icon color="error" icon="mdi-fire" class="mr-2"></v-icon>
                </template>
                <v-card-title class="text-subtitle-1 font-weight-bold">压力测试</v-card-title>
              </v-card-item>
              <v-card-text>
                <p class="text-caption text-grey-darken-1 mb-3">
                  向后端发送高频随机数据，测试系统稳定性。
                </p>
                <v-btn 
                  block 
                  color="error" 
                  variant="tonal" 
                  rounded="pill" 
                  @click="startStressTest"
                  :loading="isStressTesting"
                >
                  <v-icon start icon="mdi-speedometer"></v-icon>
                  发送 1000 条随机弹幕
                </v-btn>
              </v-card-text>
            </v-card>
          </v-col>

          <v-col cols="12" md="4">
            <v-card elevation="0" border rounded="xl" class="h-100">
              <v-card-item>
                <template v-slot:prepend>
                  <v-icon color="success" icon="mdi-magnify" class="mr-2"></v-icon>
                </template>
                <v-card-title class="text-subtitle-1 font-weight-bold">自定义查询</v-card-title>
              </v-card-item>
              <v-card-text>
                 <v-row dense align="center">
                    <v-col cols="8">
                      <v-text-field
                        v-model="manualQueryTime"
                        label="时间点/区间/历史 (空=10min, all=历史, start-end=区间)"
                        placeholder="例如 0:15:00 或 1698765432-1698769999 或 all"
                        variant="outlined"
                        density="compact"
                        hide-details
                      ></v-text-field>
                    </v-col>
                    <v-col cols="4">
                      <v-btn color="success" variant="flat" block rounded="pill" @click="handleManualQuery">
                        查询
                      </v-btn>
                    </v-col>
                 </v-row>
                 <div class="d-flex align-center mt-2 justify-space-between">
                    <span class="text-caption text-grey">当前 Top K 设置: {{ customK }}</span>
                    <v-slider
                      v-model="customK"
                      min="3" max="20" step="1"
                      thumb-label
                      hide-details
                      density="compact"
                      color="primary"
                      style="max-width: 150px"
                    ></v-slider>
                 </div>
              </v-card-text>
            </v-card>
          </v-col>
        </v-row>

        <v-row class="flex-grow-0 mb-2" style="min-height: 320px;">
          <v-col cols="12" md="6">
            <v-card elevation="0" border rounded="xl" class="h-100">
              <v-card-title class="text-body-2 font-weight-bold text-grey-darken-2 pt-3 px-4">
                实时热词排行 (Top K)
                <v-chip size="x-small" color="success" variant="flat" class="ml-2">LIVE 1s</v-chip>
              </v-card-title>
              <div ref="barChartRef" style="height: 280px; width: 100%;"></div>
            </v-card>
          </v-col>
          
          <v-col cols="12" md="6">
            <v-card elevation="0" border rounded="xl" class="h-100">
              <v-card-title class="text-body-2 font-weight-bold text-grey-darken-2 pt-3 px-4">
                词汇热度趋势 (Slope Analysis)
                <span class="text-caption text-grey ml-2">(支持负斜率显示)</span>
              </v-card-title>
              <div ref="lineChartRef" style="height: 280px; width: 100%;"></div>
            </v-card>
          </v-col>
        </v-row>

        <v-row class="flex-grow-1" style="min-height: 0;">
          <v-col cols="12" class="h-100 pb-0">
            <v-card elevation="3" rounded="xl" class="terminal-card bg-grey-darken-4 fill-height d-flex flex-column">
              <div class="d-flex align-center px-4 py-2 bg-grey-darken-3 border-bottom-dark">
                <v-icon icon="mdi-console" size="small" class="mr-2 text-grey"></v-icon>
                <span class="text-caption font-mono text-grey-lighten-1">SYSTEM TERMINAL OUTPUT</span>
                <v-spacer></v-spacer>
                <v-btn size="x-small" icon="mdi-delete" variant="text" color="grey" @click="clearLogs"></v-btn>
              </div>
              
              <div 
                ref="terminalBody" 
                class="terminal-body pa-3 flex-grow-1 font-mono text-caption"
              >
                <div v-if="logs.length === 0" class="text-grey-darken-2 text-center mt-10">
                  Waiting for input...
                </div>
                <div v-for="(log, i) in logs" :key="i" :class="`log-line text-${log.color} mb-1`">
                  <span class="text-grey-darken-1 select-none mr-2">[{{ log.time }}]</span>
                  <span v-html="log.content"></span>
                </div>
                <div ref="terminalEnd"></div>
              </div>
            </v-card>
          </v-col>
        </v-row>

      </v-container>
    </v-main>
  </v-app>
</template>

<script setup lang="ts">
import { ref, reactive, nextTick, onMounted, onUnmounted } from 'vue'
import * as echarts from 'echarts'
import axios from 'axios'

// --- 类型定义 ---
interface LogItem {
  time: string;
  content: string;
  color: string;
}

// --- 状态变量 ---
const currentSystemTime = ref<string>('0:00:00')
// 单文件模式
const selectedFile = ref<File | null>(null)

const isProcessing = ref(false)
const isStressTesting = ref(false)
const progress = ref(0)
const logs = ref<LogItem[]>([])
const terminalEnd = ref<HTMLElement | null>(null)
const shouldStop = ref(false)

// 查询设置
const manualQueryTime = ref('')
const customK = ref(10)

// 图表 DOM 引用
const barChartRef = ref<HTMLElement | null>(null)
const lineChartRef = ref<HTMLElement | null>(null)
let barChart: echarts.ECharts | null = null
let lineChart: echarts.ECharts | null = null
let chartTimer: any = null

// 发送控制参数（可按需调整）
const SEND_INTERVAL_MS = 2         // 每行最小发送间隔（毫秒）
const UI_YIELD_EVERY = 20          // 每发送多少行做一次 UI 刷新与短延迟

// --- 初始化与生命周期 ---
onMounted(() => {
  nextTick(() => {
    initCharts()
    addLog('System initialized.', 'info')
    
    // 启动 1s 轮询更新图表（独立于文件回放）
    chartTimer = setInterval(updateCharts, 1000)
    
    window.addEventListener('resize', resizeCharts)
  })
})

onUnmounted(() => {
  if (chartTimer) clearInterval(chartTimer)
  window.removeEventListener('resize', resizeCharts)
  barChart?.dispose()
  lineChart?.dispose()
})

const resizeCharts = () => {
  barChart?.resize()
  lineChart?.resize()
}

// --- 核心逻辑 1: 文件解析与回放 ---
const processFile = async () => {
  if (!selectedFile.value) return
  const file = selectedFile.value
  isProcessing.value = true
  shouldStop.value = false
  progress.value = 0
  addLog(`Load file: ${file.name}`, 'info')

  const reader = new FileReader()

  reader.onerror = (e) => {
    console.error('File read error', e)
    addLog('File read error', 'error')
    isProcessing.value = false
  }
  
  reader.onload = async (e) => {
    const text = e.target?.result as string
    if (!text) {
      isProcessing.value = false
      addLog('Empty file or read failed.', 'warning')
      return
    }

    const lines = text.split(/\r?\n/)
    const totalLines = lines.length

    // 正则（仅用于识别时间戳与 ACTION，不用于清洗）
    const timeRegex = /\[(\d{1,2}:\d{2}:\d{2})\]/
    const actionRegex = /\[ACTION\]\s*QUERY\s*K=(\d+)/i

    addLog(`Reading ${totalLines} lines... Playback started.`, 'success')

    // 顺序发送函数：逐行发送，保序且可中断
    const sendLinesSequential = async (linesArr: string[]) => {
      for (let i = 0; i < linesArr.length; i++) {
        if (shouldStop.value) {
          addLog('Playback stopped by user.', 'warning')
          break
        }

        // 保留原始行内容：仅移除行尾单个 '\r'（Windows 回车），不做其它清洗
        let line = linesArr[i]
        // 2. 如果是 undefined (TypeScript 检查) 或 null，直接跳过
        if (line === undefined || line === null) continue;
        
        // 3. 现在 line 确认为 string，可以安全调用 endsWith
        if (line.endsWith('\r')) line = line.slice(0, -1)
        // --- 修复结束 ---

        if (!line) {
          // 空行跳过但仍更新进度
          progress.value = ((i + 1) / totalLines) * 100
          continue
        }

        // 提取时间戳用于更新系统时间（仅显示，无修改）
        const timeMatch = line.match(timeRegex)
        if (timeMatch) {
          currentSystemTime.value = timeMatch[1] || ''
        }

        // 检查 ACTION 行（触发查询）
        const actionMatch = line.match(actionRegex)
        if (actionMatch) {
          const k = parseInt(actionMatch[1] || '0') || customK.value
          addLog(`[ACTION] Auto Query Top ${k} triggered`, 'action')
          // 等待后端查询结果并在终端显示
          await fetchTopKForTerminal(k)
          // 给点时间（视觉上、也避免塞满后端）
          await delay(500)
        } else {
          // 普通弹幕：按顺序发送一行（text/plain）
          try {
            await axios.post('/api/ingest', line, {
              headers: { 'Content-Type': 'text/plain' }
            })
          } catch (err) {
            // 记录错误在控制台，不把 UI 刷满
            console.error('ingest error', err)
          }
        }

        // 更新进度条（保序地）
        progress.value = ((i + 1) / totalLines) * 100

        // 每发送若干行，让出一次事件循环以便更新 UI / 响应中断
        if (i % UI_YIELD_EVERY === 0) {
          await nextTick()
          await delay(1)
        }

        // 保持每行之间有最小间隔，降低瞬时并发与限流风险
        if (SEND_INTERVAL_MS > 0) await delay(SEND_INTERVAL_MS)
      }
    }

    // 启动发送（并等待完成或中断）
    await sendLinesSequential(lines)

    isProcessing.value = false
    addLog('File playback finished.', 'success')
  }

  reader.readAsText(file, 'UTF-8')
  // 若文件确实是 GBK 等编码，浏览器端转码较复杂，需要额外库；现代多数为 UTF-8
}

const stopProcessing = () => {
  shouldStop.value = true
}

// --- 核心逻辑 2: 压力测试 ---
const startStressTest = async () => {
  isStressTesting.value = true
  addLog('>>> Starting Stress Test (1000 msgs)...', 'warning')
  
  const words = ["C++", "Vue", "HighPerformance", "Jieba", "Crow", "LockFree", "Thread", "Mutex"]
  
  let count = 0;
  const interval = setInterval(() => {
      const randomWord = words[Math.floor(Math.random() * words.length)];
      // 模拟类似文件的时间戳格式
      const msg = `[${currentSystemTime.value}] StressTest: ${randomWord} #${count}`;
      
      // 【关键】 headers 设置为 text/plain
      axios.post('/api/ingest', msg, {
          headers: {'Content-Type': 'text/plain'}
      }).catch(()=>{})

      count++;
      if (count >= 1000) { 
          clearInterval(interval);
          isStressTesting.value = false;
          addLog(">>> Stress Test Finished.", 'success');
      }
  }, 5); // 5ms 发送一次
}

// --- 新增/改造的查询逻辑 ---
// 解析后端返回并把结果显示在终端（复用）
const displayTopKResponse = (data: any) => {
  if (!data || !data.data) {
    addLog('Empty result.', 'warning')
    return
  }
  const listHtml = data.data.map((item: any, idx: number) => 
    `<span style="color: #4CAF50">[${idx+1}] ${item.word}</span> <span style="color: #9E9E9E">(${item.count})</span>`
  ).join('&nbsp;&nbsp; | &nbsp;&nbsp;')
  addLog(`Result: ${listHtml}`, 'info')
}

// 原来的 10min TopK 查询（保留）
const fetchTopKForTerminal = async (k: number) => {
  try {
    const res = await axios.get(`/api/topk?k=${encodeURIComponent(k)}`)
    const data = res.data
    if (data && data.status === 'success') {
      displayTopKResponse(data)
    } else {
      addLog('TopK query failed.', 'error')
    }
  } catch (err) {
    addLog('Query failed.', 'error')
  }
}

// 历史（全量）TopK
const fetchHistoryForTerminal = async (k: number) => {
  try {
    const res = await axios.get(`/api/history?k=${encodeURIComponent(k)}`)
    const data = res.data
    if (data && data.status === 'success') {
      displayTopKResponse(data)
    } else {
      addLog('History query failed.', 'error')
    }
  } catch (err) {
    addLog('History query failed.', 'error')
  }
}

// 按时间范围查询（start_ts, end_ts 单位为秒）
// 如果 end_ts 为 0，后端会默认把 end 设置为 now（你的后端实现有这个逻辑）
const fetchRangeForTerminal = async (start_ts: number, end_ts: number, k: number) => {
  try {
    const url = `/api/range?start=${encodeURIComponent(String(start_ts))}&end=${encodeURIComponent(String(end_ts))}&k=${encodeURIComponent(k)}`
    const res = await axios.get(url)
    const data = res.data
    if (data && data.status === 'success') {
      displayTopKResponse(data)
    } else {
      addLog('Range query failed.', 'error')
    }
  } catch (err) {
    addLog('Range query failed.', 'error')
  }
}

// 将多种人类可读时间解析为 epoch 秒（best-effort）
const parseTimeToEpoch = (s: string): number => {
  if (!s) return NaN
  s = s.trim()
  // 纯数字 -> 直接当作 epoch 秒
  if (/^\d+$/.test(s)) {
    return parseInt(s, 10)
  }
  // 尝试 Date.parse（支持 ISO / "YYYY-MM-DD HH:MM:SS" 等）
  const parsedMs = Date.parse(s)
  if (!isNaN(parsedMs)) {
    return Math.floor(parsedMs / 1000)
  }
  // 支持 HH:MM:SS 或 H:MM:SS -> 当作“今天的时间”
  const hhmmss = s.match(/^(\d{1,2}):(\d{2}):(\d{2})$/)
  if (hhmmss) {
    const now = new Date()
    // --- 修复：添加 || '0' 作为回退 ---
    const hh = parseInt(hhmmss[1] || '0', 10)
    const mm = parseInt(hhmmss[2] || '0', 10)
    const ss = parseInt(hhmmss[3] || '0', 10)
    now.setHours(hh, mm, ss, 0)
    return Math.floor(now.getTime() / 1000)
  }
  // 解析失败
  return NaN
}

// 解析 manualQueryTime 并选择调用哪个接口（topk / history / range）
const handleManualQuery = async () => {
  const text = (manualQueryTime.value || '').trim()
  addLog(`Manual Query: "${text || 'LAST10MIN'}" Top ${customK.value}`, 'action')

  // empty -> 10min TopK
  if (!text) {
    await fetchTopKForTerminal(customK.value)
    return
  }

  // "all" 或 "history" -> 全量历史
  if (/^(all|history)$/i.test(text)) {
    await fetchHistoryForTerminal(customK.value)
    return
  }

  // 区间模式：start-end （允许 end 为空，后端会把 end==0 处理为 now）
  if (text.includes('-')) {
    const parts = text.split('-', 2)
    // --- 修复：添加安全访问 ---
    const startStr = (parts[0] || '').trim()
    // 检查 parts[1] 是否存在
    const endStr = (parts.length > 1 && parts[1]) ? parts[1].trim() : ''
    
    const start_ts = parseTimeToEpoch(startStr)
    const end_ts = endStr === '' ? 0 : parseTimeToEpoch(endStr)

    if (isNaN(start_ts)) {
      addLog('Cannot parse start time for range query.', 'error')
      return
    }
    if (endStr !== '' && isNaN(end_ts)) {
      addLog('Cannot parse end time for range query.', 'error')
      return
    }
    await fetchRangeForTerminal(start_ts, end_ts, customK.value)
    return
  }

  // 单个时间点 -> 当作 start，end=0（即 start 到现在）
  const single_ts = parseTimeToEpoch(text)
  if (!isNaN(single_ts)) {
    await fetchRangeForTerminal(single_ts, 0, customK.value)
    return
  }

  addLog('Unrecognized manual query format. Examples: empty, all, 1698765432-1698769999, 0:15:00-0:20:00, 2023-12-01 10:00:00', 'error')
}

// --- ECharts 配置 ---
const updateCharts = async () => {
  try {
    // 1. 获取 Top K (柱状图) —— 默认仍是 10min 窗口
    const resTop = await axios.get(`/api/topk?k=${customK.value}`)
    if (resTop.data && resTop.data.data) {
       updateBarChart(resTop.data.data)
    }

    // 2. 获取趋势 (折线图)
    const resTrend = await axios.get(`/api/trending?k=5`)
    if (resTrend.data && resTrend.data.data) {
       updateLineChart(resTrend.data.data)
    }
  } catch (e) {
    console.warn("Chart polling failed (backend might be offline)")
  }
}

const initCharts = () => {
  if (barChartRef.value) {
    barChart = echarts.init(barChartRef.value)
    barChart.setOption({
      backgroundColor: 'transparent',
      tooltip: { trigger: 'axis' },
      grid: { top: '10%', bottom: '5%', left: '5%', right: '5%', containLabel: true },
      xAxis: { type: 'category', axisLabel: { color: '#666' } },
      yAxis: { type: 'value', splitLine: { lineStyle: { type: 'dashed', color: '#eee' } } },
      series: [{
        type: 'bar',
        barWidth: '40%',
        itemStyle: { color: '#1976D2', borderRadius: [4, 4, 0, 0] }
      }]
    })
  }

  if (lineChartRef.value) {
    lineChart = echarts.init(lineChartRef.value)
    lineChart.setOption({
      backgroundColor: 'transparent',
      tooltip: { trigger: 'axis' },
      grid: { top: '10%', bottom: '5%', left: '5%', right: '5%', containLabel: true },
      xAxis: { type: 'category', axisLabel: { color: '#666' } }, // 这里应该放词
      yAxis: { 
        type: 'value', 
        scale: true,
        splitLine: { lineStyle: { type: 'dashed', color: '#eee' } } 
      },
      series: [{
        type: 'line',
        smooth: true,
        itemStyle: { color: '#ED6C02' },
        areaStyle: { opacity: 0.1, color: '#ED6C02' },
        markLine: {
          data: [{ yAxis: 0, lineStyle: { color: '#999' } }]
        }
      }]
    })
  }
}

const updateBarChart = (data: any[]) => {
  if (!barChart) return
  const categories = data.map(d => d.word)
  const values = data.map(d => d.count)
  barChart.setOption({
    xAxis: { data: categories },
    series: [{ data: values }]
  })
}

const updateLineChart = (data: any[]) => {
  if (!lineChart) return
  const categories = data.map(d => d.word)
  const slopes = data.map(d => d.slope)
  
  lineChart.setOption({
    xAxis: { data: categories },
    series: [{ data: slopes }]
  })
}


// --- 工具 ---
const delay = (ms: number) => new Promise(resolve => setTimeout(resolve, ms))

const addLog = (content: string, type: 'info' | 'success' | 'warning' | 'error' | 'action' = 'info') => {
  const colors = {
    info: 'grey-lighten-2',
    success: 'green-accent-3',
    warning: 'amber-accent-3',
    error: 'red-accent-2',
    action: 'light-blue-accent-2'
  }
  
  const now = new Date()
  const timeStr = `${now.getHours().toString().padStart(2,'0')}:${now.getMinutes().toString().padStart(2,'0')}:${now.getSeconds().toString().padStart(2,'0')}`

  logs.value.push({
    time: timeStr,
    content,
    color: colors[type]
  })

  // 保持日志最大长度，防止浏览器卡顿
  if (logs.value.length > 200) {
    logs.value.shift()
  }

  nextTick(() => {
    terminalEnd.value?.scrollIntoView({ behavior: 'smooth' })
  })
}

const clearLogs = () => {
  logs.value = []
}
</script>

<style scoped>
.font-mono {
  font-family: 'JetBrains Mono', 'Consolas', monospace;
}

.terminal-card {
  border: 1px solid #e0e0e0;
}

.border-bottom-dark {
  border-bottom: 1px solid #333;
}

.terminal-body {
  background-color: #121212; /* 纯黑背景 */
  overflow-y: auto;
  min-height: 200px;
}

/* 日志文字颜色微调 */
.text-grey-lighten-2 { color: #e0e0e0 !important; }
.text-green-accent-3 { color: #00e676 !important; }
.text-amber-accent-3 { color: #ffc400 !important; }
.text-red-accent-2 { color: #ff5252 !important; }
.text-light-blue-accent-2 { color: #40c4ff !important; }

/* 禁止选中文本 */
.select-none {
  user-select: none;
}

/* 滚动条美化 */
::-webkit-scrollbar {
  width: 8px;
  height: 8px;
}
::-webkit-scrollbar-track {
  background: #1e1e1e;
}
::-webkit-scrollbar-thumb {
  background: #424242;
  border-radius: 4px;
}
</style>
