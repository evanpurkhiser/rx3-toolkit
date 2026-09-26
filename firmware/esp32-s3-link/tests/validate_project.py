#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def require(pattern: str, text: str, message: str) -> None:
    if re.search(pattern, text, re.MULTILINE) is None:
        raise AssertionError(message)


def main() -> int:
    manifest = (ROOT / "main/idf_component.yml").read_text()
    defaults = (ROOT / "sdkconfig.defaults").read_text()
    source = (ROOT / "main/rx3_link_bridge.c").read_text()
    protocol = (ROOT / "include/rx3_s3_config_protocol.h").read_text()
    partitions = (ROOT / "partitions.csv").read_text()
    mise = (ROOT / "mise.toml").read_text()
    ignored = (ROOT / ".gitignore").read_text().splitlines()

    require(r'version: "==2\.0\.1~1"', manifest, "esp_tinyusb must be exactly pinned")
    require(r'version: "==6\.1\.0"', manifest, "ESP-IDF compatibility must be exactly pinned")
    require(r'^CONFIG_TINYUSB_NET_MODE_NCM=y$', defaults, "NCM must be enabled")
    require(r'^CONFIG_TINYUSB_MSC_ENABLED=y$', defaults, "MSC must be enabled")
    require(r'^CONFIG_TINYUSB_MSC_BUFSIZE=4096$', defaults,
            "MSC buffer must cover one wear-level sector")
    require(r'^CONFIG_TINYUSB_NCM_OUT_NTB_BUFFS_COUNT=6$', defaults,
            "NCM receive side must tolerate sustained bursts")
    require(r'^CONFIG_TINYUSB_NCM_IN_NTB_BUFFS_COUNT=6$', defaults,
            "NCM transmit side must tolerate sustained bursts")
    require(r'^CONFIG_TINYUSB_NCM_OUT_NTB_BUFF_MAX_SIZE=6400$', defaults,
            "NCM receive blocks must aggregate multiple Ethernet frames")
    require(r'^CONFIG_TINYUSB_NCM_IN_NTB_BUFF_MAX_SIZE=6400$', defaults,
            "NCM transmit blocks must aggregate multiple Ethernet frames")
    require(r'^CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32$', defaults,
            "Wi-Fi receive buffering must retain the IDF throughput default")
    require(r'^CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=32$', defaults,
            "Wi-Fi transmit buffering must retain the IDF throughput default")
    require(r'^CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y$', defaults,
            "TinyUSB forwarding must use the S3 throughput CPU profile")
    require(r'^CONFIG_RX3_LINK_WIFI_TO_USB_QUEUE_DEPTH=16$', defaults,
            "Wi-Fi-to-USB buffering must be bounded")
    require(r'^CONFIG_TINYUSB_DESC_CUSTOM_PID=0x4012$', defaults,
            "composite USB identity must remain stable")
    require(r'^rx3disk,\s+data, fat,\s+0x300000, 0x500000,', partitions,
            "bootstrap partition must occupy the final exact 5 MiB")
    require(r'^CONFIG_TINYUSB_DESC_PRODUCT_STRING="RX3 Wi-Fi Link Bridge"$', defaults,
            "USB product identity must remain stable")
    require(r'^CONFIG_TINYUSB_DESC_SERIAL_STRING="RX3LINK-S3-0001"$', defaults,
            "USB serial identity must remain stable")
    require(r'^CONFIG_ESP_CONSOLE_SECONDARY_NONE=y$', defaults,
            "USB Serial/JTAG must not contend with TinyUSB at runtime")
    require(r'tinyusb_net_init\(&network\)[\s\S]*set_usb_link\(true\)', source,
            "NCM control carrier must remain available without Wi-Fi association")
    require(r'esp_wifi_internal_free_rx_buffer\(rx_buffer\)[\s\S]*tinyusb_net_send_async', source,
            "Wi-Fi RX ownership must be released before the nonblocking USB send")
    require(r'#define ETHERNET_MIN_FRAME_SIZE 60', source,
            "Wi-Fi-to-USB frames must use the Ethernet minimum without FCS")
    require(r'usb_length = length < ETHERNET_MIN_FRAME_SIZE[\s\S]*'
            r'memset\(frame->data \+ length, 0, usb_length - length\)', source,
            "short Wi-Fi frames must be zero-padded before NCM delivery")
    require(r'xQueueReceive\(s_wifi_to_usb_free, &frame, 0\)', source,
            "Wi-Fi receive callback must use a bounded frame pool")
    require(r'esp_wifi_start\(\)[\s\S]*esp_wifi_set_ps\(WIFI_PS_NONE\)[\s\S]*esp_wifi_connect\(\)', source,
            "Wi-Fi power saving must be disabled before association")
    require(r'esp_wifi_set_tx_done_cb\(wifi_tx_done\)', source,
            "diagnostics must distinguish queued frames from RF completion")
    require(r'wifi_tx_successes\+\+[\s\S]*wifi_tx_failures\+\+', source,
            "Wi-Fi completion callback must count both outcomes")
    require(r'esp_wifi_sta_get_ap_info\(&access_point\)[\s\S]*access_point\.rssi', source,
            "bridge status must report associated RSSI")
    require(r'esp_wifi_set_storage\(WIFI_STORAGE_RAM\)', source,
            "the versioned NVS blob must be the only persisted credential source")
    require(r'wifi_credentials_save\(&command\.credentials\)[\s\S]*'
            r's_wifi_credentials = command\.credentials', source,
            "runtime credentials must publish only after persistence succeeds")
    require(r'RX3_S3_CONFIG_PASSWORD_MAX_LENGTH 63', protocol,
            "wire protocol must preserve the WPA2 passphrase limit")
    require(r'bool tud_msc_is_writable_cb\(uint8_t lun\)[\s\S]*return true;', source,
            "bootstrap disk must permit the RX3's required read-write mount")
    require(r's_rx3_boot_usb_config[\s\S]*TUD_CDC_NCM_DESCRIPTOR[\s\S]*TUD_MSC_DESCRIPTOR',
            source,
            "NCM must precede MSC for the RX3 CONFIG_PDJ USB core")
    require(r'usb\.descriptor\.full_speed_config = s_rx3_boot_usb_config', source,
            "TinyUSB must start with the RX3 bootstrap descriptor")
    require(r'BRIDGE_BOOTLOADER_REQUEST "RX3BOOT\?"[\s\S]*xTaskNotifyGive',
            source,
            "NCM control must defer ROM downloader entry until after its acknowledgement")
    require(r'chip_usb_set_persist_flags\(USBDC_PERSIST_ENA\)[\s\S]*'
            r'RTC_CNTL_FORCE_DOWNLOAD_BOOT[\s\S]*esp_restart\(\)',
            source,
            "bootloader task must use the ESP-IDF ROM download reset sequence")
    for removed_symbol in (
        "s_rx3_runtime_usb_config",
        "TUD_CDC_DESCRIPTOR",
        "BRIDGE_CONSOLE_REQUEST",
        "tinyusb_cdcacm_init",
    ):
        if removed_symbol in source:
            raise AssertionError(
                f"removed ACM symbol remains in firmware: {removed_symbol}"
            )
    require(r'partition->address != 0x300000 \|\| partition->size != 0x500000', source,
            "firmware must verify bootstrap partition geometry")
    require(r'\[tasks\."flash-disk"\]', mise,
            "mise must provide a disk-only update path")

    for secret_path in ("sdkconfig.local.defaults", "sdkconfig"):
        if secret_path not in ignored:
            raise AssertionError(f"{secret_path} must be ignored")

    print("project validation passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
