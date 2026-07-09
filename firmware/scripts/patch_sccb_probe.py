Import("env")
import json
from pathlib import Path


def replace_function(text, marker, patch, already_marker):
    if already_marker in text:
        return text, False
    start = text.find(marker)
    if start < 0:
        return text, False
    brace = text.find("{", start)
    depth = 0
    end = brace
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    return text[:start] + patch.strip() + text[end:], True


def patch_file(path, replacements):
    if not path.exists():
        return
    text = path.read_text()
    changed = False
    for marker, patch, already in replacements:
        text, did = replace_function(text, marker, patch, already)
        changed = changed or did
    if changed:
        path.write_text(text)
        print(f"Patched {path}")


root = Path(env["PROJECT_DIR"])

SCCB_PROBE = """
uint8_t SCCB_Probe(void)
{
    for (int attempt = 0; attempt < 10; ++attempt) {
        const uint8_t slave_addr = 0x3C;
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (slave_addr << 1) | WRITE_BIT, ACK_CHECK_EN);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(sccb_i2c_port, cmd, 1000 / portTICK_RATE_MS);
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            return slave_addr;
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
    return 0;
}
"""

SCCB_WRITE16 = """
int SCCB_Write16(uint8_t slv_addr, uint16_t reg, uint8_t data)
{
    esp_err_t ret = ESP_FAIL;
    uint16_t reg_htons = LITTLETOBIG(reg);
    uint8_t *reg_u8 = (uint8_t *)&reg_htons;
    for (int attempt = 0; attempt < 5; ++attempt) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, ( slv_addr << 1 ) | WRITE_BIT, ACK_CHECK_EN);
        i2c_master_write_byte(cmd, reg_u8[0], ACK_CHECK_EN);
        i2c_master_write_byte(cmd, reg_u8[1], ACK_CHECK_EN);
        i2c_master_write_byte(cmd, data, ACK_CHECK_EN);
        i2c_master_stop(cmd);
        ret = i2c_master_cmd_begin(sccb_i2c_port, cmd, 1000 / portTICK_RATE_MS);
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            return 0;
        }
        vTaskDelay(15 / portTICK_PERIOD_MS);
    }
    ESP_LOGE(TAG, "W [%04x]=%02x fail after retries\\n", reg, data);
    return -1;
}
"""

FRAMESIZE_RETRY = """
    ESP_LOGD(TAG, "Setting frame size to %dx%d", resolution[frame_size].width, resolution[frame_size].height);
    vTaskDelay(300 / portTICK_PERIOD_MS);
    int fs_ret = -1;
    for (int fs_try = 0; fs_try < 4; ++fs_try) {
        fs_ret = s_state->sensor.set_framesize(&s_state->sensor, frame_size);
        if (fs_ret == 0) {
            break;
        }
        vTaskDelay(150 / portTICK_PERIOD_MS);
    }
    if (fs_ret != 0) {
        ESP_LOGE(TAG, "Failed to set frame size");
        err = ESP_ERR_CAMERA_FAILED_TO_SET_FRAME_SIZE;
        goto fail;
    }
"""

CAM_SRC_FILTER = [
    "-<*>",
    "+<driver/esp_camera.c>",
    "+<driver/sccb.c>",
    "+<driver/sensor.c>",
    "-<driver/cam_hal.c>",
    "+<conversions>",
    "+<sensors>",
]

for lib_root in root.glob(".pio/libdeps/**/esp32-camera"):
    lib_json = lib_root / "library.json"
    if lib_json.exists():
        data = json.loads(lib_json.read_text())
        frameworks = data.get("frameworks", [])
        if isinstance(frameworks, str):
            frameworks = [frameworks]
        if "arduino" not in frameworks:
            frameworks.append("arduino")
            data["frameworks"] = frameworks
        build = data.setdefault("build", {})
        if build.get("srcFilter") != CAM_SRC_FILTER:
            build["srcFilter"] = CAM_SRC_FILTER
            lib_json.write_text(json.dumps(data, indent=2) + "\n")
            print(f"Configured esp32-camera srcFilter in {lib_json}")

    patch_file(
        lib_root / "driver/sccb.c",
        [
            ("uint8_t SCCB_Probe(void)", SCCB_PROBE, "attempt < 10"),
            ("int SCCB_Write16(uint8_t slv_addr, uint16_t reg, uint8_t data)", SCCB_WRITE16, "fail after retries"),
        ],
    )
    esp_cam = lib_root / "driver/esp_camera.c"
    if esp_cam.exists():
        text = esp_cam.read_text()
        old = """    ESP_LOGD(TAG, "Setting frame size to %dx%d", resolution[frame_size].width, resolution[frame_size].height);
    if (s_state->sensor.set_framesize(&s_state->sensor, frame_size) != 0) {
        ESP_LOGE(TAG, "Failed to set frame size");
        err = ESP_ERR_CAMERA_FAILED_TO_SET_FRAME_SIZE;
        goto fail;
    }"""
        if "fs_try" not in text and old in text:
            esp_cam.write_text(text.replace(old, FRAMESIZE_RETRY.strip()))
            print(f"Patched {esp_cam}")

    # cam_hal.c stays in SDK libesp32-camera.a (source build breaks S3 DMA capture).
