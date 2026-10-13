/// ESP32-S3 原生USB复位：清下载标志并触发RTC watchdog，不修改Flash/eFuse。
/// ESP32-S3 native USB reset: clear download latch and trigger RTC watchdog without Flash/eFuse changes.
export async function resetAfterFlash(loader, portInfo, sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))) {
  if (loader.chip.CHIP_NAME !== "ESP32-S3") throw new Error("复位目标不是 ESP32-S3");
  const nativeUsb = portInfo?.usbVendorId === 0x303a && portInfo?.usbProductId === 0x1001;
  // 与esptool 5.3.1相同，仅清FORCE_DOWNLOAD_BOOT位。 / Match esptool 5.3.1, clearing only FORCE_DOWNLOAD_BOOT.
  await loader.writeReg(0x6000812c, 0, 1);
  if (!nativeUsb) { await loader.after("hard_reset"); return; }
  await loader.writeReg(0x600080b0, 0x50d83aa1);
  await loader.writeReg(0x6000809c, 2000);
  // 系统复位可能先于响应，连接丢失不等同写入失败。 / Reset may precede the response; disconnect is not a flash failure.
  let responseLost = false;
  try { await loader.writeReg(0x60008098, ((1 << 31) | (5 << 28) | (1 << 8) | 2) >>> 0); }
  catch { responseLost = true; }
  if (!responseLost) {
    try { await loader.writeReg(0x600080b0, 0); } catch { responseLost = true; }
  }
  await sleep(500);
  return { nativeUsb: true, responseLost };
}
