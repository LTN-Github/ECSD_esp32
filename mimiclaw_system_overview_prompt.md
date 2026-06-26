# MimiClaw 系统功能总览图 — 文生图提示词

## 用途
将以下内容复制给 GPT（ChatGPT/DALL-E）或 Midjourney，生成一张 MimiClaw 嵌入式 AI 助理系统的功能架构总览图。

---

## 提示词（中文，适用于 DALL-E / GPT-4o 绘图）

```
请绘制一张高清、专业的嵌入式 AI 系统功能架构总览图，风格为现代科技信息图（infographic），主色调为深蓝+青色+白色，背景为深色渐变。

**中心主体**：一块 ESP32-S3 开发板（拇指大小的芯片，标注 "$5"），芯片中央发光，标注 "MimiClaw"，副标题 "口袋 AI 助理 — 纯 C / 裸机运行，无 Linux，无 Node.js"。

**从芯片向外辐射 6 大功能模块，用圆角矩形卡片连接**：

1. 【AI 大脑 — 智能引擎】（顶部，最大卡片，紫色边框）
   - 双 LLM 提供商：Anthropic Claude + OpenAI GPT（运行时切换）
   - ReAct Agent 循环：LLM 思考 → 调用工具 → 执行 → 再思考（最多10轮）
   - 工具调用系统：动态注册、JSON Schema 描述、统一调度
   - 双核架构：Core 0 跑 I/O，Core 1 跑 AI 推理

2. 【通信通道】（右上，绿色边框）
   - Telegram Bot（HTTPS 长轮询）
   - 飞书 Bot（企业 IM 集成）
   - WebSocket 网关（端口 18789，局域网直连）
   - 串口 CLI（USB 调试控制台，离线可用）
   - 消息总线：Inbound/Outbound 双队列，FreeRTOS 驱动

3. 【记忆与认知】（左上，橙色边框）
   - SOUL.md：AI 人设 / 性格定义
   - USER.md：用户画像、偏好、语言
   - MEMORY.md：长期记忆，跨重启持久保存
   - 每日笔记：YYYY-MM-DD.md，自动归档
   - 会话历史：per-chat JSONL 文件，环形缓冲区
   - 技能系统：SPIFFS 中可加载的 *.md 技能模块

4. 【工具生态】（右下，黄色边框）
   - 网络工具：web_search（Tavily/Brave 搜索）、get_current_time（网络校时）
   - 文件工具：read_file / write_file / edit_file / list_dir（SPIFFS 操作）
   - 硬件工具：GPIO 读写（带安全白名单）、WS2812 RGB LED 控制、红外收发（学习+发送，预置美的/格力空调码）
   - 任务工具：cron_add / cron_list / cron_remove（AI 自主创建定时任务）

5. 【自主调度】（左下，红色边框）
   - Cron 定时器：AI 可创建周期性或一次性任务，持久化到 cron.json，重启不丢失
   - 心跳服务：每 30 分钟扫描 HEARTBEAT.md，发现待办自动唤醒 AI 执行
   - 主动型助理：无需用户发消息，AI 自主驱动任务

6. 【网络与硬件外设】（底部，蓝色边框）
   - WiFi STA + 配网热点（Captive Portal，MimiClaw-XXXX）
   - HTTP CONNECT 代理（Clash/V2Ray 兼容，适配国内网络）
   - ESP-NOW 多设备组网：Master/Slave 主从架构，同一固件按角色分叉
   - OLED 显示屏（SSD1309 128x64，I2C，中文 UTF-8 支持，13页多级菜单，4按键导航）
   - OTA 无线更新：WiFi 远程刷固件，无需 USB
   - 红外遥控：半双工 GPIO4，预置空调码库

**底部信息条**：
- 左下角："16MB Flash + 8MB PSRAM | 0.5W 功耗 | 24/7 运行"
- 右下角："ESP32-S3 | FreeRTOS | 纯 C 固件"

**整体布局要求**：
- 中心芯片为视觉焦点，向外辐射6个模块卡片
- 模块之间用发光连线表示数据流
- 模块内部用细小图标表示子功能（如 WiFi 图标、灯泡图标、时钟图标、文件图标等）
- 风格简洁、专业、有科技感，适合技术文档封面
- 图中不要出现真实人脸
- 文字使用清晰的无衬线字体，中文为主
```

---

## 提示词（英文，适用于 Midjourney / Stable Diffusion）

```
A professional tech infographic system architecture diagram for "MimiClaw" — an embedded AI assistant running on a $5 ESP32-S3 chip (bare metal, pure C, no Linux). Dark blue gradient background with cyan and white accent colors.

**Center**: A glowing ESP32-S3 microcontroller chip labeled "MimiClaw" with subtitle "Pocket AI Agent — $5 Chip, Pure C, No OS".

**Six radiating modules connected to the center**:

1. **AI Brain** (top, purple): Dual LLM support (Claude + GPT), ReAct agent loop with tool calling, 10-round max reasoning, dual-core FreeRTOS (Core 0 for I/O, Core 1 for AI).

2. **Communication Channels** (top-right, green): Telegram Bot (HTTPS long polling), Feishu Bot, WebSocket Gateway (port 18789), Serial CLI (USB console), Message Bus with inbound/outbound queues.

3. **Memory & Cognition** (top-left, orange): SOUL.md (personality), USER.md (profile), MEMORY.md (long-term memory), daily notes (YYYY-MM-DD.md), per-chat JSONL session history, loadable skill modules from SPIFFS.

4. **Tool Ecosystem** (bottom-right, yellow): Web search (Tavily/Brave), time sync, SPIFFS file operations (read/write/edit/list), GPIO control (safety whitelist), WS2812 RGB LED, IR remote (learn + send, preset AC codes).

5. **Autonomous Scheduling** (bottom-left, red): Cron scheduler (AI-created periodic/one-time tasks, persisted to cron.json), Heartbeat service (scans HEARTBEAT.md every 30 min, auto-executes todos), proactive agent behavior without user input.

6. **Network & Hardware Peripherals** (bottom, blue): WiFi STA + onboarding AP, HTTP CONNECT proxy, ESP-NOW mesh (Master/Slave roles), OLED display (SSD1309 128x64, Chinese UTF-8, 13-page menu, 4 buttons), OTA wireless updates, IR transceiver on GPIO4.

**Footer specs**: "16MB Flash | 8MB PSRAM | 0.5W | 24/7 | ESP32-S3 | FreeRTOS".

**Style**: Clean vector-style tech diagram, glowing connection lines, small icons inside each module, no human faces, futuristic but readable, suitable for documentation cover.
```

---

## 极简版（适合文生图字数受限时）

```
Tech infographic, dark blue background, center: glowing ESP32-S3 chip labeled "MimiClaw — $5 Pocket AI". Six radiating modules: (1) AI Brain (Claude+GPT, ReAct loop, dual-core), (2) Channels (Telegram, Feishu, WebSocket, Serial CLI), (3) Memory (SOUL/USER/MEMORY.md, daily notes, JSONL sessions), (4) Tools (web search, GPIO, WS2812 LED, IR remote, file ops), (5) Autonomy (cron scheduler, heartbeat, proactive tasks), (6) Hardware (WiFi+AP, ESP-NOW mesh, OLED 128x64, OTA). Glowing lines, cyan accents, clean icons, no text clutter, futuristic tech style.
```

---

## 生成建议

| 工具 | 推荐提示词版本 | 备注 |
|------|-------------|------|
| **ChatGPT / GPT-4o** | 中文长版 | 可直接粘贴，描述最完整，生成效果最可控 |
| **DALL-E 3** | 中文长版 | 支持复杂指令，中文文字渲染较好 |
| **Midjourney** | 英文版 | 英文关键词效果更稳定，适合 --ar 16:9 |
| **Stable Diffusion / Flux** | 英文版或极简版 | 根据模型能力调整描述长度 |

**Midjourney 参数建议**：
```
--ar 16:9 --style raw --v 6
```

**GPT-4o 绘图建议**：
在提示词前加一句："请生成一张宽屏（16:9）的技术架构图，风格为现代深色科技信息图。"
