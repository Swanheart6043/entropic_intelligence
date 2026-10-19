# 云端 API IPC

业务进程不得直接拼接云端 URL 或 `/robotboot` 路径。同步查询和异步事件统一连接：

```text
ipc://@cloud_api_rpc
```

协议使用 ZeroMQ REQ/REP，多帧格式如下：

```text
Frame 1: JSON 请求信封
Frame 2: 可选二进制内容（图片）
```

请求信封：

```json
{
  "version": 1,
  "request_id": "32位十六进制字符串",
  "operation": "get_server_time",
  "timeout_ms": 7000,
  "payload": {},
  "has_binary": false
}
```

当前操作：

- `get_server_time`：同步请求云端时间，成功结果为 `result.timestamp`。
- `enqueue_alarm_report`：原子写入本地 outbox 后回复 `accepted=true`。
- `enqueue_capture_report`：原子写入本地 outbox 后回复 `accepted=true`。

事件写入 outbox 后由 `web_gateway` 完成图片上传、保存 `picUrl` 检查点并发送 WS/WSS。每个事件独立指数退避（约 5～300 秒），不会因单条事件退避而暂停整个队列；确定不可重试的事件改为 `.failed` 死信文件。死信、普通抓拍和残留文件按 [配置](配置.md) 中的保留策略清理。

outbox 使用 `send_message_and_wait()` 等待最多 5 秒的本地 socket 写入完成，失败或超时保留事件重试，并记录 `SPDLOG_INFO`。只有本地写入成功才把 `.json` 原子改名为 `.sent`，再删除图片和标记；删除失败时 `.sent` 由周期清理接管，避免重发。上传成功的 `pic_url` 检查点可用于重试，避免正常情况下重复上传图片。

云端没有事件确认机制，因此这里的“写入完成”不表示云端业务已经收到、保存或处理。写入结果不确定、发送完成后进程退出但尚未标记 `.sent` 等窗口可能重复上报，也无法排除本地写入成功后云端未收到的情况；不依赖云端 ACK，也不承诺恰好一次送达。

下行回调及实时状态上报继续使用非阻塞 `send_message()`，返回值只表示本地提交是否成功，异步失败同样记录 INFO。它不自动缓存实时状态或重放下行回复。不得在 WebSocket I/O 回调中调用阻塞等待接口。

错误响应统一包含：

```json
{
  "ok": false,
  "accepted": false,
  "upstream_status": 503,
  "error": {
    "code": "upstream_http_error",
    "message": "...",
    "retryable": true
  }
}
```

REQ socket 在超时后不得复用。公共 `CloudGatewayClient` 每次调用创建独立 socket，避免 EFSM 状态污染。
