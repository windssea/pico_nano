/// 网页安装器界面与串口流程：支持在线与本地 Gitee ZIP 固件包，读取设备现状、按 plan.js 写入并逐段 MD5 校验。
/// Web-installer UI and serial flow: support online and local Gitee ZIP packages, read the device state, write per plan.js and verify every region by MD5.
import { ESPLoader, Transport } from "./vendor/esptool-js-0.5.7/bundle.js";
import { extractReleasePackage, giteeDownloadInfo } from "./package.js";
import { isLittleFs, layoutOf, preservedLayoutOf, md5, planInstall, TABLE_OFFSET, TABLE_SIZE } from "./plan.js";
import { disableNativeWatchdogs, resetAfterFlash } from "./reset.js";

const FLASH_BYTES = 16 * 1024 * 1024;
const BAUD = 115200;
const HEAD = 0x2000;
const DOWNLOAD_STALL_MS = 90000;
const FILE_KEYS = ["bootloader", "partitionTable", "otaData", "app", "factoryData", "factoryWallpaper"];

const $ = (id) => document.getElementById(id);
const state = { manifest: null, release: null, files: null, localPackage: null, loader: null, transport: null, device: null, plan: null, busy: false };

function log(text) {
  const line = document.createElement("div");
  line.textContent = text;
  $("log").append(line);
  $("log").scrollTop = $("log").scrollHeight;
}
const terminal = { clean() {}, writeLine: log, write: log };

function status(text, tone = "") {
  $("status").textContent = text;
  $("status").dataset.tone = tone;
}
function progress(label, done, total) {
  $("progress").hidden = false;
  $("progress-label").textContent = label;
  $("progress-bar").style.width = `${total ? Math.min(100, (done / total) * 100) : 0}%`;
}
const hex = (value) => `0x${value.toString(16).toUpperCase()}`;
const kib = (value) => (value >= 1048576 ? `${(value / 1048576).toFixed(2)} MiB` : `${Math.ceil(value / 1024)} KiB`);

async function sha256(bytes) {
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

/// 下载单个固件文件并按清单长度和 SHA-256 校验；支持流式进度与空闲超时。
/// Fetch a single release file and check its manifest size and SHA-256, with streaming progress and stall timeout.
async function fetchReleaseFile(release, entry, onProgress) {
  const url = new URL(entry.name, new URL(release.path, location.href));
  const useAbort = typeof AbortController === "function" && typeof setTimeout === "function" && typeof clearTimeout === "function";
  const controller = useAbort ? new AbortController() : null;
  let timer = null;
  const arm = () => {
    if (!controller) return;
    if (timer !== null) clearTimeout(timer);
    timer = setTimeout(() => controller.abort(), DOWNLOAD_STALL_MS);
  };
  arm();
  try {
    const response = await fetch(url, controller ? { cache: "no-cache", signal: controller.signal } : { cache: "no-cache" });
    if (!response.ok) throw new Error(`下载 ${entry.name} 失败（${response.status}）`);
    let bytes;
    if (response.body && typeof response.body.getReader === "function") {
      const reader = response.body.getReader();
      const chunks = [];
      let received = 0;
      try {
        for (;;) {
          arm();
          const { value, done } = await reader.read();
          if (done) break;
          chunks.push(value);
          received += value.length;
          if (onProgress) onProgress(received);
        }
      } finally {
        reader.releaseLock();
      }
      bytes = new Uint8Array(received);
      let offset = 0;
      for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
    } else {
      bytes = new Uint8Array(await response.arrayBuffer());
      if (onProgress) onProgress(bytes.length);
    }
    if (bytes.length !== entry.size || (await sha256(bytes)) !== entry.sha256) throw new Error(`${entry.name} 校验失败，请刷新页面重试`);
    return bytes;
  } catch (error) {
    if (error?.name === "AbortError") {
      throw new Error(`下载 ${entry.name} 超时无响应；请改用“从 Gitee 下载”后导入本地固件包`);
    }
    throw error;
  } finally {
    if (timer !== null) clearTimeout(timer);
  }
}

/// 下载并以清单中的 SHA-256 校验固件文件。/ Download firmware files and check them against the manifest SHA-256.
async function loadFiles(release) {
  const files = {};
  const preservingKeys = ["partitionTable", ...(release.files.currentPartitionTable ? ["currentPartitionTable"] : []), "app"];
  const keys = release.mode === "app-only" ? preservingKeys : FILE_KEYS;
  const total = keys.reduce((sum, key) => sum + (release.files[key]?.size || 0), 0);
  let completed = 0;
  log(`正在在线下载 ${release.version || ""} 固件（共 ${kib(total)}）…`);
  for (const key of keys) {
    const entry = release.files[key];
    progress(`下载固件 ${entry.name}（${kib(completed)} / ${kib(total)}）`, completed, total);
    files[key] = await fetchReleaseFile(release, entry, (received) => {
      progress(`下载固件 ${entry.name}（${kib(completed + Math.min(received, entry.size))} / ${kib(total)}）`, completed + Math.min(received, entry.size), total);
    });
    completed += entry.size;
  }
  progress("固件下载并校验完成", 1, 1);
  log("在线固件下载并校验完成。");
  layoutOf(files.partitionTable);
  if (files.currentPartitionTable) layoutOf(files.currentPartitionTable);
  return files;
}

/// 更新 Gitee 下载直链和本地固件包导入状态。/ Refresh the Gitee download link and local package status.
function renderLocalPackage() {
  const info = giteeDownloadInfo(state.release);
  const download = $("gitee-download");
  download.href = info.downloadUrl;
  download.textContent = info.fileName ? `从 Gitee 下载${info.modeLabel}（${info.fileName}）` : "打开 Gitee 发布页下载";
  $("gitee-release").href = info.releaseUrl;
  const tip = $("local-package-status");
  if (state.localPackage) {
    tip.textContent = `已导入本地固件包：${state.localPackage.fileName}（${state.localPackage.label} · SHA-256 校验通过）`;
    tip.dataset.tone = "ok";
    $("clear-package").hidden = false;
  } else {
    tip.textContent = "当前使用在线固件源；也可以直接将下载好的 ZIP 拖放到此处。";
    tip.dataset.tone = "";
    $("clear-package").hidden = true;
  }
}

function clearLocalPackage() {
  state.localPackage = null;
  state.files = null;
  $("package-file").value = "";
  renderLocalPackage();
}

function renderReleaseNotes(release) {
  const box = $("release-notes-box");
  if (!box) return;
  const baseVersion = String(release?.version || "").split("+")[0];
  const fromList = (state.manifest?.changelog || []).find((entry) => entry.version === baseVersion);
  const notes = release?.notes || (fromList ? { date: fromList.date, items: fromList.items } : null);
  if (!notes || !Array.isArray(notes.items) || !notes.items.length) {
    box.hidden = true;
    return;
  }
  $("release-notes-title").textContent = `v${baseVersion} 更新内容`;
  $("release-notes-date").textContent = notes.date || "";
  const list = $("release-notes-list");
  list.replaceChildren();
  for (const item of notes.items) {
    const li = document.createElement("li");
    li.textContent = item;
    list.append(li);
  }
  box.hidden = false;
}

/// 渲染随站点自动同步的版本更新日志列表。/ Render the auto-synced release changelog history from manifest.json.
function renderChangelogHistory() {
  const container = $("changelog-list");
  if (!container) return;
  const entries = state.manifest?.changelog || [];
  container.replaceChildren();
  if (!entries.length) {
    const empty = document.createElement("p");
    empty.className = "muted";
    empty.textContent = "暂无版本更新记录。";
    container.append(empty);
    return;
  }
  entries.forEach((entry, index) => {
    const card = document.createElement("article");
    card.className = index === 0 ? "changelog-card is-latest" : "changelog-card";
    const list = document.createElement("ul");
    list.className = "changelog-items";
    for (const text of entry.items || []) {
      const li = document.createElement("li");
      li.textContent = text;
      list.append(li);
    }
    if (index === 0) {
      const head = document.createElement("div");
      head.className = "changelog-card-head";
      const title = document.createElement("span");
      title.className = "changelog-version";
      title.textContent = `v${entry.version}`;
      const badge = document.createElement("span");
      badge.className = "changelog-badge";
      badge.textContent = "最新版本";
      title.append(badge);
      const date = document.createElement("span");
      date.className = "muted";
      date.textContent = entry.date || "";
      head.append(title, date);
      card.append(head, list);
    } else {
      const details = document.createElement("details");
      const summary = document.createElement("summary");
      const title = document.createElement("span");
      title.textContent = `v${entry.version}`;
      const date = document.createElement("span");
      date.className = "muted";
      date.textContent = entry.date || "";
      summary.append(title, date);
      details.append(summary, list);
      card.append(details);
    }
    container.append(card);
  });
}

/// 使用清单中的实测大小与词条数同步刷新词典卡片展示。/ Sync dictionary card sizes, word counts, and Gitee links from manifest.json.
function renderDictionaryCards() {
  const dicts = state.manifest?.dictionaries || [];
  for (const dict of dicts) {
    const card = document.querySelector(`[data-dict-id="${dict.id}"]`);
    if (!card) continue;
    const meta = card.querySelector('[data-field="meta"]');
    if (meta) {
      const count = dict.wordcount ? `${Number(dict.wordcount).toLocaleString("en-US")} 词` : "";
      meta.textContent = [count, kib(dict.size || 0), dict.license].filter(Boolean).join(" · ");
    }
    const gitee = card.querySelector('[data-field="gitee"]');
    if (gitee && dict.giteeUrl) gitee.href = dict.giteeUrl;
  }
}

/// 渲染 PicoNano 共享素材库（分类标签、模拟阅读页叠加预览、字体试看、放大灯箱与 Gitee 加速整包下载）。
/// Render the PicoNano shareable assets gallery (category tabs, simulated e-ink previews, font preview, lightbox, and Gitee pack download).
const shareState = { activeId: null, checker: false, loadedFonts: new Set() };

function setLightboxBackdrop(mode) {
  const stage = $("lightbox-stage");
  if (!stage) return;
  stage.classList.remove("is-prose", "is-checker", "is-paper");
  stage.classList.add(mode === "checker" ? "is-checker" : mode === "paper" ? "is-paper" : "is-prose");
  for (const btn of (document.querySelectorAll?.("#lightbox-backdrops button[data-backdrop]") || [])) {
    btn.setAttribute("aria-pressed", btn.dataset.backdrop === mode ? "true" : "false");
  }
}

function openShareLightbox(cat, item) {
  const dialog = $("share-lightbox");
  if (!dialog) return;
  $("lightbox-title").textContent = item.stem || item.name;
  const dims = item.width && item.height ? `${item.width}×${item.height} px` : "";
  $("lightbox-meta").textContent = [cat.title, dims, kib(item.size || 0), item.ext?.toUpperCase()].filter(Boolean).join(" · ");
  const img = $("lightbox-img");
  img.src = item.path;
  img.alt = `${cat.title}：${item.name}`;
  const stage = $("lightbox-stage");
  stage.dataset.mode = cat.previewMode || "wallpaper";
  const backdrops = $("lightbox-backdrops");
  const showBackdrops = cat.previewMode === "mask" || cat.previewMode === "background";
  if (backdrops) backdrops.hidden = !showBackdrops;
  setLightboxBackdrop(cat.previewMode === "mask" ? (shareState.checker ? "checker" : "prose") : "paper");
  $("lightbox-target").textContent = `导入目标：${cat.uploadTarget}`;
  const dl = $("lightbox-download");
  dl.href = item.path;
  dl.download = item.name;
  const giteePack = $("lightbox-gitee-pack");
  if (giteePack) {
    if (cat.pack?.giteeUrl) {
      giteePack.href = cat.pack.giteeUrl;
      giteePack.hidden = false;
    } else {
      giteePack.hidden = true;
    }
  }
  if (typeof dialog.showModal === "function") dialog.showModal();
  else dialog.setAttribute("open", "");
}

function loadShareFontPreview(item, familyName, sampleNode) {
  if (typeof FontFace !== "function" || !document.fonts) return;
  if (shareState.loadedFonts.has(familyName)) {
    sampleNode.style.fontFamily = `"${familyName}", var(--display)`;
    return;
  }
  const face = new FontFace(familyName, `url("${encodeURI(item.path)}")`);
  face.load().then((loaded) => {
    document.fonts.add(loaded);
    shareState.loadedFonts.add(familyName);
    sampleNode.style.fontFamily = `"${familyName}", var(--display)`;
  }).catch(() => {});
}

function renderShareCategory(share, catId) {
  const categories = share?.categories || [];
  const cat = categories.find((c) => c.id === catId) || categories.find((c) => c.count > 0) || categories[0];
  if (!cat) return;
  shareState.activeId = cat.id;

  const tabs = $("share-tabs");
  if (tabs) {
    for (const btn of tabs.querySelectorAll('button[role="tab"]')) {
      const selected = btn.dataset.catId === cat.id;
      btn.setAttribute("aria-selected", selected ? "true" : "false");
    }
  }

  const toolbar = $("share-toolbar");
  if (toolbar) {
    toolbar.hidden = false;
    $("share-cat-title").textContent = `${cat.title}（${cat.count} 项）`;
    $("share-cat-badge").textContent = cat.badge || cat.title;
    $("share-cat-desc").textContent = cat.description || "";
    $("share-cat-target").textContent = `设备导入位置：${cat.uploadTarget || ""}`;
    const toggle = $("share-backdrop-toggle");
    if (toggle) {
      toggle.hidden = !(cat.previewMode === "mask" && cat.count > 0);
      toggle.textContent = shareState.checker ? "切回阅读页底图" : "切换透明棋盘格";
    }
    const giteeBtn = $("share-pack-gitee");
    const localBtn = $("share-pack-local");
    if (cat.pack) {
      giteeBtn.href = cat.pack.giteeUrl;
      giteeBtn.textContent = `Gitee 国内下载整包（${kib(cat.pack.size)}）`;
      giteeBtn.hidden = false;
      localBtn.href = cat.pack.path;
      localBtn.download = cat.pack.fileName;
      localBtn.textContent = `本站下载整包（ZIP）`;
      localBtn.hidden = false;
    } else {
      giteeBtn.hidden = true;
      localBtn.hidden = true;
    }
  }

  const grid = $("share-grid");
  if (!grid) return;
  grid.replaceChildren();
  grid.dataset.mode = cat.previewMode || "wallpaper";

  if (!cat.items || !cat.items.length) {
    const empty = document.createElement("div");
    empty.className = "share-empty";
    const title = document.createElement("strong");
    title.textContent = `「${cat.title}」目录已就绪，待补充素材`;
    const body = document.createElement("p");
    body.className = "muted";
    body.textContent = `将合法可分享的 ${cat.extensions.join(" / ")} 文件放入仓库 assets/share/${cat.dir}/ 并推送到 main 分支后，本页会自动生成在线预览并同步至 Gitee 国内高速下载。`;
    empty.append(title, body);
    grid.append(empty);
    return;
  }

  cat.items.forEach((item, index) => {
    const card = document.createElement("article");
    card.className = `share-card is-${cat.kind}`;

    if (cat.kind === "image") {
      const stage = document.createElement("button");
      stage.type = "button";
      stage.className = `share-thumb is-${cat.previewMode}${cat.previewMode === "mask" && shareState.checker ? " is-checker" : ""}`;
      stage.setAttribute("aria-label", `放大预览 ${item.stem || item.name}`);
      if (cat.previewMode === "mask" || cat.previewMode === "background") {
        const prose = document.createElement("div");
        prose.className = "thumb-prose";
        prose.setAttribute("aria-hidden", "true");
        const h = document.createElement("span");
        h.textContent = "第一章 拾起阅读";
        const p1 = document.createElement("span");
        p1.textContent = "字与字之间，留出恰好的距离。让阅读回到舒适，把时间留给文字。";
        const p2 = document.createElement("span");
        p2.textContent = "在柔和的墨水屏上，每一页都安静沉稳。";
        prose.append(h, p1, p2);
        stage.append(prose);
      } else if (cat.previewMode === "banner") {
        const titleOverlay = document.createElement("div");
        titleOverlay.className = "thumb-banner-copy";
        titleOverlay.setAttribute("aria-hidden", "true");
        titleOverlay.textContent = "读书 · 拾起上次的阅读";
        stage.append(titleOverlay);
      }
      const img = document.createElement("img");
      img.src = item.path;
      img.alt = `${cat.title}：${item.stem || item.name}`;
      img.loading = "lazy";
      if (item.width && item.height) {
        img.width = item.width;
        img.height = item.height;
      }
      const zoomHint = document.createElement("span");
      zoomHint.className = "thumb-zoom";
      zoomHint.textContent = "点开预览";
      stage.append(img, zoomHint);
      stage.addEventListener("click", () => openShareLightbox(cat, item));
      card.append(stage);
    } else if (cat.kind === "font") {
      const sample = document.createElement("div");
      sample.className = "share-font-sample";
      const line1 = document.createElement("p");
      line1.textContent = "字与字之间，留出恰好的距离。";
      const line2 = document.createElement("p");
      line2.className = "muted";
      line2.textContent = "PicoNano Reading 0123456789";
      sample.append(line1, line2);
      const familyName = `pn-share-font-${index}`;
      loadShareFontPreview(item, familyName, sample);
      card.append(sample);
    } else {
      const badge = document.createElement("div");
      badge.className = "share-book-cover";
      const ext = document.createElement("span");
      ext.className = "resource-tag";
      ext.textContent = (item.ext || "BOOK").toUpperCase();
      const bookTitle = document.createElement("strong");
      bookTitle.textContent = item.stem || item.name;
      badge.append(ext, bookTitle);
      card.append(badge);
    }

    const info = document.createElement("div");
    info.className = "share-card-body";
    const nameEl = document.createElement("h4");
    nameEl.textContent = item.stem || item.name;
    const metaEl = document.createElement("p");
    metaEl.className = "share-card-meta";
    const dims = item.width && item.height ? `${item.width}×${item.height}` : "";
    metaEl.textContent = [dims, kib(item.size || 0)].filter(Boolean).join(" · ");
    const actions = document.createElement("div");
    actions.className = "share-card-actions";
    const dl = document.createElement("a");
    dl.className = "share-dl-link";
    dl.href = item.path;
    dl.download = item.name;
    dl.textContent = cat.kind === "image" ? "下载原图" : "下载文件";
    actions.append(dl);
    if (cat.kind === "image") {
      const prevBtn = document.createElement("button");
      prevBtn.type = "button";
      prevBtn.className = "share-preview-link";
      prevBtn.textContent = "放大预览";
      prevBtn.addEventListener("click", () => openShareLightbox(cat, item));
      actions.append(prevBtn);
    }
    info.append(nameEl, metaEl, actions);
    card.append(info);
    grid.append(card);
  });
}

function renderShareGallery() {
  const share = state.manifest?.share;
  const tabs = $("share-tabs");
  const grid = $("share-grid");
  if (!tabs || !grid || !share || !Array.isArray(share.categories)) return;
  const giteeHome = $("share-gitee-home");
  if (giteeHome && share.giteeReleaseUrl) giteeHome.href = share.giteeReleaseUrl;

  tabs.replaceChildren();
  for (const cat of share.categories) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.role = "tab";
    btn.className = "share-tab";
    btn.dataset.catId = cat.id;
    btn.setAttribute("aria-selected", "false");
    const label = document.createElement("span");
    label.textContent = cat.title;
    const count = document.createElement("span");
    count.className = "share-tab-count";
    count.textContent = String(cat.count || 0);
    btn.append(label, count);
    btn.addEventListener("click", () => renderShareCategory(share, cat.id));
    tabs.append(btn);
  }
  const initial = share.categories.find((c) => c.count > 0) || share.categories[0];
  if (initial) renderShareCategory(share, initial.id);
}

function renderRelease() {
  const release = state.release;
  $("version").textContent = release.version;
  $("release-meta").textContent = `提交 ${release.commit} · 应用 ${kib(release.files.app.size)}`;
  const preserving = release.mode === "app-only";
  $("mode-note").textContent = preserving ? "支持已知旧布局及本项目完整刷入后的布局，仅更新应用。其他布局仍需完整刷入。"
    : "完整刷入会恢复引导与启动布局。请核对所选版本、备份及清除选项；不会自动切换刷入方式。";
  renderReleaseNotes(release);
  renderLocalPackage();
}

/// 解析并严格校验用户从本地导入的官方固件 ZIP。/ Parse and strictly verify a locally imported official firmware ZIP.
async function importPackageFile(file) {
  if (!file || state.busy || !state.release) return;
  state.busy = true;
  state.plan = null;
  $("plan").hidden = true;
  syncButtons();
  status(`正在解析并校验本地固件包 ${file.name}…`);
  log(`正在校验本地固件包：${file.name}（${kib(file.size || 0)}）`);
  try {
    const zipBytes = new Uint8Array(await file.arrayBuffer());
    const extracted = await extractReleasePackage(zipBytes, state.release, sha256, {
      loadMissingFile: (_key, entry) => fetchReleaseFile(state.release, entry),
    });
    state.files = extracted.files;
    state.localPackage = { fileName: file.name, label: extracted.label, version: state.release.version, mode: state.release.mode || "full" };
    renderLocalPackage();
    log(`本地固件包校验通过：${file.name}（${extracted.label}）`);
    if (state.loader && state.device) {
      await prepare();
    } else {
      state.busy = false;
      syncButtons();
      status(`已导入并校验本地固件包：${file.name}。请点击“连接设备”继续。`, "ok");
    }
  } catch (error) {
    fail(error);
  } finally {
    $("package-file").value = "";
  }
}

/// 按刷入方式列出可用包，不把旧升级包当作完整镜像。/ List packages by mode without treating an app-only package as a complete image.
function selectMode(preferredCommit) {
  const mode = $("install-mode").value;
  const releases = state.manifest.releases.filter((r) => (r.mode || "full") === mode && !r.legacy);
  const select = $("release");
  select.replaceChildren();
  for (const release of releases) {
    const option = document.createElement("option");
    option.value = release.version;
    option.textContent = release.version;
    select.append(option);
  }
  state.release = releases.find((r) => r.commit === preferredCommit) || releases[0];
  clearLocalPackage();
  state.plan = null;
  $("plan").hidden = true;
  for (const id of ["clear-nvs", "initialize-storage", "confirm-full", "confirm-model"]) $(id).checked = false;
  if (!state.release) {
    $("version").textContent = "—";
    $("release-meta").textContent = "";
    $("mode-note").textContent = "没有可用的当前完整包；历史首装包不作为新版本的恢复包。";
    status("当前没有该方式的固件包，请选择另一种方式或等待完整包发布。", "warn");
    syncButtons();
    return;
  }
  select.value = state.release.version;
  renderRelease();
  if (state.loader && state.device) prepare().catch(fail);
  else syncButtons();
}

async function init() {
  if (!("serial" in navigator)) {
    $("unsupported").hidden = false;
    $("connect").disabled = true;
  }
  try {
    const response = await fetch("firmware/manifest.json", { cache: "no-cache" });
    state.manifest = await response.json();
  } catch (error) {
    status(`读取固件清单失败：${error.message}`, "error");
    $("connect").disabled = true;
    return;
  }
  renderChangelogHistory();
  renderDictionaryCards();
  renderShareGallery();
  const select = $("release");
  select.addEventListener("change", () => {
    state.plan = null;
    $("plan").hidden = true;
    state.release = state.manifest.releases.find((r) => r.version === select.value && (r.mode || "full") === $("install-mode").value);
    clearLocalPackage();
    for (const id of ["confirm-model", "confirm-full", "clear-nvs", "initialize-storage"]) $(id).checked = false;
    renderRelease();
    if (state.device) prepare().catch(fail);
    else syncButtons();
  });
  $("install-mode").value = state.manifest.releases[0].mode || "full";
  selectMode();
}

function fail(error) {
  console.error(error);
  log(`错误：${error.message || error}`);
  status(`${error.message || error}`, "error");
  state.busy = false;
  state.plan = null;
  $("plan").hidden = true;
  $("progress").hidden = true;
  syncButtons();
}

function syncButtons() {
  const full = state.release && state.release.mode !== "app-only";
  const ready = state.plan && !state.plan.needsNvsClear && !state.plan.needsStorageInitialization &&
    $("confirm-model").checked && (!full || $("confirm-full").checked) && !state.busy;
  $("install").disabled = !ready;
  $("connect").disabled = state.busy || !("serial" in navigator) || !state.release;
  $("connect").textContent = state.loader ? "重新连接" : "连接设备";
  $("release").disabled = state.busy || !state.release;
  $("install-mode").disabled = state.busy || !state.manifest;
  $("import-package").disabled = state.busy || !state.release;
  $("clear-package").disabled = state.busy;
  for (const id of ["clear-nvs", "initialize-storage"]) $(id).disabled = state.busy || !full;
  $("confirm-full-label").hidden = !full;
}

async function disconnect() {
  if (state.transport) {
    try { await state.transport.disconnect(); } catch (error) { console.warn(error); }
  }
  state.transport = null;
  state.loader = null;
  state.device = null;
}

/// 连接、确认芯片与闪存容量，并读取当前分区表与数据分区开头。/ Connect, confirm chip and flash size, then read the table and data heads.
async function connect() {
  state.busy = true;
  state.plan = null;
  $("plan").hidden = true;
  syncButtons();
  await disconnect();
  $("confirm-model").checked = false;
  $("confirm-full").checked = false;
  status("请选择设备串口…");
  const port = await navigator.serial.requestPort({});
  // 选中串口后先就绪固件再进下载会话，避免网络下载慢时设备长时间停在 Stub 等待。
  // Ready the firmware after port selection and before entering stub download mode so slow networks never stall an active session.
  if (!state.files) {
    status(`正在下载并校验 ${state.release.version} 固件（如较慢可先从 Gitee 下载并导入本地包）…`);
    state.files = await loadFiles(state.release);
  } else if (state.localPackage) {
    log(`使用已校验的本地固件包：${state.localPackage.fileName}（跳过在线下载）`);
  }
  state.transport = new Transport(port, false);
  // 屏蔽固定 esptool-js 0.5.7 读包时的无条件 trace/hexConvert 风暴，不改 vendor 源码。
  // Suppress unconditional trace/hexConvert overhead during esptool-js 0.5.7 reads without modifying vendor code.
  state.transport.trace = () => {};
  state.transport.hexConvert = () => "";
  state.loader = new ESPLoader({ transport: state.transport, baudrate: BAUD, romBaudrate: 115200, terminal });
  status("正在连接，若一直无响应请让设备进入下载模式后重试…");
  const chip = await state.loader.main();
  if (state.loader.chip.CHIP_NAME !== "ESP32-S3") throw new Error(`检测到 ${chip}，本固件只支持 ESP32-S3`);
  const portInfo = port.getInfo();
  if (await disableNativeWatchdogs(state.loader, portInfo)) log("已关闭 ESP32-S3 原生 USB 看门狗。");
  const flashKiB = await state.loader.getFlashSize();
  if (flashKiB * 1024 !== FLASH_BYTES) throw new Error(`闪存容量为 ${flashKiB} KiB，本固件需要 16 MiB`);
  const mac = await state.loader.chip.readMac(state.loader);
  state.device = { chip, mac, portInfo };
  $("device").textContent = `${chip} · 16 MiB · MAC ${mac}`;
  log(`已识别设备：${chip} · 16 MiB · MAC ${mac}`);
  await prepare();
}

/// 读取设备现状并生成写入计划。/ Read the device state and build the write plan.
async function prepare() {
  state.busy = true;
  state.plan = null;
  $("plan").hidden = true;
  syncButtons();
  if (!state.files) {
    status(`正在下载并校验 ${state.release.version} 固件（如较慢可先从 Gitee 下载并导入本地包）…`);
    state.files = await loadFiles(state.release);
  }
  const layout = layoutOf(state.files.partitionTable);
  status("正在读取设备分区表…");
  log("正在读取设备分区表…");
  const deviceTable = await state.loader.readFlash(TABLE_OFFSET, TABLE_SIZE);
  // 未识别头部时读取完整分区，不能凭空白头部认定全部数据为空。/ Read the full partition for unrecognized headers; a blank header does not prove all data is blank.
  const readStorage = async (label, partition) => {
    const head = await state.loader.readFlash(partition.offset, HEAD);
    if (head.length !== HEAD) throw new Error("内部分区读取不完整，停止写入 / incomplete storage read");
    if (isLittleFs(head)) return head;
    status(`正在读取 ${label} 分区（${kib(partition.size)}）以确认是否为空…`);
    log(`正在读取 ${label} 分区（${kib(partition.size)}）…`);
    const bytes = await state.loader.readFlash(partition.offset, partition.size, (_packet, done, total) => {
      progress(`读取 ${label} 分区 ${kib(done)} / ${kib(total)}`, done, total);
    });
    if (bytes.length !== partition.size) throw new Error("内部分区读取不完整，停止写入 / incomplete storage read");
    return bytes;
  };
  const preserving = state.release?.mode === "app-only";
  const ota = preserving ? preservedLayoutOf(state.files, deviceTable).otadata : null;
  const otaReadback = preserving ? await state.loader.readFlash(ota.offset, ota.size) : null;
  const dataHead = preserving ? null : await readStorage("内部数据", layout.data);
  const wallpaperHead = preserving ? null : await readStorage("锁屏壁纸", layout.wallpaper);
  state.device.reads = { deviceTable, dataHead, wallpaperHead, otaReadback };
  buildPlan();
  $("progress").hidden = true;
  state.busy = false;
  syncButtons();
  log("设备状态读取完成，请核对写入计划。");
  status(state.plan.needsNvsClear || state.plan.needsStorageInitialization
    ? "已读取设备状态：迁移需要明确选择清除／初始化，请核对计划并先备份。" : "已读取设备状态，请核对下方计划。",
    state.plan.needsNvsClear || state.plan.needsStorageInitialization ? "warn" : "ok");
}

function buildPlan() {
  const { deviceTable, dataHead, wallpaperHead, otaReadback } = state.device.reads;
  state.plan = null;
  const preserving = state.release?.mode === "app-only";
  for (const id of ["clear-nvs", "initialize-storage"]) {
    $(id).disabled = preserving;
    if (preserving) $(id).checked = false;
  }
  state.plan = planInstall({ files: state.files, deviceTable, dataHead, wallpaperHead, otaReadback, clearNvs: $("clear-nvs").checked, initializeStorage: $("initialize-storage").checked, mode: preserving ? "app-only" : "full" });
  const plan = state.plan;
  const mode = preserving ? "保留布局升级：仅更新应用固件"
    : plan.mode === "upgrade" ? "升级：设备上已是本固件的分区布局"
    : plan.knownTable ? "首次安装：将把设备切换到本固件的分区布局" : "首次安装：未能识别设备上的分区表";
  $("plan-mode").textContent = mode;
  $("plan-mode").dataset.tone = preserving || plan.mode === "upgrade" ? "ok" : "warn";
  const kept = plan.keptNvs.length ? `保留设置区（NVS）扇区 ${plan.keptNvs.map(hex).join("、")}。` : "设置区（NVS）将全部清空。";
  const storage = $("initialize-storage").checked ? "已选择清除并初始化内部数据和壁纸，请先备份。"
    : plan.unrecognizedStorage.length ? `无法确认 ${plan.unrecognizedStorage.join("、")} 分区兼容。保留原数据；安装后可能无法保存，请备份后再决定恢复方式。` : "";
  $("plan-note").textContent = preserving ? (plan.resetOta ? "保留引导程序、分区表、NVS、阅读记录和壁纸。设备用过 OTA：写入 ota_0 后清空 OTA 选择，下次从新写入的固件启动。" : "保留引导程序、分区表、OTA 选择、NVS、阅读记录和壁纸。") : `${kept}${storage}不整片擦除，不触碰存储卡和屏幕电源芯片。`;
  if (plan.needsNvsClear) $("plan-note").textContent += " 新 NVS 范围含原来的非 NVS 或未知数据，必须明确勾选清除设置区才能刷入。";
  if (plan.needsStorageInitialization) $("plan-note").textContent += " 内部数据布局不兼容，必须明确选择初始化才能刷入。";
  const list = $("plan-steps");
  list.replaceChildren();
  for (const step of plan.steps) {
    const item = document.createElement("li");
    const name = document.createElement("span");
    name.textContent = step.kind === "erase" ? `清空 ${step.name}` : step.name;
    const where = document.createElement("code");
    where.textContent = `${hex(step.address)} · ${kib(step.data.length)}`;
    const why = document.createElement("small");
    why.textContent = step.optional ? `${step.reason}；与设备相同则跳过` : step.reason;
    item.append(name, where, why);
    list.append(item);
  }
  $("backup").checked = $("backup").dataset.touched ? $("backup").checked : !preserving;
  $("plan").hidden = false;
  syncButtons();
}

function saveBlob(bytes, name) {
  const url = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.textContent = `重新下载 ${name}`;
  link.click();
  $("backup-link").replaceChildren(link);
  setTimeout(() => URL.revokeObjectURL(url), 600000);
}

/// 整片读出备份；按 1 MiB 分块并实时回报进度。/ Read a full-chip backup in 1 MiB chunks with live progress.
async function backup() {
  const chunk = 0x100000;
  const image = new Uint8Array(FLASH_BYTES);
  log("开始备份整片闪存（16 MiB），期间请勿断开设备…");
  for (let at = 0; at < FLASH_BYTES; at += chunk) {
    progress(`备份整片闪存 ${kib(at)} / 16 MiB`, at, FLASH_BYTES);
    const bytes = await state.loader.readFlash(at, chunk, (_packet, done) => {
      progress(`备份整片闪存 ${kib(at + done)} / 16 MiB`, at + done, FLASH_BYTES);
    });
    if (bytes.length !== chunk) throw new Error("备份读取不完整，停止写入 / incomplete backup read");
    image.set(bytes, at);
  }
  progress("备份完成", 1, 1);
  const name = `PicoNano-backup-${state.device.mac.replaceAll(":", "")}-${new Date().toISOString().slice(0, 10)}.bin`;
  saveBlob(image, name);
  log(`备份已保存：${name}（SHA-256 ${await sha256(image)}）`);
}

async function install() {
  syncButtons();
  if ($("install").disabled) return;
  state.busy = true;
  syncButtons();
  let writeStarted = false;
  try {
    if ($("backup").checked) {
      status("正在备份整片闪存，请等待进度完成，期间请勿断开…");
      await backup();
    }
    const steps = [];
    for (const step of state.plan.steps) {
      if (step.optional) {
        const onDevice = await state.loader.flashMd5sum(step.address, step.data.length);
        if (onDevice === md5(step.data)) { log(`${step.name} 与设备相同，跳过`); continue; }
      }
      steps.push(step);
    }
    const total = steps.reduce((sum, step) => sum + step.data.length, 0);
    const before = steps.map((_, i) => steps.slice(0, i).reduce((sum, step) => sum + step.data.length, 0));
    status("正在写入，请勿断开设备…");
    writeStarted = true;
    await state.loader.writeFlash({
      fileArray: steps.map((step) => ({ data: state.loader.ui8ToBstr(step.data), address: step.address })),
      flashSize: "keep", flashMode: "keep", flashFreq: "keep", eraseAll: false, compress: true,
      reportProgress: (index, written, size) => progress(`写入 ${steps[index].name}`, before[index] + (written / size) * steps[index].data.length, total),
      calculateMD5Hash: (image) => md5(state.loader.bstrToUi8(image)),
    });
    progress("写入并校验完成", 1, 1);
    log("全部区域 MD5 校验通过，正在重启设备…");
    let resetUncertain = false;
    try {
      const reset = await resetAfterFlash(state.loader, state.device.portInfo);
      resetUncertain = !!reset?.responseLost;
      if (resetUncertain) log("watchdog 复位步骤USB响应中断，启动结果需观察。");
    } catch (error) {
      log(`固件写入与MD5校验已成功，但自动复位失败：${error.message || error}`);
      await disconnect();
      state.plan = null;
      state.busy = false;
      syncButtons();
      status("固件写入校验成功，自动复位未完成。请按板级方式复位或拔线开机，无需因复位错误重新刷写。", "warn");
      return;
    }
    await disconnect();
    state.plan = null;
    status(resetUncertain ? "写入校验通过，复位步骤遇到USB断开。请确认设备是否启动；未启动时按板级方式复位或拔线开机。" : "写入校验通过，已发送复位请求。请观察设备是否正常启动；若仍停在下载模式，请按板级方式复位或拔线开机。", resetUncertain ? "warn" : "ok");
  } catch (error) {
    fail(new Error(writeStarted
      ? `安装中断：${error.message || error}。设备可能无法正常启动，请手动进入下载模式后重新安装，或用外部工具写回备份。`
      : `安装准备失败：${error.message || error}。尚未写入固件，请保留日志并重试。`));
    return;
  }
  state.busy = false;
  syncButtons();
}

$("connect").addEventListener("click", () => connect().catch(fail));
$("install").addEventListener("click", () => install());
$("import-package").addEventListener("click", () => { if (!state.busy && state.release) $("package-file").click?.(); });
$("package-file").addEventListener("change", (event) => importPackageFile(event?.target?.files?.[0]));
$("clear-package").addEventListener("click", () => {
  if (state.busy) return;
  clearLocalPackage();
  log("已切换回在线固件源。");
  if (state.loader && state.device) prepare().catch(fail);
  else status("已改回在线固件源。", "");
});
const dropZone = $("local-package-box");
dropZone.addEventListener("dragover", (event) => {
  event?.preventDefault?.();
  if (!state.busy && state.release) dropZone.classList?.add("is-dragover");
});
dropZone.addEventListener("dragleave", () => dropZone.classList?.remove("is-dragover"));
dropZone.addEventListener("drop", (event) => {
  event?.preventDefault?.();
  dropZone.classList?.remove("is-dragover");
  return importPackageFile(event?.dataTransfer?.files?.[0]);
});
$("confirm-model").addEventListener("change", syncButtons);
$("confirm-full").addEventListener("change", syncButtons);
$("install-mode").addEventListener("change", () => selectMode(state.release?.commit));
$("backup").addEventListener("change", () => { $("backup").dataset.touched = "1"; });
$("clear-nvs").addEventListener("change", () => { if (!state.busy && state.device?.reads) { try { buildPlan(); } catch (error) { fail(error); } } });
$("initialize-storage").addEventListener("change", () => { if (!state.busy && state.device?.reads) { try { buildPlan(); } catch (error) { fail(error); } } });
$("share-backdrop-toggle")?.addEventListener("click", () => {
  shareState.checker = !shareState.checker;
  if (state.manifest?.share) renderShareCategory(state.manifest.share, shareState.activeId);
});
$("lightbox-close")?.addEventListener("click", () => $("share-lightbox")?.close?.());
$("share-lightbox")?.addEventListener("click", (event) => {
  if (event.target === $("share-lightbox")) $("share-lightbox")?.close?.();
});
for (const btn of (document.querySelectorAll?.("#lightbox-backdrops button[data-backdrop]") || [])) {
  btn.addEventListener("click", () => setLightboxBackdrop(btn.dataset.backdrop));
}
init().then(syncButtons);
