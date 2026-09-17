# Mac 常驻开发服务

服务只持有一条 USB 会话。运行不同客户端不会重新打开设备端口，因此可避免
历史上每次 CLI 操作触发启动、把画面重置为启动页的问题。

在 SDK 根目录运行（当前已由 Codex 启动，无须重复启动）：

```sh
/usr/bin/python3 host/service.py serve --port /dev/cu.usbmodem11301
```

另一个终端查询或更新：

```sh
/usr/bin/python3 host/service.py call service.status
/usr/bin/python3 host/service.py call display.text --args '{"text":"Hello Metalio"}'
/usr/bin/python3 host/service.py watch examples/watch-scene.json
```

保存 scene JSON 后，watch 校验格式，下发并核对设备 RAM 哈希与版本；间隔
至少两秒。现有场景仅支持矩形；RAM 校验不代表墨水屏像素逐点验收。
设备端 C++ 变化仍需要编译和 app-only 刷入。

人在设备旁时运行一次屏幕和声音联合检查：

```sh
/usr/bin/python3 host/service.py check
```

应看到 `SCREEN + SOUND CHECK`，听到三个半秒提示音，临时音量 80。
完成后恢复先前音量，不修改持久化设置。

刷机或使用旧的直连 CLI 前先释放端口：

```sh
/usr/bin/python3 host/service.py call service.stop
```

服务未配置登录自启。重启 Mac 后手动启动；设备拔插后的恢复仍待物理验收。
断开/重启会使旧会话失效，未确认完成的请求不会自动重试。
日志默认 `sessions/service.jsonl`；本轮使用 `artifacts/dev10/service.jsonl`。
本地 socket 为 `/tmp/metalio-<uid>.sock`，权限 0600。不提供网络监听或刷写接口。
