from pathlib import Path
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32, web_server_base
from esphome.components.esp32 import (
    add_extra_build_file, 
    add_idf_component, 
    include_builtin_idf_component, 
    require_vfs_dir,
    add_idf_sdkconfig_option,
    get_esp32_variant,
    require_fatfs,
)
from esphome.components.esp32.const import (
    KEY_ESP32,
    KEY_FLASH_SIZE,
)
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.core import CORE
from esphome.helpers import write_file_if_changed

from esphome.const import (
   CONF_ID,
   CONF_AUTH,
   CONF_PASSWORD,
   CONF_USERNAME,
   CONF_WEB_SERVER,
   PLATFORM_ESP32,
)
import esphome.final_validate as fv
from esphome.types import ConfigType

PARTITIONS_FILENAME = f"{esp32.CONF_PARTITIONS}_gen.csv"
PARTITIONS_USER_FILENAME = "partitions.csv"
CONF_DISK_SIZE = "disk_kilobytes"
CONF_WEB_AUTH = "web_auth"
CONF_HTTP_REPORT = "ext_http_report"
CONF_FILE_SYSTEM ="filesystem"
CONF_LITTLEFS = "LITTLEFS"
CONF_SPIFFS = "SPIFFS"
CONF_FFAT = "FFAT"
CONF_2M = "2MB"
CONF_4M = "4MB"
CONF_8M = "8MB"
CONF_16M = "16MB"
CONF_32M = "32MB"

def AUTO_LOAD() -> list[str]:
    auto_load = ["web_server_base", "ota.web_server"]
    return auto_load

CODEOWNERS = ["@Brokly"]
DEPENDENCIES = ["wifi"]

OPTIONS_FS = {
    CONF_LITTLEFS: 0,
    CONF_SPIFFS: 1,
    CONF_FFAT: 2,
}

httpfs_ns = cg.esphome_ns.namespace("httpfs")
Httpfs = httpfs_ns.class_("Httpfs", cg.Component)

def validate_auth(config):
    if CONF_AUTH in config and CONF_WEB_AUTH in config:
       raise cv.Invalid( 
            f"You only need to use one parameter '{CONF_WEB_AUTH}' or '{CONF_AUTH}'\n"
            f" Use one of the three:\n"
            f" 1. Do not use credentials to open file server pages: '{CONF_WEB_AUTH}: False'.\n"
            f" 2. Use credentials from the web server '{CONF_WEB_AUTH}: True' or the absence of any parameters.\n"
            f" 3. Use alternative credentials (only if authorization is disabled on the web server):\n"
            f"      {CONF_AUTH}:\n"
            f"        {CONF_USERNAME}: 'your username'\n"
            f"        {CONF_PASSWORD}: 'your pasword'"
       )
    return config

def is_idf_framework():
    """Detect framework from the validated ESP32 configuration."""
    framework = (CORE.config or {}).get("esp32", {}).get("framework", {})
    return framework.get("type") == "esp-idf"

def fs_is(config, name):
    """Accept both the enum value and its YAML spelling."""
    value = config[CONF_FILE_SYSTEM]
    return value == OPTIONS_FS[name] or (isinstance(value, str) and value.upper() == name)

def validate_fs(config):
    require_vfs_dir()
    # На этапе CONFIG_SCHEMA CORE.config может ещё не содержать esp32.framework.
    # Выбор значения по умолчанию переносим в FINAL_VALIDATE_SCHEMA.
    if CONF_FILE_SYSTEM in config and fs_is(config, CONF_FFAT):
       require_fatfs()
    return config

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Httpfs),
            cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(
                web_server_base.WebServerBase
            ),
            cv.Optional(CONF_FILE_SYSTEM): cv.enum(OPTIONS_FS, upper=True),
            cv.Optional(CONF_DISK_SIZE): cv.uint32_t,
            cv.Optional(CONF_WEB_AUTH): cv.boolean,
            cv.Optional(CONF_HTTP_REPORT, default = True): cv.boolean,
            cv.Optional(CONF_AUTH): cv.Schema(
                {
                    cv.Required(CONF_USERNAME): cv.All(
                        cv.string_strict, cv.Length(min=1)
                    ),
                    cv.Required(CONF_PASSWORD): cv.All(
                        cv.string_strict, cv.Length(min=1)
                    ),
                }
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on(
        [
            PLATFORM_ESP32,
        ]
    ),
    validate_fs,
    validate_auth,
)

def _final_validate(config: ConfigType) -> ConfigType:
    full_config = fv.full_config.get()
    wifi_conf = full_config.get("wifi")
    if wifi_conf is None:
        raise cv.Invalid(f"HTTP file server requires the wifi component to be configured")
    
    # Здесь доступна полная конфигурация, включая выбранный фреймворк.
    framework = full_config.get("esp32", {}).get("framework", {}).get("type")
    if CONF_FILE_SYSTEM not in config:
        if framework == "esp-idf":
            config[CONF_FILE_SYSTEM] = OPTIONS_FS[CONF_SPIFFS]
        else:
            raise cv.Invalid("Arduino requires an explicit httpfs.filesystem setting (littlefs, spiffs, or ffat)")
    if framework == "esp-idf" and not fs_is(config, CONF_SPIFFS):
        raise cv.Invalid("ESP-IDF supports SPIFFS only")

    web_conf = full_config.get("web_server")
    web_auth = web_conf.get("auth") if web_conf is not None else None

    # Авторизация веб-сервера может быть Basic или Digest. Проверку выполняет
    # сам ESPHome; HTTPFS передаёт ему те же учётные данные.
    if config.get(CONF_WEB_AUTH) is True and web_auth is None:
        raise cv.Invalid("httpfs.web_auth: true requires web_server.auth to be configured")
    if CONF_AUTH in config and web_auth is not None and config.get(CONF_WEB_AUTH, True):
        raise cv.Invalid("httpfs.auth cannot be used together with web_server.auth; set httpfs.web_auth: false to disable web server authentication for HTTPFS")

    return config

FINAL_VALIDATE_SCHEMA = _final_validate

async def to_code(config):

    paren = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], paren)
    await cg.register_component(var, config)
    if is_idf_framework():
        cg.add_define("HTTPFS_USE_ESP_IDF")
        include_builtin_idf_component("spiffs")
    else:
        cg.add_library("FS", None)

    if CONF_DISK_SIZE in config:
      
       disk_size=int(config[CONF_DISK_SIZE])*0x400
       mem_size=0
       flashsize=CORE.data[KEY_ESP32][KEY_FLASH_SIZE]
       for ch in flashsize: 
          if ch.isdigit(): mem_size=(mem_size * 10) + int(ch)
       mem_size=mem_size*0x100000
       mem_size=mem_size-0x9000-0x5000-0x2000-0x1000
       if disk_size>mem_size: disk_size=mem_size
       disk_size=mem_size-disk_size
       app_size=round(disk_size/0x20000)*0x10000
       if app_size<0x80000:
          raise cv.Invalid(f"\033[31mThe requested disk size is too large")

       disk_size=mem_size-app_size-app_size
       output  = f"# Name,     Type,   SubType,    Offset,         Size,       Flags\n"
       output += f"nvs,        data,   nvs,        0x9000,         0x5000,\n"
       output += f"otadata,    data,   ota,        0xE000,         0x2000,\n"
       output += f"app0,       app,    ota_0,      0x10000,        "+hex(app_size)+",\n"
       output += f"app1,       app,    ota_1,      ,               "+hex(app_size)+",\n"
       output += f"eeprom,     data,   0x99,       ,               0x1000,\n"
       if fs_is(config, CONF_FFAT) :
          output += f"ffat,       data,   fat,        ,               "
       else: 
          output += f"spiffs,     data,   spiffs,     ,               "
       output += hex(disk_size) + ",\n"
       
       part = PARTITIONS_FILENAME
       part_file=CORE.relative_build_path(part)
       write_file_if_changed(part_file, output)
       cg.add_platformio_option("board_build.partitions", part)
       print(f"\n\033[32mAutomatically generated partition table:\n")
       print(f"Flash for APP: {app_size} bytes , Disk size: {disk_size} bytes\n")
       print(output)

    else:
    
       part = PARTITIONS_USER_FILENAME
       partitions_path = Path(__file__).parent / part
       if partitions_path.exists():
          add_extra_build_file(part, partitions_path)
          cg.add_platformio_option("board_build.partitions", part)
          print(f"\n\033[32mUse user partition table\n")

    if fs_is(config, CONF_LITTLEFS) and not is_idf_framework():
       cg.add_library("LittleFS", None)
    elif fs_is(config, CONF_SPIFFS) and not is_idf_framework():
       # Явно подключаем библиотеку SPIFFS из Arduino Core (включая путь к SPIFFS.h).
       cg.add_library("SPIFFS", None)
    elif fs_is(config, CONF_FFAT) :
       cg.add_library("FFat", None)
       add_idf_sdkconfig_option("CONFIG_FATFS_LONG_FILENAMES",True)
       add_idf_sdkconfig_option("CONFIG_FATFS_MAX_LFN",255)
       add_idf_sdkconfig_option("CONFIG_FATFS_LFN_STACK",True)

    cg.add_define("HTTPFS_FILE_SYSTEM", config[CONF_FILE_SYSTEM])
    if config[CONF_HTTP_REPORT] == False:
       cg.add_define("HTTPFS_NO_EXT_REASON")
    # Настройки авторизации передаём во время инициализации, а не через
    # макросы компилятора: это сохраняет пароли со спецсимволами и не
    # подставляет их в определения препроцессора.
    if config.get(CONF_WEB_AUTH) is not False:
        if CONF_AUTH in config:
            auth = config[CONF_AUTH]
            cg.add_define("USE_WEBSERVER_AUTH")
            cg.add(var.set_auth(auth[CONF_USERNAME], auth[CONF_PASSWORD]))
        else:
            web_conf = (CORE.config or {}).get("web_server")
            web_auth = web_conf.get("auth") if web_conf is not None else None
            if web_auth is not None:
                cg.add(var.set_auth(web_auth[CONF_USERNAME], web_auth[CONF_PASSWORD]))
