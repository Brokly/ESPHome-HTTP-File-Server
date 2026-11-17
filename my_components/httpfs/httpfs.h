#pragma once

#ifndef HTTPFS_H
#define HTTPFS_H

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/wifi/wifi_component.h"
#include <esp_task_wdt.h>

#include <vector>
#include <string>
#include <utility>
#ifdef HTTPFS_USE_ESP_IDF
#include "httpfs_idf_storage.h"
#define _FS idf_spiffs
#define FILE_WRITE "wb"
#define FILE_READ "rb"
#else
#include "FS.h"

#if HTTPFS_FILE_SYSTEM == 0
   #include "LittleFS.h"
   #define _FS LittleFS
#elif HTTPFS_FILE_SYSTEM == 1
   #include <SPIFFS.h>
   #define _FS SPIFFS
#elif HTTPFS_FILE_SYSTEM == 2
   #include "FFat.h"
   #define _FS FFat
#endif
#endif // HTTPFS_USE_ESP_IDF

namespace esphome {
namespace httpfs {

#ifdef HTTPFS_USE_ESP_IDF
using HttpfsFile = IdfFile;
#else
using HttpfsFile = fs::File;
#endif

// File upload content
struct FileUploadContent {
  char* fileName{nullptr}; 
  uint8_t* blobBuffer{nullptr};
  HttpfsFile file;
  size_t fileSize{0};      
  size_t blobIndex{0};
  size_t buffCounter{0};
  size_t received{0};
};

// File download content
struct FileDownloadContent {
  HttpfsFile file;
  size_t chunkCounter{0};  
  AsyncWebServerRequest *request{nullptr};
  char* sendBuff{nullptr};
};

class Httpfs : public Component, public AsyncWebHandler {
 public:

  explicit Httpfs(web_server_base::WebServerBase *base) : base_(base) {}

  // Public, framework-independent filesystem API. Paths are relative to the
  // mounted filesystem root, for example "/settings.json".
  // Call only after HTTPFS setup() has successfully mounted the filesystem.
  bool storage_ready() const { return storage_ready_; }
  bool file_exists(const char *path) { return storage_ready_ && path && _FS.exists(path); }
  bool file_remove(const char *path) { return storage_ready_ && path && _FS.remove(path); }
  bool file_rename(const char *from, const char *to) {
    return storage_ready_ && from && to && _FS.rename(from, to);
  }
  HttpfsFile file_open(const char *path, const char *mode = "r") {
    if (!storage_ready_ || !path || !mode) return {};
    return _FS.open(path, mode);
  }
  size_t storage_total_bytes() { return storage_ready_ ? _FS.totalBytes() : 0; }
  size_t storage_used_bytes() { return storage_ready_ ? _FS.usedBytes() : 0; }
  // Names are returned with a leading slash, suitable for file_open().
  std::vector<std::string> file_list() {
    std::vector<std::string> result;
    if (!storage_ready_) return result;
    HttpfsFile root = _FS.open("/");
    if (!root || !root.isDirectory()) return result;
    for (HttpfsFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
      if (!entry.isDirectory()) {
        std::string name = entry.name();
        if (!name.empty() && name[0] != '/') name.insert(name.begin(), '/');
        result.push_back(std::move(name));
      }
      entry.close();
    }
    root.close();
    return result;
  }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void handleUpload(AsyncWebServerRequest *request, const PlatformString &filename, size_t index, uint8_t *data, size_t len, bool final) override;
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override;
  bool isRequestHandlerTrivial() const override { return false; }
  // Используем тот же механизм проверки Basic/Digest, что и web_server.
  void set_auth(const PlatformString &username, const PlatformString &password) {
    this->username_ = username;
    this->password_ = password;
    this->auth_enabled_ = !username.empty();
  }

  void storFormat(){
     ESP_LOGI(TAG, "Start format...");
     #if HTTPFS_FILE_SYSTEM == 1
        esp_task_wdt_config_t wdtConfig = { 20000000, 0, false};
        esp_task_wdt_reconfigure(&wdtConfig);
        esp_task_wdt_reset();
     #endif
     bool formatRes = _FS.format();
     #if HTTPFS_FILE_SYSTEM == 1
        wdtConfig = {5000000, (1<<1 | 1<<0), true};
        esp_task_wdt_reconfigure(&wdtConfig);
     #endif 
     storage_ready_ = _FS.begin();
     ESP_LOGI(TAG,"Format storage - %s", formatRes ? "OK" : "ERROR");
  }

 protected:
  bool storage_ready_{false};
  web_server_base::WebServerBase *base_;
  const char *TAG{"Httpfs"};
  const char *index_url_{"/files"};
  const char *json_url_{"/files/js"};
  const char *upload_url_{"/files/up"};
  const char *download_url_{"/files/get"};
  const char *delete_url_{"/files/del"};
  const char *tempFileName{"/temp.tmp"};
  const char dongle{0}; 
  const FixedVector<const char *> str_deny={&json_url_[6], &upload_url_[6], &download_url_[6], &delete_url_[6]};
  
 private:
  PlatformString extension(const PlatformString &file);
  PlatformString mime_type(const PlatformString &file);

  // JSON listing files
  PlatformString jsData{""};
  void refreshJson();
  void addPairJson(const char* key, const char* val, PlatformString* buff);

  // Upload
  FileUploadContent* upCont{nullptr}; // контент загрузки файла
  size_t initUploadContent(const PlatformString &input);
  void destroyUploadContent();
  bool saveUploadData(uint8_t* data, uint16_t index, uint16_t len);

  // Download
  FileDownloadContent* downCont{nullptr};
  bool initDownloadContent(const PlatformString &fileName, AsyncWebServerRequest *request);
  void destroyDownloadContent();
  bool sendDownloadData();
  
  // for HTTP errors procesing
  const char* errorReason{&dongle};
  uint16_t httpError;
 
  // Авторизация: тип Basic/Digest выбирается самим ESPHome при сборке.
  PlatformString username_;
  PlatformString password_;
  bool auth_enabled_{false};

};

}  // namespace httpfs
}  // namespace esphome

#endif //HTTPFS_H
