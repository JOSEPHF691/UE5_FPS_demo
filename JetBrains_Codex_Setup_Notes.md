# JetBrains Codex Agent 配置记录

## 概要

本次会话的目标是：在 JetBrains Rider 中配置一个自定义 Codex ACP Agent，通过 OpenAI 兼容中转站接入：

```text
https://ai.dongli.work/v1
```

这样可以让 Rider 里的 Codex 体验接近当前 VS Code 中的 Codex 插件，同时绕开 JetBrains 内置 Codex 链路中出现的错误：

```text
unexpected status 404 Not Found
url: http://127.0.0.1:50000/responses
```

## 排查结果

- Rider 自己会在本机启动一个 OpenAI 代理服务，地址是 `127.0.0.1:50000`。
- 之前失败的请求来自 JetBrains 内置 `Codex` / `JETBRAINS_AI` 链路，不是来自中转站。
- 已直接验证中转站接口：
  - `/v1/models` 返回 `200 OK`。
  - `/v1/responses` 返回 `200 OK`。
- 第一次生成的 ACP 配置文件失败，是因为 PowerShell 把 `acp.json` 写成了带 BOM 的 UTF-8。
- Rider 不接受该编码，日志中出现：

```text
acp.json is invalid: Unexpected JSON token at offset 0
Expected start of the object '{', but had '﻿'
```

## 已完成的修改

创建了 JetBrains ACP 配置文件：

```text
C:\Users\joseph\.jetbrains\acp.json
```

其中定义了一个本地 ACP Agent：

```text
codex-dongli
```

该 Agent 使用：

```text
npx -y @agentclientprotocol/codex-acp@0.0.44 -acp
```

并配置了以下环境变量：

```text
OPENAI_BASE_URL=https://ai.dongli.work/v1
OPENAI_MODEL=gpt-5.3-codex
OPENAI_API_KEY=<redacted>
```

配置文件已重新写为不带 BOM 的 UTF-8，因此 Rider 可以正常解析。

## 当前状态

配置已经生效。Rider 现在可以看到并使用 `codex-dongli` 这个 Agent，消息不再进入之前的 `127.0.0.1:50000/responses` 重连失败循环。

JetBrains 的模型下拉菜单中可以看到以下模型族：

```text
GPT-5.5
gpt-5.4
GPT-5.4-Mini
gpt-5.3-codex
gpt-5.2
```

同时可以选择推理强度：

```text
low
medium
high
xhigh
```

日常编码推荐选择：

```text
gpt-5.3-codex + medium
```

复杂重构或疑难调试时可以选择：

```text
gpt-5.3-codex + high
```

## 与 VS Code 中 Codex 的区别

VS Code 中的 Codex 配置更接近原生 Codex 插件或 CLI 体验。

JetBrains 当前链路是：

```text
Rider AI Chat
-> ACP Agent: codex-dongli
-> @agentclientprotocol/codex-acp
-> https://ai.dongli.work/v1
-> 中转站实际选择的后端模型
```

因此，两边的核心编程 Agent 能力接近，但 JetBrains 多了一层 ACP 抽象。模型名称、模型身份回答、以及中转站实际路由到的后端模型，不一定完全一致。

## 注意事项

- 如果内置 `Codex` 仍然出现 `127.0.0.1:50000/responses` 的 404 错误，就不要使用它。
- 在 Agent 下拉菜单中选择 `codex-dongli`。
- API key 没有写入本文档，文档中只保留了脱敏占位符。
- 由于 key 曾在聊天中出现过，如果中转站账号比较敏感，建议后续轮换一次 key。
- 如果手动编辑 `acp.json`，请保存为不带 BOM 的 UTF-8。

