# Error Frames & Handling Guide

本文档描述了服务器在拒绝/保护场景下返回的标准错误帧（msgType + body），便于客户端区分原因、做出正确的重试/告警策略。

## 错误帧一览

| msgType | body             | 场景/原因                                   | 客户端建议             |
| ------- | ---------------- | ------------------------------------------ | ---------------------- |
| `65000` | `ip_conn_limit`  | 单 IP 连接数超过 `ipLimit.maxConnPerIp`    | 立即断开，延时重连或更换源 IP |
| `65001` | `ip_qps_limit`   | 单 IP QPS 超过 `ipLimit.maxQpsPerIp`       | 降低速率，稍后重试     |
| `65002` | `inflight_limit` | 全局 in-flight 超限（服务过载保护）        | 短暂退避后重试，监控告警 |
| `65003` | `msg_rate_limit` | 按 msgType 的限流命中（QPS/并发/背压拒绝） | 不立即重试或退避重试；业务可降级 |
| `65535` | `backpressure`   | 背压状态下丢弃低优先级消息                 | 降低流量，等待背压解除 |
| `65004` | `format_error`   | 反序列化失败（JSON/Proto 格式错误）        | 检查请求格式，修正后再发 |

> 说明：上述 msgType/body 默认为 `config.lua` 中 `errorFrames` 的默认值，如有调整请同步此表与客户端。

## 处理建议

- **重试策略**：对 ip/限流类错误应退避重试，避免立刻重试导致雪崩；对 backpressure/inflight 超限优先减流或降级。
- **告警建议**：`inflight_limit`/`backpressure` 触发时应接入运维告警，排查资源瓶颈或调高容量。
- **客户端兼容**：客户端应按 msgType 分支处理，避免仅凭 body 文本。建议在日志中打出错误码 + 体，以便排障。
