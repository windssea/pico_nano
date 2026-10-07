# 原生EPUB导航会话与显示回执

0.0.25加入pn_epub_reader，共享前后页、跨章、语义跳转与重排后的导航状态。它使用原出版物/XHTML/页节点，不把EPUB转成TXT。当前是已测试的会话层，尚未接到设备/SDL产品EPUB交互界面，新语义载荷与保存策略已接入 [持久化核心](EPUB_PERSISTENCE.md)；不能称为完整EPUB续读或书签已交付。

## 所有权与确认

初始化借用出版物、pool、布局、度量、caller页缓冲和随机salt；外层验证内容SHA对应该源，并保证资源寿命。固定版式返回UNSUPPORTED，不套重排版路径。close只释放导航状态，不关闭源；外层必须先完成未保存进度的关闭屏障。

prepare只生成完整XHTML校验后的暂定节点，返回owner/token/ticket/intent/语义anchor。pending期间新请求BUSY；失败保留输出receipt和原visible，暂定page不可呈现。字体/图片/显示owner随后完整绘制所有图片及正文，实际呈现成功才complete(success=true)。page.valid不证明图片已解码，不能跳过 [图片资源](IMAGE_RESOURCES.md) 的ZIP CRC。

complete检查owner、布局代次、不重复ticket、原意图、路径与位置以及时间顺序；伪造/重复/过期回执不调用确认回调。失败显示、源失效不改变visible、历史或保存输入。成功后再更新visible并调用可选confirm(bookSHA+location,是否真实前后页,now)。若confirm写失败，返回错误但新页已经实际显示；外层须保留dirty及保存重试/关闭屏障，不能当作旧页仍在。progress/page getter重新检查借用源的有效媒体代次；无显示progress为EMPTY，错误保持输出。

## 原生位置

location包含规范spine资源path、locator版本1、kind/element/run/Unicode标量offset，另有chapter_start标志供资源首请求。chapter_start时位置字段全零；显示确认位置来自首实际节点或有意义的空白事件，chapter_start=false。不存永久页号、ZIP本次索引或spine序号。保存载荷的版本/路径/内容SHA及UTF8规范见 [PNEP载荷](EPUB_PERSISTENCE.md)，产品owner仍须装配恢复/保存屏障。

FIRST从首个linear资源开始，NEXT沿当前下一页，资源末尾转下一个linear并跳过空资源；显式JUMP可以进入linear=no的目录资源。下一资源的存在不保证有正文，has_next可以表示候选资源；实际NEXT决定到末尾的EMPTY。JUMP非章节首位置必须在完整校验的正文事件中存在；缺失位置INVALID、未知locator版本UNSUPPORTED，隐藏锚点UNSUPPORTED。id查询使用pn_xhtml_anchor，重复/缺失id行为保留。

## 上一页与重排

缓存最近32个成功向前翻页的首语义位置；失败不会推进历史。PREVIOUS有缓存时回到该位置，同章以前一当前位置作为硬终点。JUMP/FIRST清历史，reflow推进generation并丢pending/历史，保留已显示的原文位置，随后CURRENT重排。

冷PREVIOUS从当前资源首扫描，以当前visible.begin为语义stop，只生成它之前的节点，仍验证完整XHTML/CRC；当当前首字落在重排自然页中间时，上一页截为前缀，next仍是原visible.begin，不重复/漏掉当前首字。若资源内没有之前正文，找前一个linear资源的最后有效页并跳过空资源。尾部仅空白的资源不会丢掉前一有效页。

新增page.source_order/begin_order/next_order和pn_xhtml_order仅是本次完整事件流的临时顺序；不持久化。节点顺序是可见节点事件，order查询是该位置首次事件；带id的图片先有ANCHOR再有IMAGE时两者可能不同，不把它们无条件混为同一序号。父元素在子节点之后的文本可能element编号更小，必须按实际事件顺序处理，不能给locator元组排序。prepare_until的stop在start之前为INVALID，缺失stop不发布页；空前缀EMPTY。

当前冷上一页/最后一页每次从资源首重建，尚无分页索引、热页缓存、时间切片/deadline保证。逻辑正确不证明真实设备快速翻页已达标，后续优化必须保留原语义边界和完整校验规则。

## 验证与剩余工作

python tools/dev.py host的epub_reader_session使用自有真实ZIP/OPF/XHTML，度量与显示/保存回调模拟。覆盖延迟/失败显示、pending、伪造/重复/过期回执、源失效、分配失败、写失败后真实visible、空/非线性/跨章/书末、id跳转、父-子-父文本、重排/恢复中间位置的截断上一页，以及超出32历史窗口后的反向导航。坏XML/CRC不发布位置，结束pool used/live为0。它没有实际模拟器窗口/面板呈现，不冒充端到端阅读验收。

原生语义A/B与保存策略已有核心实现；接下来接EPUB逐书书签和最近阅读，再装配实际字体/图片/显示owner及中文目录/七项排版UI到SDL和设备。封面缓存、全字库回退、上传/传书/壁纸、PDF阶段及真机资源/速度验收仍在原第一版范围内。


实际应用与独立PC窗口见 [原生EPUB应用](EPUB_APP.md)；此前核心模拟显示边界不变，设备/书架和完整UI继续装配。
