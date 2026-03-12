# WaterSort 游戏内 AI 聊天助手 — 技术设计方案

## 1. 系统概述

### 1.1 目标
在 WaterSort OpenGL 游戏中嵌入一个 AI 聊天助手，用户可以随时呼出对话面板，
向 AI 提问关于游戏规则、攻略提示、当前局面分析等问题，AI 实时返回专业回答。

### 1.2 核心特性
- **F1 键** 打开/关闭聊天面板
- **实时局面分析**：AI 能读取当前游戏状态（瓶子颜色/层数/已用步数）
- **智能回答**：游戏规则、策略建议、当前局面提示
- **异步调用**：AI 请求在后台线程执行，不阻塞游戏渲染
- **打字机效果**：回答逐字显示，提升体验感

---

## 2. 系统架构

```
┌─────────────────────────────────────────────────┐
│            WaterSort Game (OpenGL/C++)           │
│                                                  │
│  ┌──────────────┐    ┌──────────────────────┐   │
│  │  Game Logic   │    │   Chat UI Overlay    │   │
│  │  (existing)   │◄──►│  (2D OpenGL渲染)     │   │
│  └──────┬───────┘    └──────────┬───────────┘   │
│         │                       │                │
│  ┌──────┴───────────────────────┴───────────┐   │
│  │           ChatAgent Module                │   │
│  │  ┌─────────────┐  ┌──────────────────┐   │   │
│  │  │ GameContext  │  │  LLM Client      │   │   │
│  │  │ Serializer  │  │  (WinHTTP HTTPS) │   │   │
│  │  └─────────────┘  └────────┬─────────┘   │   │
│  │  ┌─────────────┐          │              │   │
│  │  │ Knowledge   │          │              │   │
│  │  │ Base (内嵌) │          │              │   │
│  │  └─────────────┘          │              │   │
│  └───────────────────────────┼──────────────┘   │
└──────────────────────────────┼──────────────────┘
                               │ HTTPS POST
                    ┌──────────┴──────────┐
                    │   LLM API Service   │
                    │  (智谱AI GLM-4-Flash │
                    │   或 OpenAI GPT-4o  │
                    │   或 DeepSeek V3)   │
                    └─────────────────────┘
```

---

## 3. 技术选型

### 3.1 HTTP 客户端：WinHTTP（Windows 原生）

**理由**：
- ✅ 零外部依赖（Windows 自带 `winhttp.dll`）
- ✅ 原生 HTTPS/TLS 支持（无需安装 OpenSSL）
- ✅ 项目已经 `#include <windows.h>`
- ✅ 支持异步操作

**链接库**：只需添加 `#pragma comment(lib, "winhttp.lib")`

### 3.2 JSON 解析：手写轻量级解析器

**理由**：
- 项目是单文件 C++ 程序，不适合引入大型头文件库
- LLM API 响应格式固定，只需提取 `content` 字段
- 约 50 行代码即可实现

### 3.3 LLM API 选择

| 服务商 | 模型 | 价格 | 延迟 | 推荐度 |
|--------|------|------|------|--------|
| **智谱AI** | GLM-4-Flash | ¥0.1/百万tokens | ~1s | ⭐⭐⭐⭐⭐ |
| DeepSeek | DeepSeek-V3 | ¥1/百万tokens | ~1.5s | ⭐⭐⭐⭐ |
| 月之暗面 | Moonshot-v1-8k | ¥12/百万tokens | ~2s | ⭐⭐⭐ |
| OpenAI | GPT-4o-mini | $0.15/百万tokens | ~1s | ⭐⭐⭐⭐ |

**推荐：智谱AI GLM-4-Flash**
- 国内访问快，无需代理
- 注册即送 500 万 tokens 免费额度
- API 格式与 OpenAI 兼容

---

## 4. 模块设计

### 4.1 ChatAgent 核心类

```cpp
// ===== AI Chat Agent =====
#define MAX_CHAT_MESSAGES 20
#define MAX_MSG_LEN 512

struct ChatMessage {
    char role[16];      // "user" or "assistant"
    char content[MAX_MSG_LEN];
    float timestamp;
};

struct ChatAgent {
    int isOpen;                              // 面板是否打开
    int isLoading;                           // 是否正在等待API响应
    char inputBuffer[256];                   // 用户输入缓冲
    int inputLen;                            // 输入长度
    ChatMessage messages[MAX_CHAT_MESSAGES]; // 聊天历史
    int messageCount;                        // 消息数量
    int displayCharCount;                    // 打字机效果：已显示字符数
    char apiKey[128];                        // API Key
    char apiEndpoint[256];                   // API 端点
    char modelName[64];                      // 模型名称
};
```

### 4.2 游戏状态序列化

```cpp
// 将当前游戏状态序列化为文字描述，供AI理解
void serializeGameState(char* buffer, int bufferSize) {
    // 输出格式示例：
    // "当前关卡: Easy, 已用步数: 5, 瓶子数: 5
    //  瓶1: [红,蓝,绿,红] (满)
    //  瓶2: [蓝,绿] (半满)
    //  瓶3: [] (空)
    //  ..."
}
```

### 4.3 System Prompt（知识库）

```
你是 WaterSort Puzzle（水瓶排序益智游戏）的AI助手。

游戏规则：
1. 目标：将所有同色液体倒入同一个瓶子中
2. 每个瓶子最多容纳4层液体
3. 只能将顶层液体倒入另一个瓶子
4. 倒入时，源瓶顶部连续同色的所有层会一起倒出
5. 目标瓶必须有足够空间容纳液体

策略技巧：
- 优先使用空瓶作为中转站
- 尽量避免把不同颜色的液体倒在一起
- 先处理颜色层数最少的颜色
- 如果一个瓶子顶部有多层同色液体，优先利用它
- 陷入困局时考虑 Undo (按U键)

你会根据当前游戏局面给出具体建议。保持简洁友好。
```

### 4.4 聊天 UI 渲染

**布局**：
```
┌──────────────────────────────┐
│  🤖 AI 助手           [X]   │  ← 标题栏
├──────────────────────────────┤
│                              │
│  User: 这关怎么过？          │  ← 消息历史区
│                              │
│  AI: 我看到你有3种颜色和     │
│  5个瓶子。建议先把瓶1的红色  │
│  倒到瓶4...                  │
│                              │
│  User: 怎么判断死局？        │
│                              │
│  AI: 当所有瓶子都满了且没有  │
│  空瓶时，如果顶层颜色都不匹  │
│  配，就是死局。按U撤销。     │
│                              │
├──────────────────────────────┤
│  > 输入问题...          [⏎]  │  ← 输入区
└──────────────────────────────┘
```

- 右侧 1/3 屏幕宽度
- 半透明深色背景 (RGBA: 0.1, 0.12, 0.15, 0.92)
- 圆角矩形面板
- 消息区可滚动

---

## 5. 实现计划

### Phase 1：基础设施（~150行代码）
1. WinHTTP HTTPS POST 封装函数
2. JSON 响应解析（提取 assistant content）
3. API 调用测试

### Phase 2：ChatAgent 核心（~200行代码）
4. ChatAgent 数据结构
5. System Prompt + 游戏状态序列化
6. 消息历史管理
7. 异步 API 调用（std::thread / CreateThread）

### Phase 3：游戏内 UI（~250行代码）
8. 聊天面板 2D 渲染（OpenGL ortho projection）
9. 键盘输入处理（FreeGLUT keyboard callback）
10. 打字机动画效果
11. 消息滚动

### Phase 4：集成 & 优化（~50行代码）
12. F1 键绑定
13. 游戏状态实时注入
14. 错误处理（网络超时、API 错误）
15. API Key 从配置文件读取

**总计新增代码量**：约 600-700 行 C++ 代码

---

## 6. API 调用示例

### 6.1 请求格式（智谱AI / OpenAI 兼容）

```
POST https://open.bigmodel.cn/api/paas/v4/chat/completions
Content-Type: application/json
Authorization: Bearer <API_KEY>

{
  "model": "glm-4-flash",
  "messages": [
    {"role": "system", "content": "<system_prompt + game_state>"},
    {"role": "user", "content": "这关怎么过？"}
  ],
  "temperature": 0.7,
  "max_tokens": 300
}
```

### 6.2 响应格式

```json
{
  "choices": [{
    "message": {
      "role": "assistant",
      "content": "我分析了你当前的局面..."
    }
  }]
}
```

---

## 7. 文件结构

```
WaterSort_Source/
├── WaterSort.cpp          # 主程序（在末尾添加 ChatAgent 代码）
├── chat_agent.h           # ChatAgent 头文件（可选，也可内嵌）
├── ai_config.txt          # API Key 配置文件（.gitignore 中排除）
├── Release/
│   └── ai_config.txt      # 运行时配置
└── docs/
    └── AI_Chat_Agent_Design.md  # 本文档
```

### ai_config.txt 格式
```
API_KEY=your_api_key_here
API_ENDPOINT=https://open.bigmodel.cn/api/paas/v4/chat/completions
MODEL_NAME=glm-4-flash
```

---

## 8. 安全考虑

1. **API Key 不入库**：`ai_config.txt` 加入 `.gitignore`
2. **HTTPS 加密**：WinHTTP 使用 TLS 1.2+ 加密传输
3. **输入过滤**：用户输入限制长度，防止 prompt injection
4. **错误隔离**：API 调用失败不影响游戏主循环

---

## 9. 用户体验流程

1. 玩家进入游戏，正常玩
2. 遇到困难，按 **F1** 打开 AI 助手面板
3. 输入问题（中文/英文均可），按 Enter 发送
4. AI 基于当前游戏局面进行分析，给出策略建议
5. 回答以打字机效果逐字显示
6. 玩家可以继续追问或按 F1/ESC 关闭面板
7. 面板关闭后继续正常游戏
