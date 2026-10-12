# LocalSend PC HTTP 开发入口

此入口用于真实 TCP、UDP 发现、确认会话和原生安装事务联调。已提供 v2 的 info、register、prepare-upload、upload、cancel，以及可选 IPv4 组播公告/响应；尚未验证官方桌面/手机客户端完整互通。它是独立 PC 明文 HTTP 服务，未接入阅读模拟器的传输页面，也不是 ESP32-S3 无线固件入口。设备 HTTPS、设备发现与中文图形确认/进度界面仍待实现。

## 启动与确认

先运行 `python tools/dev.py host`，生成 Linux 可执行程序 `build-host/pn_localsend_host`。PowerShell 中启动交互容器：

```powershell
docker --context desktop-linux run --rm -it --mount type=bind,source=D:\windssea\dev\pico_nano,target=/work -p 127.0.0.1:53317:53317 -w /work pico-nano-sdl:dev python3 tools/localsend_server.py --worker /work/build-host/pn_localsend_host --root /work/build-dev/localsend-library --bind 0.0.0.0 --port 53317
```

容器映射仅开放本机回环地址。测试库必须独立于正在阅读的模拟器书库和原始 mockDoc；不同时运行其他文件消费者。Linux/WSL 已编译 host 时可直接运行同样的 Python 命令，使用该环境的实际可执行路径，默认 bind 为 127.0.0.1。

### Windows 本机网络与 UDP 发现

Docker Desktop 端口映射不能当成局域网组播验证。可让 Python 直接在 Windows 网卡上处理 HTTP/UDP，只把原生文件 owner 放进 Docker。先构建 host 并确认 `pico-nano-sdl:dev` 镜像可用；测试根必须在工作目录内部：

```powershell
python tools/localsend_server.py --docker-worker --root build-dev/localsend-library --bind 0.0.0.0 --port 54417 --discovery-interface 192.168.50.173 --discovery-port 54417
```

示例网卡地址须替换为本机实际局域网 IPv4。默认 HTTP/UDP 端口均为 53317；示例使用自定义 54417，是为了避开部分 Windows/Hyper-V 的 UDP 保留范围。客户端的发现端口必须匹配；HTTP 实际端口随公告发送。需要相同端口的客户端可将两者都设为 54417。本入口不修改官方客户端设置、防火墙或系统端口保留。

报 WinError 10013 时先只读检查 `netsh interface ipv4 show excludedportrange protocol=udp`，再选择未保留端口；端口占用另用 `Get-NetUDPEndpoint` 检查。绑定或加入组播失败会关闭本次服务和 owner，错误不会被当成“发现已成功”。需要纯 HTTP 时去掉发现参数，保留默认本机回环绑定。

发现明确选择一张网卡，使用 224.0.0.167、TTL=1，启动时发送 announce=true。只对有效局域网 IPv4 来源的 announce=true 作组播 announce=false 回退，不回连远端 HTTP、不处理下载。自指纹、响应报文、重复字段、非 2.x、坏 UTF8、非法大小/端口和超长报文均忽略。每报文最多 2048 字节；每来源一秒最多一次，全局每秒四次、最多四次突发；来源缓存最多 32 项、30 秒过期。收到发现信息不等于接收文件授权。

收到 prepare-upload 后，本机控制台展示发送方、文件名称、大小和确认编号，等待 60 秒。逐文件输入选择，例如两文件输入 `13`：

| 选择 | 行为 |
| --- | --- |
| 0 | 拒绝该文件 |
| 1 | 接收书籍或字体，目录由实际类别决定 |
| 3 | 将图片作为封面安装到 covers |
| 4 | 将图片作为锁屏图片安装到 wallpapers |

图片不能使用选择 1；书籍和字体不能靠图片选择改变目录。全拒绝或超时不发授权。发送方名称只是申报信息，不能视为认证。确认编号绑定当前请求；本机控制台是开发工具，不通过 HTTP 提供远程确认接口。Ctrl+C 停止入口、请求活动接收取消、等待请求线程退出，再关闭原生 owner。

## 网络与文件边界

### 本机图形确认

在上述 Python 服务命令中增加 `--control`，即可用浏览器替代控制台输入。Windows 示例：

```powershell
python tools/localsend_server.py --docker-worker --root build-dev/localsend-library --port 54417 --control
```

打开启动日志打印的“本机图形确认”地址，逐文件选择图书/字体、封面或锁屏；默认全部不接收。确认与拒绝仍进入原生 `pn_ls_decide`，绑定原请求代次。界面显示接收字节数、已安装/未安装计数，支持取消；接收量达到100%仍需等待校验与安装回执。页面刷新保留已确认用途，服务停止后旧权限失效。取消已接纳不表示撤销已安装资源。

控制入口独立绑定127.0.0.1随机端口，最多4个处理线程；每次启动使用随机权限令牌，地址片段只在本机浏览器读取，之后从地址栏移除。状态/API需要Bearer令牌，修改还检查精确Host及同源Origin，不对局域网发送端开放确认权限。展示快照不包含协议会话/文件授权令牌；发送方和文件名以文本显示，不作为HTML执行。

这是PC开发控制工具，使用HTML界面；共用C界面组件及SDL演练尚未连接真实网络，设备接线仍待完成。与官方客户端完整互通、设备TLS不能以这个入口的测试代替。

- 每个 HTTP 请求关闭连接。拒绝重复长度头、Transfer-Encoding、重复查询参数、过长地址和超限元数据；正文长度须与已授权大小相同。元数据最多 8192 字节，文件最多 8 个，会话总量最多 512 MiB；类别大小限制沿用共享安装层。
- 实际 socket 地址绑定授权，不信任 JSON 地址或 X-Forwarded-For。服务有 8 个请求线程槽，单个确认会话、单个文件流；并发文件请求返回 409。官方客户端的多文件并发兼容仍需联调，不宣称全部支持。
- 网络正文空闲上限 10 秒，总时限 30 分钟；取消按 250 ms 轮询收敛，文件校验/安装期间等待 native owner 返回。取消响应表示取消请求已接纳，不代表已撤销安装；已安装文件保留。
- 缺省 SHA 时 owner 计算完整 SHA，声明过 SHA 则核对。安装成功才回复上传 200；摘要或实际资源校验失败回复 422。收到字节或内部管道帧回执都不是持久安装成功。
- 同名拒绝覆盖；截断/取消的未安装文件不能出现在书架。路径、资源校验、配额、空间和安装恢复复用现有 worker，HTTP/Python 不写暂存或最终文件。目录见 [存储卡目录](SD_CARD_LAYOUT.md)。
- 不具备网页分块的跨重启续传，断线采用重新发起。原生管道损坏、意外退出或收流中 owner 错误会停止该原生进程；本机服务需停止后重新启动，不自动丢弃未确认事务。恢复契约见 [整文件接收](TRANSFER_RECEIVE.md)。
- 此入口未启用 PIN、TLS或公网发布；UDP 组播仅明确传入网卡时开启。不向第三方回连，不下载发送方预览；register 返回本机信息但不保存远端发现列表。指纹为启动时安全随机值，HTTP 不提供链路加密。

## 可复跑测试

`python tools/dev.py host` 包含 `localsend_host` 和 `localsend_http`：真实子进程、TCP、中文 ID、旧确认、错误来源/令牌、拒绝、重复上传、长度/摘要失败、图片分类、字体安装及正文中的并发取消。所有输出在临时独立根，测试由可信本机测试操作者显式确认，不是自动接收产品策略。

可在同一 Linux/容器环境补测本机私有资源：

```bash
python3 tests/host/test_localsend_http.py build-host/pn_localsend_host mockDoc/绍宋.epub mockDoc/LXGWWenKai-Regular.ttf
```

测试只读原文件，通过 HTTP 传输后比较安装文件的完整 SHA。主机安装成功不能证明官方客户端发现、真实 WiFi 吞吐、设备内存、掉电或功耗。

Windows Python + Docker owner 可复跑同一 HTTP 测试：

```powershell
python tests/host/test_localsend_http.py --docker-worker mockDoc/绍宋.epub mockDoc/LXGWWenKai-Regular.ttf
python tests/host/test_localsend_discovery.py -v
```

默认发现测试不发送 LAN 组播，验证报文、限速、真实回环 UDP 和停止。真实网卡组播另需显式指定环境：

```powershell
$env:PN_LS_TEST_INTERFACE='192.168.50.173'
$env:PN_LS_TEST_PORT='54417'
python tests/host/test_localsend_discovery.py -v
```

此项验证同机经真实网卡发送的启动公告与回退响应，不代表另一台设备、路由器转发或官方客户端完整传输已验收。停止时先停发现并 join 线程，再等待 HTTP 请求和原生 owner 收口；无后台残留服务。

原生桥接维护边界：仅可信本机适配器调用 PREP/DECIDE/AUTH/CANCEL/LOCAL_CANCEL/STATUS/EXIT；AUTH 成功且 STREAM_BEGIN 成功才发 body 提示。DATA 单帧最多 8192 字节，可跨原生 64 KiB 块边界；EOF/ABORT 收口后才返回最终回执。控制命令严格串行；96 KiB 接收 pool 与 6 MiB 文件 worker 分离，不能把这些预算之和当完整进程或设备内存峰值。
