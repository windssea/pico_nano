# LocalSend PC HTTP 开发入口

此入口用于真实 TCP、确认会话和原生安装事务联调。已提供 v2 的 info、register、prepare-upload、upload、cancel；尚未验证官方桌面/手机客户端互通。它是独立 PC 明文 HTTP 服务，未接入阅读模拟器的传输页面，也不是 ESP32-S3 无线固件入口。UDP 发现、设备 HTTPS、中文图形确认/进度界面仍待实现。

## 启动与确认

先运行 `python tools/dev.py host`，生成 Linux 可执行程序 `build-host/pn_localsend_host`。PowerShell 中启动交互容器：

```powershell
docker --context desktop-linux run --rm -it --mount type=bind,source=D:\windssea\dev\pico_nano,target=/work -p 127.0.0.1:53317:53317 -w /work pico-nano-sdl:dev python3 tools/localsend_server.py --worker /work/build-host/pn_localsend_host --root /work/build-dev/localsend-library --bind 0.0.0.0 --port 53317
```

容器映射仅开放本机回环地址。测试库必须独立于正在阅读的模拟器书库和原始 mockDoc；不同时运行其他文件消费者。Linux/WSL 已编译 host 时可直接运行同样的 Python 命令，使用该环境的实际可执行路径，默认 bind 为 127.0.0.1。

收到 prepare-upload 后，本机控制台展示发送方、文件名称、大小和确认编号，等待 60 秒。逐文件输入选择，例如两文件输入 `13`：

| 选择 | 行为 |
| --- | --- |
| 0 | 拒绝该文件 |
| 1 | 接收书籍或字体，目录由实际类别决定 |
| 3 | 将图片作为封面安装到 covers |
| 4 | 将图片作为锁屏图片安装到 wallpapers |

图片不能使用选择 1；书籍和字体不能靠图片选择改变目录。全拒绝或超时不发授权。发送方名称只是申报信息，不能视为认证。确认编号绑定当前请求；本机控制台是开发工具，不通过 HTTP 提供远程确认接口。Ctrl+C 停止入口、请求活动接收取消、等待请求线程退出，再关闭原生 owner。

## 网络与文件边界

- 每个 HTTP 请求关闭连接。拒绝重复长度头、Transfer-Encoding、重复查询参数、过长地址和超限元数据；正文长度须与已授权大小相同。元数据最多 8192 字节，文件最多 8 个，会话总量最多 512 MiB；类别大小限制沿用共享安装层。
- 实际 socket 地址绑定授权，不信任 JSON 地址或 X-Forwarded-For。服务有 8 个请求线程槽，单个确认会话、单个文件流；并发文件请求返回 409。官方客户端的多文件并发兼容仍需联调，不宣称全部支持。
- 网络正文空闲上限 10 秒，总时限 30 分钟；取消按 250 ms 轮询收敛，文件校验/安装期间等待 native owner 返回。取消响应表示取消请求已接纳，不代表已撤销安装；已安装文件保留。
- 缺省 SHA 时 owner 计算完整 SHA，声明过 SHA 则核对。安装成功才回复上传 200；摘要或实际资源校验失败回复 422。收到字节或内部管道帧回执都不是持久安装成功。
- 同名拒绝覆盖；截断/取消的未安装文件不能出现在书架。路径、资源校验、配额、空间和安装恢复复用现有 worker，HTTP/Python 不写暂存或最终文件。目录见 [存储卡目录](SD_CARD_LAYOUT.md)。
- 不具备网页分块的跨重启续传，断线采用重新发起。原生管道损坏、意外退出或收流中 owner 错误会停止该原生进程；本机服务需停止后重新启动，不自动丢弃未确认事务。恢复契约见 [整文件接收](TRANSFER_RECEIVE.md)。
- 此入口未启用 PIN、TLS、UDP 组播或公网发布。不向第三方回连，不下载发送方预览；register 返回本机信息但不保存远端发现列表。指纹为启动时安全随机值，HTTP 不提供链路加密。

## 可复跑测试

`python tools/dev.py host` 包含 `localsend_host` 和 `localsend_http`：真实子进程、TCP、中文 ID、旧确认、错误来源/令牌、拒绝、重复上传、长度/摘要失败、图片分类、字体安装及正文中的并发取消。所有输出在临时独立根，测试由可信本机测试操作者显式确认，不是自动接收产品策略。

可在同一 Linux/容器环境补测本机私有资源：

```bash
python3 tests/host/test_localsend_http.py build-host/pn_localsend_host mockDoc/绍宋.epub mockDoc/LXGWWenKai-Regular.ttf
```

测试只读原文件，通过 HTTP 传输后比较安装文件的完整 SHA。主机安装成功不能证明官方客户端发现、真实 WiFi 吞吐、设备内存、掉电或功耗。

原生桥接维护边界：仅可信本机适配器调用 PREP/DECIDE/AUTH/CANCEL/LOCAL_CANCEL/STATUS/EXIT；AUTH 成功且 STREAM_BEGIN 成功才发 body 提示。DATA 单帧最多 8192 字节，可跨原生 64 KiB 块边界；EOF/ABORT 收口后才返回最终回执。控制命令严格串行；96 KiB 接收 pool 与 6 MiB 文件 worker 分离，不能把这些预算之和当完整进程或设备内存峰值。
