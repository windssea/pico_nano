/// 切换真实界面预览；独立于固件安装与串口状态。 / Switch real-screen previews independently of firmware installation and serial state.
const choices = [...document.querySelectorAll(".feature-choice")];
const previewImage = document.getElementById("preview-image");
const previewLink = document.getElementById("preview-link");
for (const choice of choices) {
  choice.addEventListener("click", () => {
    for (const item of choices) item.setAttribute("aria-pressed", String(item === choice));
    const { image, title, description, caption } = choice.dataset;
    previewImage.src = `assets/${image}.png`;
    previewImage.alt = `PicoNano 实机${caption}`;
    previewLink.href = previewImage.src;
    previewLink.setAttribute("aria-label", `打开${caption}原图（新标签页）`);
    document.getElementById("preview-caption").textContent = `${caption} · 点击查看原图`;
    document.getElementById("preview-title").textContent = title;
    document.getElementById("preview-description").textContent = description;
  });
}
