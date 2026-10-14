> **历史归档（2026-10-14）**：本文的状态和方案只作历史证据，当前规格、实现与后续工作以[现行文档](../../README.md)为准。

# 整文件流式接收

`pn_transfer_receive` 是LocalSend接收的网络无关基础层，已在主机验证；不提供LocalSend HTTP路由、发现、TLS或设备接收确认。现有网页分块API继续要求预先提供完整SHA，不改变其公开请求契约。

## 接线与所有权

调用方先完成授权与接收确认，将媒体交给现有传输worker，然后提供执行回调和读取回调。执行回调原样转发给 `pn_transfer_worker_execute`（主机也可用串行service）；不创建另一个文件服务。读取回调遵守capacity，PN_OK返回正字节数，精确EOF返回PN_EMPTY和0；超时、取消、HTTP内容长度核对及任务退出由网络适配负责。

接收函数只分配一个64KiB块，将任意短读聚合成已有持久分块。网络调用方使用独立受限pool，不能跨线程借用UI池。它不直接写文件，也不复制第二份整文件：暂存、配额、同步、目录分类、内容格式校验和可恢复安装均由共享事务负责，位置仍为 `/.readpico/uploads`。四类资源目录见 [存储卡目录](SD_CARD_LAYOUT.md)。

`PN_TRANSFER_STREAM_BEGIN` 是内部操作：允许缺省摘要，拒绝覆盖。普通BEGIN仍要求摘要。流请求期间，service只接受同ID分块、完成、取消，阻止网页/其他请求切换文件owner；STOP保留既有停止屏障行为。网络读回调须检查停止信号，所有网络调用方退出后才能join/关闭worker并归还媒体。

## 完成与错误

收够声明长度后再确认EOF，提前EOF或额外字节均失败。共享owner读取精确暂存内容计算完整SHA，然后使用原完成流程再次核对内容、实际格式并安装。提供过SHA时仍须一致；缺省SHA仅免去发送方计算，不省去接收方校验。

`result.received` 是读入字节，不是持久ACK或续传位置。只有 `committed` 为真才确认安装；digest此时有效。`cleanup_pending` 表示新主已确认但安装备份待清理。

失败保留原错误，尝试持久取消并报告 `cancel_status`。已经开始安装（finish_started）而未确认成功时，不能推断目标不存在；若取消返回BUSY/IO等，保留记录并由owner恢复同ID完成或经过停止屏障处理。取消不会撤销VERIFIED/COMMITTED资源。调用失败后不继续读网络，不尝试重放同一读取流。

运行时串行门不代替持久日志：进程重启后仍靠PNUP/PNFI恢复。LocalSend客户端暂不支持网页式跨重启分块续传，下一次网络传输按新接收处理；孤立会话管理仍待实现。

## 验证入口

`python tools/dev.py host` 包含transfer_receive：跨64KiB、137字节短读、缺省/已知/错误SHA、截断、额外字节、取消、非法名称、低内存、持久记录分配失败恢复、同名拒绝、竞争请求、真实worker及TTF/PNG/JPEG分类安装。底层故障矩阵继续由upload_files/transfer_service等测试覆盖。设备网络、官方客户端与FAT掉电未验收。

主机样本驱动 `receive_resource_driver SOURCE KIND SAVE_NAME` 使用独立临时目标目录，源文件只读；KIND为1书籍/2字体/3封面/4锁屏。可在固定Docker镜像中运行：

```text
build-host/receive_resource_driver mockDoc/绍宋.epub 1 sample.epub
build-host/receive_resource_driver mockDoc/LXGWWenKai-Regular.ttf 2 sample.ttf
```

两份私人样本已有实际流式安装及原SHA一致证据；接收pool峰值65,568字节（单块及分配头），不含worker校验池、源读取库缓冲、网络/TLS/任务栈。该数字不是ESP32-S3总内存或无线吞吐测量。主机介质换代测试也验证：失效租约拒绝写入，重新交给同一卡的新owner后可恢复并取消未安装会话。
