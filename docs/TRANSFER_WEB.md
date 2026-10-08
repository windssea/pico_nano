# 中文传输网页与PC服务

0.0.41提供实际HTTP开发入口：网页把书籍、字体、封面、锁屏图片交给同源C上传事务安装。当前不是设备WiFi服务，也不实现LocalSend协议互通。0.0.42增加 [设备TF同步适配](DEVICE_TRANSFER_STORAGE.md)；设备热点、配对码画面与无线owner连接后续已装配；STA配网/局域网界面、图片应用和真实无线验证仍待完成。

## 启动与操作

先运行 `python tools/dev.py host`，生成Linux原生worker。以下PowerShell命令只在本机回环地址开放，使用独立、可丢弃的测试库，不与阅读模拟器共享根目录：

```powershell
docker run --rm --mount type=bind,source=D:\windssea\dev\pico_nano,target=/work -p 127.0.0.1:8787:8787 -w /work espressif/idf:v6.1@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c python3 tools/transfer_server.py --worker /work/build-host/pn_transfer_host --root /work/build-dev/transfer-library --bind 0.0.0.0 --port 8787 --authority 127.0.0.1:8787
```

打开 `http://127.0.0.1:8787`，输入控制台六位配对码，选择类别后添加文件并开始发送。网页支持桌面拖放、手机文件选择、暂停/继续和取消。同名文件需要确认旧文件身份后覆盖，也可在创建上传前改名；改名不修改本机原文件。上传字体或图片不会自动改变阅读字体、封面或锁屏。

书籍当前接受TXT/EPUB，字体接受TTF，图片接受PNG/JPEG。书籍最大512MiB，字体32MiB，图片8MiB；空间与未完成会话配额由原生文件port再次检查。PDF等计划格式尚不接受。

配对码五分钟失效，连续五次错误锁定一分钟。令牌只在网页内存中保存，停止服务后失效。刷新网页需重新配对、重新选择原文件；同一标签的sessionStorage只保存文件指纹和上传ID，不保存令牌或文件内容。续传查询必须与类别、名称、长度及完整摘要相符。

## 所有权与API

Python只验证HTTP、认证及请求边界；`simulator/transfer_host.c` 只转换有界IPC命令与回复，再交给 [有界工作任务](TRANSFER_WORKER.md)。所有会话切换、文件身份查询、写入、校验、覆盖、恢复和取消都由 [同源传输服务](TRANSFER_SERVICE.md) 通过 `pn_upload`/`pn_upload_files` 完成。任务使用固定测试介质身份和独立6MiB事务池。HTTP线程串行调用worker；不存在Python直接把请求写进正式书库的捷径。

| 接口 | 行为 |
| --- | --- |
| GET `/api/v1/status` | 公共服务信息与开发预览标记 |
| POST `/api/v1/pair` | 六位码换取当前服务令牌 |
| GET `/api/v1/session` | 检查当前令牌 |
| POST `/api/v1/uploads` | 创建随机ID上传；指定类别、名称、长度、SHA-256及可选旧文件身份 |
| GET `/api/v1/uploads/{id}` | 关闭/重新恢复原生事务，返回持久确认位置 |
| PUT `/api/v1/uploads/{id}/chunks` | 原始分块，X-Offset及X-Chunk-SHA256；最大64KiB |
| POST `/api/v1/uploads/{id}/complete` | 完整摘要和格式校验后安装 |
| DELETE `/api/v1/uploads/{id}` | 原生取消；已验证/已安装状态不能撤销 |
| GET `/api/v1/files/{kind}?name=…` | 查询旧文件长度与摘要，供明确覆盖确认 |

除公共信息与配对外均需 `Authorization: Bearer …`。修改请求必须带与服务authority完全相符的Origin，Host也须匹配；不提供跨域授权。服务拒绝重复关键头、分块编码、未知JSON字段、超长请求及路径名称。页面使用自托管资源与CSP，文件名通过textContent显示。

64KiB分块仅最后一块可短；完整文件与分块均使用SHA-256。网页在持久ACK后推进进度，最终校验/安装完成才显示“已保存”。网页使用module worker增量计算摘要，局域网HTTP不依赖要求安全上下文的WebCrypto。事务持久性及覆盖恢复见 [文件安装](UPLOAD_FILES.md) 与 [上传事务](UPLOAD_TRANSACTION.md)。

HTTP不提供链路加密；此入口默认仅本机测试，不能直接当公网服务。LAN部署仍需设备配对、无线生命周期与访问范围的完整接入。退出传输时不得以关闭TF挂载代替每块同步，也不能让阅读消费者与上传并行访问同一介质。

## 可复跑验证

`python tools/dev.py host` 中的transfer_http测试通过真实TCP及原生worker验证认证、Host/Origin、配对锁定/过期、坏参数、块重试、服务重启失效旧令牌、持久续传、明确覆盖、取消及TTF/PNG/JPEG安装后的完整字节一致性。更底层的掉电恢复、空间、格式损坏测试继续由upload_files/upload_resources覆盖。

浏览器摘要实现的独立oracle运行方式为 `node tests/tools/test_transfer_sha.mjs`，与Node crypto比较边界长度及不同分块大小。Node不属于固件运行依赖。

开发专用 `/qa` 页面增加合成演练TXT按钮，用于浏览器交互测试；普通首页无此按钮。演练只生成测试正文，不读取本机私人文件。PC浏览器已检查桌面及390px手机布局、改名、暂停续传与保存。模拟器、HTTP测试和截图均不能证明设备无线速度、功耗或墨水屏表现。
