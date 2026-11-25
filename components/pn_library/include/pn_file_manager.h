/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：存储卡文件管理（从传书页“存储卡”进入）：浏览标准目录、图书建夹与移动、受保护文件拦截、删除二次确认与loading遮罩；绘制/命中与文件操作严格分离。
 * English: TF card file manager (entered from Transfer Hub "Storage"): browse standard folders, create/move book folders, block protected files, two-step delete confirmation and loading overlay; drawing/hit-testing are strictly separated from file I/O.
 * 冻结：仅限 pn_sd_layout.h 公开目录，不暴露内部 .readpico，不删除系统保留字体；写操作须在调用方归还只读租约并持有 PN_MEDIA_WRITE 后执行。
 * Frozen: restricted to public folders in pn_sd_layout.h, never exposing .readpico or deleting the protected system font; writes require the caller to release read leases and hold PN_MEDIA_WRITE.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pn_font.h"
#include "pn_frame.h"
#include "pn_keyboard.h"
#include "pn_scan_dirs.h"
#include "pn_storage.h"
#include "pn_sd_layout.h"

#define PN_FM_CAT_BOOKS 0u ///< 图书目录 / Books directory
#define PN_FM_CAT_FONTS 1u ///< 字体目录 / Fonts directory
#define PN_FM_CAT_WALLPAPERS 2u ///< 锁屏壁纸目录 / Lock-screen wallpapers directory
#define PN_FM_CAT_BACKGROUNDS 3u ///< 阅读背景目录 / Reading backgrounds directory
#define PN_FM_CAT_COVERS 4u ///< 封面原图目录 / Book covers directory
#define PN_FM_CAT_BANNERS 5u ///< 首页横幅目录 / Home top banners directory
#define PN_FM_CAT_MASKS 6u ///< 阅读遮罩目录 / Reading lock-masks directory
#define PN_FM_CAT_COUNT 7u ///< 标准目录总数 / Number of standard public directories

#define PN_FM_PAGE_SIZE 9u ///< 每页最多展示项数 / Maximum visible entries per page
#define PN_FM_BROWSE_PAGE_SIZE 8u ///< 外部目录浏览页每页目录数 / Folders per page in external directory browser
#define PN_FM_NAME_MAX 128u ///< 单文件名上限 / Single entry name capacity
#define PN_FM_SUBPATH_MAX 128u ///< 子目录名上限 / Subfolder name capacity
#define PN_FM_PATH_MAX 512u ///< 完整路径上限 / Full path capacity
#define PN_FM_PRESET_COUNT 8u ///< 新建文件夹预设数量 / Quick preset folder names

/// 当前界面状态。/ Active screen state.
typedef enum {
    PN_FM_VIEW_ROOT=0, ///< 顶层 5 个标准目录与外部目录入口 / Top-level 5 standard directories and external scan-dir entry
    PN_FM_VIEW_LIST=1, ///< 目录文件列表 / Directory file listing
    PN_FM_VIEW_ACTIONS=2, ///< 单个文件操作面板 / File action bottom sheet
    PN_FM_VIEW_MOVE=3, ///< 移动图书目标目录选择 / Move-book destination folder picker
    PN_FM_VIEW_MKDIR=4, ///< 图书目录新建子文件夹 / Create subfolder under books
    PN_FM_VIEW_CONFIRM=5, ///< 删除二次确认与执行中遮罩 / Two-step delete confirmation and busy overlay
    PN_FM_VIEW_EXT_LIST=6, ///< 外部扫描目录管理页 / External scan directories manager
    PN_FM_VIEW_EXT_BROWSE=7 ///< 存储卡文件夹浏览选择页 / TF card directory browser for adding external folders
} pn_fm_view_t;

/// 命中码。/ Hit codes.
#define PN_FM_BACK 1 ///< 返回上一级 / Back to previous level
#define PN_FM_MKDIR_OPEN 2 ///< 打开新建文件夹页 / Open create-folder page
#define PN_FM_PREV 3 ///< 上一页 / Previous page
#define PN_FM_NEXT 4 ///< 下一页 / Next page
#define PN_FM_DELETE_EMPTY_DIR 5 ///< 删除当前空子文件夹 / Delete current empty subfolder
#define PN_FM_ACT_OPEN 6 ///< 打开图书阅读 / Open book in reader
#define PN_FM_ACT_MOVE 7 ///< 移动图书到文件夹 / Move book to folder
#define PN_FM_ACT_DELETE 8 ///< 进入删除确认 / Enter delete confirmation
#define PN_FM_ACT_CANCEL 9 ///< 关闭操作面板 / Close action sheet
#define PN_FM_CONFIRM_DELETE 10 ///< 确认删除 / Confirm deletion
#define PN_FM_CONFIRM_CANCEL 11 ///< 取消删除 / Cancel deletion
#define PN_FM_MKDIR_COMMIT 12 ///< 提交新建文件夹 / Create folder with entered name
#define PN_FM_MKDIR_CLEAR 13 ///< 清空输入 / Clear input text
#define PN_FM_EXT_OPEN 14 ///< 打开外部扫描目录管理页 / Open external scan directories manager
#define PN_FM_EXT_BROWSE 15 ///< 打开存储卡目录浏览器 / Open TF card folder browser
#define PN_FM_EXT_PICK 16 ///< 选定当前浏览目录加入扫描 / Add currently browsed folder to scan list
#define PN_FM_CAT_BASE 20 ///< 顶层目录命中基址（+0..4）/ Root category hit base (+0..4)
#define PN_FM_ITEM_BASE 40 ///< 列表行命中基址（+0..8）/ List row hit base (+0..8)
#define PN_FM_MOVE_BASE 60 ///< 移动目标命中基址（+0..8）/ Move target hit base (+0..8)
#define PN_FM_PRESET_BASE 80 ///< 预设文件夹名基址（+0..7）/ Preset folder name base (+0..7)
#define PN_FM_EXT_CAT_BASE 100 ///< 外部目录类别切换基址（+0..3：图书/字体/壁纸/背景）/ External dir category tab base (+0..3)
#define PN_FM_EXT_DEL_BASE 110 ///< 移出已登记外部目录基址（+0..3）/ Remove registered external dir base (+0..3)
#define PN_FM_BROWSE_BASE 120 ///< 浏览器目录行基址（+0..7）/ Folder browser row base (+0..7)
#define PN_FM_KB_SHIFT 190 ///< 屏幕键盘大小写 / On-screen keyboard shift
#define PN_FM_KB_LAYER 191 ///< 屏幕键盘符号层 / On-screen keyboard symbol layer
#define PN_FM_KB_DEL 192 ///< 屏幕键盘退格 / On-screen keyboard backspace
#define PN_FM_KB_BASE 200 ///< 屏幕键盘字符基址（+ASCII）/ On-screen keyboard character base (+ASCII)
#define PN_FM_KB_IME_BASE 340 ///< 340..400映射全拼中文输入法功能/候选/标点码 / 340..400 map Chinese Pinyin IME function/candidate/punctuation codes

/// 当前正在使用的资源路径，用于在列表中标注“使用中”。/ Currently active resource references used to badge in-use files.
typedef struct {
    const char *current_book; ///< 当前/最近打开的图书完整路径，可NULL / Active or last-read book full path, nullable
    const char *system_font; ///< 当前系统字体完整路径，可NULL / Active system font full path, nullable
    const char *reading_font; ///< 当前全局阅读字体完整路径，可NULL / Active global reading font full path, nullable
    const char *lock_wallpaper; ///< 当前自定义锁屏壁纸原图路径，可NULL / Active custom lock wallpaper source path, nullable
    const char *lock_mask; ///< 当前阅读/锁屏遮罩原图路径，可NULL / Active lock/reading mask source path, nullable
    const char *home_banner; ///< 当前主页顶部背景文件名，可NULL / Active Home top banner filename, nullable
    const char *reading_bg; ///< 当前全局阅读背景文件名，可NULL / Active global reading background filename, nullable
} pn_fm_usage_t;

/// 单条目录或文件记录。/ Single directory or file entry.
typedef struct {
    char name[PN_FM_NAME_MAX]; ///< 条目名称 / Entry name
    uint64_t size; ///< 文件大小（目录为0）/ File size in bytes (0 for directories)
    bool is_dir; ///< 是否子目录 / True if directory
    bool in_use; ///< 当前是否正在使用 / True if currently referenced by active settings or book
    bool protected_item; ///< 是否系统保留不可删除 / True if protected system asset
    bool readable_book; ///< 是否支持直接打开阅读（TXT/EPUB）/ True if readable TXT/EPUB book
} pn_fm_entry_t;

/// 文件管理器状态（纯数据，绘制与命中只读）。/ File manager state (pure data; render and hit test are read-only).
typedef struct {
    char root[PN_FM_PATH_MAX]; ///< TF 卡挂载根（如 /sdcard）/ TF card mount root (e.g. /sdcard)
    char cat_dirs[PN_FM_CAT_COUNT][PN_FM_PATH_MAX]; ///< 5 个标准目录完整路径 / Full paths for the 5 standard directories
    char storage[96]; ///< 容量说明 / Storage capacity summary
    pn_fm_view_t view; ///< 当前视图 / Current view
    unsigned category; ///< 当前标准目录 0..4 / Current category 0..4
    char subpath[PN_FM_SUBPATH_MAX]; ///< books 内当前子文件夹名，根目录为空 / Current subfolder name under books, empty at root
    size_t root_counts[PN_FM_CAT_COUNT]; ///< 5 个标准目录各自条目数 / Entry counts for each root category
    size_t page; ///< 当前列表页码（0起）/ Current list page (0-based)
    size_t total; ///< 当前目录总条目数 / Total entries in current directory
    size_t count; ///< 当前页有效条目数 / Entries loaded on current page
    pn_fm_entry_t items[PN_FM_PAGE_SIZE]; ///< 当前页条目 / Current page entries
    pn_fm_entry_t selected; ///< 当前选中操作的文件或空目录 / Currently selected file or empty folder
    bool delete_current_dir; ///< 确认页针对当前空子文件夹 / Confirmation targets current empty subfolder
    bool deleting; ///< 正在执行删除/移动，显示 loading 遮罩并屏蔽点击 / Busy state showing loading overlay and blocking taps
    size_t move_page; ///< 移动目标列表页码 / Move destination page
    size_t move_total; ///< 移动目标总数（含 books 根目录）/ Move destination total (including books root)
    size_t move_count; ///< 当前页移动目标数 / Move destinations on current page
    char move_targets[PN_FM_PAGE_SIZE][PN_FM_SUBPATH_MAX]; ///< 目标子目录名，空串代表 books 根目录 / Target subfolder names; empty string means books root
    pn_keyboard_t keyboard; ///< 新建文件夹屏幕键盘状态 / On-screen keyboard state for mkdir
    char input[64]; ///< 新建文件夹输入名 / Folder name input buffer
    pn_scan_dirs_t scan_dirs; ///< 当前外部扫描目录快照（由控制层同步）/ Active external scan directories snapshot (synced by controller)
    unsigned ext_cat; ///< 外部目录页当前类别 0..3（pn_scan_cat_t）/ Active category tab in external dir manager
    char browse_rel[PN_SCAN_REL_MAX]; ///< 存储卡浏览器当前相对根路径（空串为TF根目录）/ Current TF browser relative path (empty = TF root)
    size_t browse_page; ///< 浏览器页码（0起）/ Browser page index
    size_t browse_total; ///< 当前目录下子文件夹总数 / Total subfolders at current browse location
    size_t browse_count; ///< 当前页子文件夹数 / Visible subfolders on current page
    char browse_items[PN_FM_BROWSE_PAGE_SIZE][PN_FM_NAME_MAX]; ///< 当前页子文件夹名 / Subfolder names on current page
    char notice[96]; ///< 底部状态提示 / Status notice banner
} pn_file_manager_t;

/// 初始化状态到顶层标准目录视图。/ Initialize state to the root categories view.
void pn_file_manager_init(pn_file_manager_t *fm,const char *root,const char *storage);
/// 可选覆盖单个标准目录的完整路径（供主机模拟器映射独立样本目录；设备路径使用 init 默认值）。
/// Optionally override one standard category directory path (used by the host simulator for sample roots; firmware uses init defaults).
void pn_file_manager_set_dir(pn_file_manager_t *fm,unsigned category,const char *dir_path);
/// 判断标准目录是否属于可跨目录移动的四类图片（锁屏壁纸/阅读背景/首页横幅/阅读遮罩）。
/// Check whether a standard category belongs to the four cross-movable image folders (wallpapers/backgrounds/banners/masks).
bool pn_file_manager_is_image_category(unsigned category);
/// 返回图片移动目标标识对应的中文名称，非图片目标返回 NULL。/ Return the Chinese display title for an image move target token, or NULL if not an image target.
const char *pn_file_manager_move_target_label(const char *target);
/// 返回标准目录相对路径（pn_sd_layout.h 常量）。/ Return the relative directory constant from pn_sd_layout.h.
const char *pn_file_manager_category_dir(unsigned category);
/// 返回新建文件夹预设名称。/ Return a preset subfolder name.
const char *pn_file_manager_preset_name(unsigned index);
/// 判断目标是否为系统保留不可删除文件。/ Check whether a file is a protected system asset.
bool pn_file_manager_is_protected(unsigned category,const char *subpath,const char *name);
/// 拼接当前目录或目录下条目的完整路径（name 可为 NULL 表示当前目录）。/ Build full path of current directory or an entry inside it (NULL name means current directory).
pn_status_t pn_file_manager_build_path(const pn_file_manager_t *fm,const char *name,char *out,size_t cap);
/// 统计 5 个标准目录条目数（需持有 READ 租约）。/ Count entries in each of the 5 standard directories (requires READ lease).
pn_status_t pn_file_manager_scan_roots(pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease);
/// 扫描当前目录当前页（需持有 READ 租约）；usage 可为 NULL。/ Scan the current directory page (requires READ lease); usage may be NULL.
pn_status_t pn_file_manager_scan(pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease,const pn_fm_usage_t *usage);
/// 扫描 books 下可移动的目标目录列表（需持有 READ 租约）。/ Scan move destination folders under books (requires READ lease).
pn_status_t pn_file_manager_scan_move_targets(pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease);
/// 扫描存储卡浏览器当前路径下的子文件夹（需持有 READ 租约，自动隐藏 .readpico 与系统目录）。
/// Scan subdirectories at fm->browse_rel under TF mount root (requires READ lease, hiding .readpico and system dirs).
pn_status_t pn_file_manager_scan_browse(pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease);
/// 在 books 根目录创建子文件夹（需持有 WRITE 租约）。/ Create a subfolder under books root (requires WRITE lease).
pn_status_t pn_file_manager_mkdir(const pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease,const char *name);
/// 删除文件或空子文件夹（需持有 WRITE 租约），拒绝系统保留字体与非空目录；成功时可选输出被删路径。
/// Delete a file or empty subfolder (requires WRITE lease), rejecting protected system font and non-empty directories; optionally outputs deleted path.
pn_status_t pn_file_manager_delete(const pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease,const char *name,bool is_dir,char *deleted_path,size_t deleted_cap);
/// 在 books 目录内移动图书文件到指定子目录（target_subdir 空串表示 books 根目录；需持有 WRITE 租约）；不覆盖同名文件。
/// Move a book within books to target_subdir (empty string means books root; requires WRITE lease); never overwrites an existing file.
pn_status_t pn_file_manager_move(const pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease,const char *name,const char *target_subdir,char *old_path,size_t old_cap,char *new_path,size_t new_cap);
/// 纯绘制当前视图，恢复字号，不访问文件或硬件。/ Pure render of the current view, restoring font size, without file or hardware I/O.
pn_status_t pn_file_manager_render(const pn_file_manager_t *fm,pn_font_t *font,pn_frame_t *frame);
/// 纯命中测试：返回 PN_FM_*，空白或 busy 时返回 -1。/ Pure hit test: returns PN_FM_* or -1 on blank space or busy overlay.
int pn_file_manager_hit(const pn_file_manager_t *fm,int x,int y);
/// 将新建文件夹页的屏幕键盘命中码（含全拼中文）应用到 fm->keyboard 与 fm->input；若不是键盘码返回 false，否则返回 true 并通过 changed_out 指示是否需重绘。
/// Apply a mkdir on-screen keyboard hit code (including Chinese Pinyin IME) to fm->keyboard and fm->input; returns false if not a keyboard code.
bool pn_file_manager_apply_kb(pn_file_manager_t *fm,int hit,bool *changed_out);
