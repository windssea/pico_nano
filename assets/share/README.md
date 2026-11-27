# PicoNano 共享素材目录 / PicoNano Shareable Assets

本目录用于存放可在网页安装器在线预览、单文件下载，并自动同步到 Gitee 国内加速下载页（`share-assets` Release）的共享素材。

## 目录结构（与设备存储卡分类一一对应）

| 目录 | 类型 | 对应设备存储卡目录 | 支持格式 | 网页预览方式 |
| --- | --- | --- | --- | --- |
| `image/mask/` | 透明遮罩（阅读遮罩，建议 684×1216） | `/piconano/masks/`（传书「阅读遮罩」） | `.png`（含 Alpha 通道） | 模拟墨水屏阅读正文叠加预览 / 透明棋盘格切换 |
| `image/wallpaper/` | 锁屏壁纸（建议 684×1216） | `/piconano/wallpapers/`（传书「锁屏壁纸」） | `.png` / `.jpg` / `.jpeg` | 模拟阅读器整屏边框预览 |
| `image/background/` | 阅读背景（正文纸纹底图，建议 684×1216） | `/piconano/backgrounds/`（传书「阅读背景」） | `.png` / `.jpg` / `.jpeg` | 叠加阅读正文示例预览 |
| `image/banner/` | 首页横幅（建议 684×146） | `/piconano/banners/`（传书「首页横幅」） | `.png` / `.jpg` / `.jpeg` | 模拟首页「读书」顶部横幅预览 |
| `image/cover/` | 书籍封面（建议 184×256） | `/piconano/covers/`（传书「书籍封面」） | `.png` / `.jpg` / `.jpeg` | 3:4 书封卡片预览 |
| `font/` | 开源/可再分发字体 | `/piconano/fonts/`（传书「字体」） | `.ttf` / `.otf` | 浏览器加载字体实时示例预览 |
| `book/` | 公版/开源电子书 | `/piconano/books/`（传书「书籍」） | `.epub` / `.txt` | 书名、格式与大小卡片 + 下载 |

## 添加与自动同步流程

1. 直接将可合法公开分发的素材文件放入上方对应子目录（如新增锁屏壁纸放入 `assets/share/image/wallpaper/`，开源字体放入 `assets/share/font/`，公版书放入 `assets/share/book/`）。
2. 运行 `python tools/installer_site.py --write-share-manifest` 更新 `assets/share/manifest.json` 后提交并推送到 `main` 分支：
   - `.github/workflows/installer-pages.yml` 会自动扫描本目录，生成预览站点、分类打包 ZIP（`PicoNano-share-<分类>.zip`）与 `assets/share/manifest.json`，发布到网页安装器。
   - `.github/workflows/gitee-share.yml` 会自动将更新后的分类 ZIP、词典包与清单同步到 Gitee `windssea/pico_nano` 的 `share-assets` 发行版，并做匿名下载 SHA-256 校验，供国内用户高速下载。
   - 传书页「在线素材库」会通过国内 CDN / 官方站点自动读取 `assets/share/manifest.json`，支持在线预览与一键导入 PicoNano。
3. 本地预览：`python tools/installer_site.py --serve 8765`；本地校验 Gitee 同步包：`python tools/sync_gitee_share.py`。
