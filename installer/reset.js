/// ESP32-S3 原生USB会话维护与复位：禁用下载期间RTC看门狗/自动喂SWD，刷写后清下载标志并触发RTC watchdog，不修改Flash/eFuse。
/// ESP32-S3 native USB session guard and reset: disable RTC WDT / auto-feed SWD during flashing, and clear download latch then trigger RTC watchdog after flash without Flash/eFuse changes.

const RTC_CNTL_WDTCONFIG0_REG = 0x60008098;
const RTC_CNTL_WDTCONFIG1_REG = 0x6000809c;
const RTC_CNTL_WDTWPROTECT_REG = 0x600080b0;
const RTC_CNTL_WDT_WKEY = 0x50d83aa1;
const RTC_CNTL_SWD_CONF_REG = 0x600080b4;
const RTC_CNTL_SWD_WPROTECT_REG = 0x600080b8;
const RTC_CNTL_SWD_WKEY = 0x8f1d312a;
const RTC_CNTL_SWD_AUTO_FEED_EN = 0x80000000;
const RTC_CNTL_OPTION1_REG = 0x6000812c;

function isNativeUsb(portInfo) {
  return portInfo?.usbVendorId === 0x303a && portInfo?.usbProductId === 0x1001;
}

/// 与 esptool 5.3.1 ESP32S3ROM.disable_watchdogs 对齐：原生 USB-Serial/JTAG 下关闭 RTC WDT 并开启 SWD 自动喂狗，避免读取或写入期间复位。
/// Match esptool 5.3.1 ESP32S3ROM.disable_watchdogs: disable RTC WDT and enable SWD auto-feed on native USB-Serial/JTAG to prevent mid-session resets.
export async function disableNativeWatchdogs(loader, portInfo) {
  if (loader.chip.CHIP_NAME !== "ESP32-S3") throw new Error("看门狗目标不是 ESP32-S3");
  if (!isNativeUsb(portInfo)) return false;
  await loader.writeReg(RTC_CNTL_WDTWPROTECT_REG, RTC_CNTL_WDT_WKEY);
  await loader.writeReg(RTC_CNTL_WDTCONFIG0_REG, 0);
  await loader.writeReg(RTC_CNTL_WDTWPROTECT_REG, 0);
  await loader.writeReg(RTC_CNTL_SWD_WPROTECT_REG, RTC_CNTL_SWD_WKEY);
  const swd = await loader.readReg(RTC_CNTL_SWD_CONF_REG);
  await loader.writeReg(RTC_CNTL_SWD_CONF_REG, ((swd | RTC_CNTL_SWD_AUTO_FEED_EN) >>> 0));
  await loader.writeReg(RTC_CNTL_SWD_WPROTECT_REG, 0);
  return true;
}

export async function resetAfterFlash(loader, portInfo, sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))) {
  if (loader.chip.CHIP_NAME !== "ESP32-S3") throw new Error("复位目标不是 ESP32-S3");
  const nativeUsb = isNativeUsb(portInfo);
  // 与esptool 5.3.1相同，仅清FORCE_DOWNLOAD_BOOT位。 / Match esptool 5.3.1, clearing only FORCE_DOWNLOAD_BOOT.
  await loader.writeReg(RTC_CNTL_OPTION1_REG, 0, 1);
  if (!nativeUsb) { await loader.after("hard_reset"); return; }
  await loader.writeReg(RTC_CNTL_WDTWPROTECT_REG, RTC_CNTL_WDT_WKEY);
  await loader.writeReg(RTC_CNTL_WDTCONFIG1_REG, 2000);
  // 系统复位可能先于响应，连接丢失不等同写入失败。 / Reset may precede the response; disconnect is not a flash failure.
  let responseLost = false;
  try { await loader.writeReg(RTC_CNTL_WDTCONFIG0_REG, ((1 << 31) | (5 << 28) | (1 << 8) | 2) >>> 0); }
  catch { responseLost = true; }
  if (!responseLost) {
    try { await loader.writeReg(RTC_CNTL_WDTWPROTECT_REG, 0); } catch { responseLost = true; }
  }
  await sleep(500);
  return { nativeUsb: true, responseLost };
}
