# MimiClaw 简洁系统框图 — 文生图提示词

## 提示词（直接复制给 GPT / DALL-E 使用）

```
请绘制一张简洁、扁平风格的嵌入式 AI 系统框图，白底或浅灰背景，深色细线边框，无多余装饰。

画面主体分为三层，用横向矩形框和箭头表示数据流向：

**最上层 — 用户入口**（一排小矩形）：
- Telegram（绿色小框）
- 飞书（蓝色小框）
- WebSocket（橙色小框）
- 串口 CLI（灰色小框）
四个小框向下箭头 → 汇聚到中间

**中间层 — 核心处理**（一个大矩形框，画面中心）：
框内标注 "AI Agent（ESP32-S3）"，框内分为两个并列小区域：
- 左侧小框："LLM 推理（Claude / GPT）"
- 右侧小框："工具调用（搜索 / GPIO / LED / 红外）"
两个区域之间有双向箭头连接

**最下层 — 记忆与存储**（一排小矩形）：
- SOUL.md（人设）
- USER.md（用户）
- MEMORY.md（记忆）
- 会话文件（JSONL）
四个小框向上箭头 → 连接到中间核心框

**右侧独立小框 — 定时任务**（标注 "Cron + 心跳"），用虚线箭头连接到核心框

**左侧独立小框 — 硬件外设**（标注 "WiFi / OLED / ESP-NOW / 红外"），用虚线箭头连接到核心框

整体要求：
- 极简扁平风格，类似 Lucidchart / Draw.io 的简洁流程图
- 每个模块用圆角矩形，不同层级用颜色区分（上层浅绿、核心蓝、下层浅橙、侧边浅灰）
- 箭头清晰标注数据方向
- 文字中文，无多余装饰，无人物
- 适合放入技术文档或 PPT 的干净配图
```

---

## 极简版（适合短描述限制）

```
Flat minimal system architecture diagram, white background, rounded rectangles with thin borders, dark text.

Top row: four small boxes labeled Telegram, Feishu, WebSocket, Serial CLI, all arrows pointing down to center.

Center large box: "AI Agent (ESP32-S3)" with two inner boxes "LLM (Claude/GPT)" and "Tools (search/GPIO/LED/IR)" connected by bidirectional arrows.

Bottom row: four small boxes labeled SOUL.md, USER.md, MEMORY.md, Sessions (JSONL), all arrows pointing up to center.

Right side: small box "Cron + Heartbeat" with dashed arrow to center.
Left side: small box "Peripherals (WiFi/OLED/ESP-NOW/IR)" with dashed arrow to center.

Clean, no gradients, no shadows, no icons, just boxes and arrows. Infographic style suitable for documentation.
```

---

## 更更简洁版（一句话描述）

```
Minimal flat architecture diagram: top row (Telegram, Feishu, WebSocket, CLI) → center box "AI Agent (ESP32-S3)" with LLM and Tools inside → bottom row (SOUL.md, USER.md, MEMORY.md, Sessions). Side boxes: Cron+Heartbeat, WiFi+OLED+ESP-NOW. White background, rounded rectangles, clean arrows, no decoration, Chinese text.
```
