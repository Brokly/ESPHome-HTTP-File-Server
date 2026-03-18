#pragma once

#ifndef HTTPFS_H
#define HTTPFS_H

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/wifi/wifi_component.h"
#include <esp_task_wdt.h>

#include <vector>
#include "FS.h"

#if HTTPFS_FILE_SYSTEM == 0
   #include "LittleFS.h"
   #define _FS LittleFS
#elif HTTPFS_FILE_SYSTEM == 1
   #include "spiffs.h"
   #define _FS SPIFFS
#elif HTTPFS_FILE_SYSTEM == 2
   #include "FFat.h"
   #define _FS FFat
#endif

#ifndef HTTPFS_NO_AUTH // авторизация отключена

   #ifdef HTTPFS_WEB_PASS // пароль из конфига web_server определен
      #ifdef HTTPFS_WEB_USER // юзер из конфига web_server определен
         #define HTTPFS_WEB_CRED // есть авторизация с вебсервера
         #define HTTPFS_AUTH // активируем авторизацию
      #endif
   #endif

   #ifndef HTTPFS_WEB_CRED // если есть учетные данные с веб сервера то локальная авторзация работать не будет
      #ifdef HTTPFS_LOCAL_AUTH // есть параметры локальной авторизацции 
         #define HTTPFS_AUTH // активируем авторизацию
      #endif
   #endif
   
#endif

namespace esphome {
namespace httpfs {

// File upload content
struct FileUploadContent {
  char* fileName{nullptr}; 
  uint8_t* blobBuffer{nullptr};
  File file;
  size_t fileSize{0};      
  size_t blobIndex{0};
  size_t buffCounter{0};
  size_t received{0};
};

// File download content
struct FileDownloadContent {
  File file;
  size_t chunkCounter{0};  
  AsyncWebServerRequest *request{nullptr};
  char* sendBuff{nullptr};
};

class Httpfs : public Component, public AsyncWebHandler {
 public:

  explicit Httpfs(web_server_base::WebServerBase *base) : base_(base) {}

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void handleUpload(AsyncWebServerRequest *request, const PlatformString &filename, size_t index, uint8_t *data, size_t len, bool final) override;
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override;
  bool isRequestHandlerTrivial() const override { return false; }
  void set_auth(const PlatformString &username, const PlatformString &password) {
     #ifdef HTTPFS_AUTH
        #ifndef HTTPFS_WEB_PASS
          this->username_ = username;
          this->password_ = password;
          this->auth_enabled_ = true;
        #endif
     #endif   
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
     _FS.begin();     
     ESP_LOGI(TAG,"Format storage - %s", formatRes ? "OK" : "ERROR");
  }

 protected:
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
 
  // Authentication
#ifdef HTTPFS_AUTH
  #ifdef HTTPFS_WEB_PASS
     PlatformString username_{HTTPFS_WEB_USER};
     PlatformString password_{HTTPFS_WEB_PASS};
     bool auth_enabled_{true};
  #else 
     PlatformString username_;
     PlatformString password_;
     bool auth_enabled_{false};
  #endif
#endif

};

}  // namespace httpfs
}  // namespace esphome

#endif //HTTPFS_H
