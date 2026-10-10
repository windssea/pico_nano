# 界面组件与图标

所有页面共用 `components/pn_widgets`（主机、模拟器与固件同源），只绘制，不读写存储或硬件。坐标为 684×1216 逻辑像素，灰阶 4bpp（0 黑，15 白）。设计依据见 [界面与交互](UI_UX.md) 第1节。

## 视觉规则（Mono Glass V2）

- 令牌：`PN_UI_MUTED` 5（次要文字）、`PN_UI_STROKE` 11（描边/分隔）、`PN_UI_SELECT` 13（选中底、细分隔）、`PN_UI_SURFACE` 14（雾面卡片）；圆角 `PN_UI_RADIUS` 16、`PN_UI_CARD_RADIUS` 22、`PN_UI_CHIP_RADIUS` 28；最小命中 `PN_UI_HIT_MIN` 88。
- 文字：`pn_w_text_ex`/`pn_w_text_lines_ex` 指定墨色与加粗；墨色由字体的 `ink` 字段混合（0为黑），加粗为错开0.6 px重绘。
- 拟玻璃：`pn_w_glass` 画下沿一级灰影＋雾面＋细描边；`pn_w_card`/`pn_w_card_row` 组成分组卡片，`pn_w_group` 为分组标题。
- 按钮：主按钮深色填充白字（先画字再 `pn_w_invert_round`），次按钮雾面加描边，禁用为次要墨色加删除线；危险操作只描边。
- 分段 `pn_w_segments`：雾面槽内深色胶囊为当前项，可带第二行说明。开关 `pn_w_toggle`：开为深色轨道白钮，关为浅灰轨道描边白钮。
- 底栏 `pn_w_tabbar_icons`：图标在上、文字在下，当前项浅灰圆角底座与加粗标签。
- 状态带 `pn_w_status`：PicoNano、时间（`pn_w_set_clock`）、无线（`pn_w_set_wifi`）、电量（`pn_w_set_battery`），没有设置的项不画。顶栏 `pn_w_header` 内含状态带。

## 图标

`pn_w_icon(frame, icon, x, y, size, shade)`，线宽随尺寸（size/14，最小 2px），圆头等线宽：搜索、网格、列表、返回、箭头（右）、书架、传书、设置（滑杆）、目录、书签、排版（Aa）、刷新、关闭、加、减、前进箭头、字体、图片、锁、删除、对勾、WiFi、更多（⋯）、导入、进度；另有电池 `pn_w_battery`。图标表可用 `PN_ICON_SHEET=path.pgm build-host/test_icons` 导出审查。

## 绘制与命中

控件只负责绘制与坐标；命中函数与页面同文件（`pn_shelf_hit_ex`、`pn_search_ui_hit` 等），保证主机、模拟器和设备点击区一致。三键焦点由 `pn_focus` 统一提供：扫描页面命中函数得到可操作区域，KEY1/KEY3 移动、KEY2 确认并画圆角粗框焦点环；书架的 KEY2 焦点和阅读正文按键规则见 [界面与交互](UI_UX.md) 第7节。

主机截图与模拟器画面不能当作面板灰阶或观感的实测，真机验收前不宣称观感达标。
