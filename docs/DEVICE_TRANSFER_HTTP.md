# ESP-IDF设备HTTP上传适配

0.0.45提供 `pn_device_transfer_http`，使用真实esp_http_server接口、固定版本cJSON、构建嵌入网页及 [工作任务](TRANSFER_WORKER.md)。主机测试使用显式SDK接口stub及真实JSON、worker和文件事务；两套ESP-IDF配置编译真实SDK分支。当前主设备入口尚未调用此适配，WiFi生命周期、设备传输页和二维码仍待接通，尚未进行真实无线测试。

## 启动与停止

调用方先停止TF消费者、移交共享media并启动worker，再启动WiFi，最后以固定IP[:port] authority打开HTTP服务。生成配对码和令牌时要求ESP随机数源已有真实熵；不能把无无线时的测试随机数当成已验证的设备认证。服务固定使用端口对应的IPv4地址入口，不接受任意Host或跨域来源，mDNS别名尚未实现。

配对码由拒绝采样生成六位数字，五分钟过期；连续五次错误锁定一分钟。令牌为随机128位、仅内存保存。界面只能通过code API查询显示用配对码，日志不输出令牌。HTTP是本地网络通道，没有链路加密，不能直接作为公网服务。

request_stop先锁保护关闭认证、清令牌，再关闭worker接收入口，不等待完整格式校验。close调用httpd_stop等待HTTP处理结束，成功后才释放上下文；随后由调用方关闭worker并归还media。关闭失败保留对象供重试，不能提前释放平台ctx、卸载TF或恢复阅读。启动后注册失败也关闭接收；若HTTP停止失败则保留对象，调用方必须继续清理。

接收覆盖回调在socket接收前后检查停止状态，沿用SDK十秒socket超时，使停止后的未完成请求头也可退出接收路径。正文另有十秒总接收预算，超时或未完整接收不调用上传worker。真实SDK调度/网络下的停止上限仍须真机验证。

## 请求边界

API与 [PC网页服务](TRANSFER_WEB.md) 对齐：status、pair、session、新建上传、按ID恢复、64KiB块、完整安装、取消及按类别查询旧文件摘要。书籍当前TXT/EPUB、字体TTF、图片PNG/JPEG；上传后不自动切换字体或应用图片。

Host须与固定authority相符；修改请求必须有准确Origin，GET携带Origin时也须匹配。拒绝重复Host、Origin、Authorization、Content-Length、Content-Type及分块摘要/位置头，拒绝任何Transfer-Encoding。JSON最多8192字节，原始块最多65536字节；禁止不完整正文、未知/重复字段、嵌入NUL、超深/过多JSON结构、非整数范围和错误摘要。名称、扩展名及类别仍由共享服务再次验证。

错误响应关闭连接以免未消费正文成为下一条请求。授权先于私人数据操作；停止期间返回不可用。worker错误不输出推测ACK，已提交但网络响应丢失时按原ID查询恢复。新建上传成功回复丢失的孤立会话仍受既有八会话配额约束，自动按文件指纹找回该ID的接口尚未实现。

设备网页只嵌入普通首页及四个JS/CSS资源，不提供/qa演练入口。生成时将PC预览说明替换为设备连接说明，其余排版与交互共用原资源。文件名通过已有textContent显示；服务设置CSP、no-store和nosniff，所有资源自托管。

## 固定SDK维护边界

ESP-IDF公开get_hdr接口只返回首个匹配项，因此此适配读取固定6.1的私有 `httpd_req_aux` scratch/count布局来检查重复关键头；扫描有容量和头数上限。不修改SDK源码。main明确加入esp_http_server/src及port/esp32私有包含路径和http_parser依赖；升级SDK必须复查布局、解析生命周期并重跑测试，不能盲目移植。

[官方6.1迁移指南](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/migration-guides/release-6.x/6.0/protocols.html)说明JSON已移出SDK内置组件。本项目固定上游cJSON v1.7.19三个原文件及MIT许可，摘要由check_docs核对，使用同源主机/设备构建；没有引入浮动版本。JSON递归编译限16层，解析前另限制结构和字符串token总量64，避免以8192字节请求构造无限节点分配。解析器使用libc分配，未计入事务池峰值，完整内存门仍需设备测试。

## 证据与未完成工作

host中的device_transfer_http运行同一handler，SDK网络、时钟、随机数和信号量是显式stub，文件与事务worker真实。验证配对/401、重复关键头、锁定/过期、未知参数/NUL、正文超时/不完整、中文TXT完整安装、旧文件摘要、覆盖确认后取消、假TTF拒绝及停止；接收覆盖回调还用真实POSIX socketpair验证普通接收和停止拒绝。stub不能证明SDK解析器、socket调度或WiFi已在设备通过。

后续接入AP/STA、设备传输页、二维码、停止回原阅读位置，再进行实际手机浏览器、拔卡/关闭、任务栈、PSRAM、吞吐、断电与功耗测试。完整第一版目标还包括字体管理、封面应用、锁屏图片激活及格式/性能门，不能以本阶段编译或主机测试代替产品验收。
