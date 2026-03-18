#include <cinttypes>
#include <esp_task_wdt.h>
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/application.h"
#include "esphome/core/defines.h"
#include "esphome/core/version.h"
#include "esphome/core/progmem.h"
#include "esphome/core/util.h"

#include "Httpfs.h"

namespace esphome {
namespace httpfs {

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#ifndef HTTPFS_NO_EXT_REASON
   #define RESAON(x) static const char* reason=x; this->errorReason=reason;
#else
   #define RESAON(x)
#endif

#include "pages.h"

constexpr static size_t SEND_CHUNK_SIZE = 0x400;
constexpr static size_t LOAD_CHUNK_SIZE = 0x400;

void Httpfs::addPairJson(const char* key, const char* val, PlatformString* buff){
   *buff=*buff+"\""+key+"\":\""+val+"\"";
}

void Httpfs::refreshJson(){
  if(_FS.exists(this->tempFileName)){ 
     _FS.remove(this->tempFileName);
  }
  jsData="{";
  this->addPairJson("t", (to_string(_FS.totalBytes())).c_str(), &jsData);
  jsData=jsData+",";
  this->addPairJson("u", (to_string(_FS.usedBytes())).c_str(), &jsData);
  jsData=jsData+",\"f\":[";
  File root = _FS.open("/");
  File file = root.openNextFile();
  
  for(uint8_t i=0; file; i=1){
    if(!file.isDirectory()){
        if(i>0) jsData = jsData+",";
        jsData = jsData+"{";
        this->addPairJson("n", file.name(), &jsData);
        jsData = jsData+",";
        this->addPairJson("s", (to_string(file.size())).c_str(), &jsData);
        jsData = jsData+"}";
    }
    file = root.openNextFile();
  }
  jsData=jsData+"]}";
  ESP_LOGV(TAG,"Refresh JSON: %s", jsData.c_str());
}

void Httpfs::dump_config() {
  ESP_LOGCONFIG(TAG, "Httpfs:");
  ESP_LOGCONFIG(TAG, "  Index URL: %s", this->index_url_);
  #if HTTPFS_FILE_SYSTEM == 0
     ESP_LOGCONFIG(TAG, "  LittleFS used");
  #elif HTTPFS_FILE_SYSTEM == 1
     ESP_LOGCONFIG(TAG, "  SPIFFS used");
  #elif HTTPFS_FILE_SYSTEM == 2
     ESP_LOGCONFIG(TAG, "  FFat used");
  #endif
  ESP_LOGCONFIG(TAG, "     Size %d bytes", _FS.totalBytes()); 
  ESP_LOGCONFIG(TAG, "     Used %d bytes", _FS.usedBytes()); 
  #ifdef HTTPFS_AUTH
     #ifdef HTTPFS_WEB_PASS
        ESP_LOGCONFIG(TAG, "     Web server credentials are used");
     #else
        ESP_LOGCONFIG(TAG, "     Active web authorization");
     #endif
  #else
     ESP_LOGCONFIG(TAG, "      are available to everyone");
  #endif
}

void Httpfs::setup() {

  if(!_FS.begin()){
     ESP_LOGE(TAG,"An Err has occurred while mounting storage, start format...");
     this->storFormat();
  } 
 
  ESP_LOGV(TAG, "Setting up Httpfs handler...");
  this->refreshJson();
  this->base_->init();
  this->base_->add_handler(this);
 
}

void Httpfs::loop(){
  //...
}

PlatformString Httpfs::extension(const PlatformString &file) {
  size_t pos = file.find_last_of('.');
  if (pos == PlatformString::npos || pos == file.size() - 1)
    return "";
  return file.substr(pos + 1);
}

PlatformString Httpfs::mime_type(const PlatformString &file){
  static const std::array<std::pair<const char *, const char *>, 25> mime_types = {{
      {"mp3", "audio/mpeg"},        {"wav", "audio/wav"},       {"flac", "audio/flac"},
      {"png", "image/png"},         {"jpg", "image/jpeg"},      {"jpeg", "image/jpeg"},
      {"gif", "image/gif"},         {"bmp", "image/bmp"},       {"txt", "text/plain"},
      {"log", "text/plain"},        {"csv", "text/csv"},        {"html", "text/html"},
      {"htm", "text/html"},         {"css", "text/css"},        {"js", "application/javascript"},
      {"json", "application/json"}, {"xml", "application/xml"}, {"pdf", "application/pdf"},
      {"zip", "application/zip"},   {"gz", "application/gzip"}, {"tar", "application/x-tar"},
      {"mp4", "video/mp4"},         {"avi", "video/x-msvideo"}, {"webm", "video/webm"},
      {"mkv", "video/x-matroska"},
  }};
  PlatformString ext = extension(file);
  if (!ext.empty()){
     std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
     for (const auto &[file_ext, mime] : mime_types) {
        if (ext == file_ext) return mime;
     }
  }
  return "application/octet-stream";
}

bool Httpfs::canHandle(AsyncWebServerRequest *request) const {
  auto url = request->url(); 
  if (request->method() == HTTP_GET) {
     return (url == this->index_url_ || url == this->json_url_ || url.find(this->download_url_)==0);
  } else if(request->method() == HTTP_POST) {
     return (url == this->upload_url_ || url.find(this->delete_url_)==0);
  }
  return false;
}

void Httpfs::destroyUploadContent(){
   if(this->upCont){
      if(this->upCont->file){
         this->upCont->file.close();
      }
      if(this->upCont->fileName){
         delete this->upCont->fileName;
         this->upCont->fileName=nullptr;
      }
      if(this->upCont->blobBuffer){
         delete this->upCont->blobBuffer;
         this->upCont->blobBuffer=nullptr;
      }
      delete this->upCont;
      this->upCont=nullptr;
   }
   this->refreshJson();
}

void Httpfs::destroyDownloadContent(){
   if(this->downCont){
      if(this->downCont->file){
         this->downCont->file.close();
      }
      if(this->downCont->sendBuff){
         delete this->downCont->sendBuff;
         this->downCont->sendBuff=nullptr;
      }
      
      delete this->downCont;
      this->downCont=nullptr;
   }
}

// получаем параметры из поля filename
size_t Httpfs::initUploadContent(const PlatformString &input){
   this->httpError=500;
   if(!this->downCont){
      const char* in=input.c_str();
      size_t ret=std::stoi(in);
      if (ret!=0) return ret;
      this->destroyUploadContent();
      in=strchr(in,'/');
      if(in){
         this->upCont = new FileUploadContent;
         if(this->upCont){
            in++;
            this->upCont->fileSize=std::stoi(in);
            in=strchr(in,'/');
            if(in && this->upCont->fileSize){
               ret=strlen(in);
               if(ret){
                  this->upCont->fileName = new char[ret+1]; 
                  if(this->upCont->fileName){
                     bool ok=true;
                     for(uint8_t i=0;i<str_deny.size();i++){
                        if(strcmp(in, str_deny[i])==0){
                           ok=false;
                           break;
                        }                           
                     }
                     if(ok){
                        strcpy(this->upCont->fileName,in);
                        this->upCont->blobBuffer = new uint8_t[LOAD_CHUNK_SIZE];
                        if(this->upCont->blobBuffer){
                           this->upCont->file=_FS.open(this->tempFileName, FILE_WRITE, true);
                           if(this->upCont->file){
                              ESP_LOGD(TAG,"Create content for receive file %s, size %d", this->upCont->fileName, this->upCont->fileSize);
                              return 0;
                           } else {
                              RESAON("Open file (" TOSTRING(__LINE__) ")");
                           }
                        } else {
                           RESAON("Buffer create (" TOSTRING(__LINE__) ")");
                        }
                     } else {
                        RESAON("Filename restricted (" TOSTRING(__LINE__) ")");
                     }                         
                  } else {
                     RESAON("Save filename (" TOSTRING(__LINE__) ")");
                  }                      
               } else {
                  RESAON("Chunk number (" TOSTRING(__LINE__) ")");
               }
            } else {
               RESAON("Read file size (" TOSTRING(__LINE__) ")");
            }
         } else {
            RESAON("Content create (" TOSTRING(__LINE__) ")");
         }
      } else {
         RESAON("Chunk number read (" TOSTRING(__LINE__) ")");
      }
   } else {
      RESAON("Download active (" TOSTRING(__LINE__) ")");
   }
   this->destroyUploadContent();
   this->httpError=500;
   ESP_LOGD(TAG,"Error read upload file parameters");
   return 0;
}

// сохранение данных в файл
bool Httpfs::saveUploadData(uint8_t* data, uint16_t index, uint16_t len){
  if(this->upCont){
     this->upCont->received+=len;
     for(uint16_t i=0; i<len; i++){
        this->upCont->blobBuffer[this->upCont->buffCounter++]=data[i];
        if(this->upCont->buffCounter==LOAD_CHUNK_SIZE){ // буффер накоплен
           this->upCont->file.write(this->upCont->blobBuffer,this->upCont->buffCounter);
           this->upCont->buffCounter=0;
           ESP_LOGV(TAG,"Blob store for %s approx. %d bytes", this->upCont->fileName, this->upCont->received);
        }
     }
     if(this->upCont->received==this->upCont->fileSize){ // конец записи файла
         this->upCont->file.write(this->upCont->blobBuffer,this->upCont->buffCounter);
         this->upCont->file.close();
         ESP_LOGD(TAG,"Final store for %s size %d bytes", this->upCont->fileName, this->upCont->received);
     }
     return true;
   }
   RESAON("Receiving process (" TOSTRING(__LINE__) ")");
   return false;   
}

void Httpfs::handleUpload(AsyncWebServerRequest *request, const PlatformString &filename, size_t index, uint8_t *data, size_t len, bool final){
  #ifdef HTTPFS_AUTH 
     // Check authentication if enabled
     if (this->auth_enabled_) {
        if (!request->authenticate(this->username_.c_str(), this->password_.c_str())) {
           ESP_LOGW(TAG, "Upload authentication failed");
           return;
        }
     }
  #endif
  if(len>0){
     if(this->saveUploadData(data,index,len)) return; 
  } else {
     if(index==0 && final==false){ // первый кусочек куска
        size_t currBlobIndex = this->initUploadContent(filename);
        if(currBlobIndex==0 && this->upCont==nullptr){
           //...
        } else if(currBlobIndex==0 && this->upCont){
           if(_FS.totalBytes()-_FS.usedBytes() > this->upCont->fileSize){
              ESP_LOGV(TAG,"Start upload file");
              return;
           } 
           RESAON("File size (" TOSTRING(__LINE__) ")");
        } else if (this->upCont && ++(this->upCont->blobIndex)==currBlobIndex){
           RESAON("Start OK (" TOSTRING(__LINE__) ")");
           this->httpError=200;
           return;                 
        } else {
           RESAON("Blob index (" TOSTRING(__LINE__) ")");
        }            
     } else if (final && this->upCont){ // успешное окончание приема куска
        if(this->upCont->received==this->upCont->fileSize){ // принят целый файл
           File file = _FS.open(this->tempFileName, FILE_READ);
           if(file){
              size_t fileSize=file.size();    
              file.close();
              if(this->upCont->fileSize==fileSize){
                 if(_FS.exists(this->upCont->fileName)){ 
                    _FS.remove(this->upCont->fileName);
                 }
                 uint32_t res=_FS.rename(this->tempFileName,this->upCont->fileName);
                 ESP_LOGD(TAG,"Save file %s , size: %d OK", this->upCont->fileName, fileSize);
                 this->destroyUploadContent();
                 RESAON("File OK (" TOSTRING(__LINE__) ")");
                 this->httpError=200;
                 return;                 
              } else {
                 RESAON("Read data size (" TOSTRING(__LINE__) ")");
              }
           } else {
              RESAON("Open templ file (" TOSTRING(__LINE__) ")");
           }
        } else { // принят кусок
           RESAON("Blob OK (" TOSTRING(__LINE__) ")");
           this->httpError=200;
           return;                 
        }
     }        
  }
  ESP_LOGD(TAG,"Error: %s", this->errorReason);
  this->destroyUploadContent();
  request->send(500, "text/plain", this->errorReason);
}
  
void Httpfs::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
  /*
  #ifdef HTTPFS_AUTH 
     if (this->auth_enabled_) {
       if (!request->authenticate(this->username_.c_str(), this->password_.c_str())) {
         return;
       }
     }
  #endif
  */
  ESP_LOGE(TAG,"Handle Body not used");
}

bool Httpfs::sendDownloadData(){
   if(this->downCont){
      uint16_t i=0;
      for(i; i<SEND_CHUNK_SIZE && this->downCont->file.available();i++){
         this->downCont->sendBuff[i]=this->downCont->file.read();   
      }
      if(httpd_resp_send_chunk(*(this->downCont->request), this->downCont->sendBuff, i)==ESP_OK){
         this->downCont->chunkCounter++;
         ESP_LOGV(TAG,"Send chunk %d, size %d",this->downCont->chunkCounter, i);
         if(i==0){ // передан заверщающй кусок
            ESP_LOGD(TAG,"Send full file Ok");
            this->destroyDownloadContent();
         }
         return true;
      } else {
         RESAON("Send chunk (" TOSTRING(__LINE__) ")");
      }
   } else {
      RESAON("Fatal(" TOSTRING(__LINE__) ")");
   }
   ESP_LOGE(TAG,"Error: %s",this->errorReason);
   this->downCont->request->send(this->httpError, "text/plain", this->errorReason);
   this->destroyDownloadContent();
   return false;
}

bool Httpfs::initDownloadContent(const PlatformString &fileName, AsyncWebServerRequest *request){
   this->httpError=500;
   if(!this->upCont){
      const char* chFileName = fileName.c_str();
      if(_FS.exists(chFileName)){ 
         File file=_FS.open(chFileName,FILE_READ);
         if(file){
            this->destroyDownloadContent();
            this->downCont = new FileDownloadContent;          
            if(this->downCont){
               this->downCont->sendBuff=new char[SEND_CHUNK_SIZE];
               if(this->downCont->sendBuff){
                  size_t fileSize=file.size();
                  PlatformString mime_t = mime_type(fileName);
                  PlatformString content_disposition = "attachment;filename=\"" + fileName.substr(1) + "\"";
                  this->downCont->request = request;
                  auto response = request->beginResponse(200, "application/octet-stream");
                  response->addHeader("Content-Disposition",content_disposition.c_str());
                  response->addHeader("Content-Length", to_string(file.size()).c_str());
                  response->addHeader("Cache-Control", "no-cache");
                  this->downCont->file=file;
                  ESP_LOGD(TAG,"Create content for send file: %s",chFileName);
                  return this->sendDownloadData();
               } else {
                  RESAON("Download buffer (" TOSTRING(__LINE__) ")");
               }
            } else {
               RESAON("Init content (" TOSTRING(__LINE__) ")");
            }
         } else {
            RESAON("Open file (" TOSTRING(__LINE__) ")");
         }
      } else {
         RESAON("File not exist (" TOSTRING(__LINE__) ")");
      }
   } else {
      RESAON("Upload active (" TOSTRING(__LINE__) ")");
   }
   this->destroyDownloadContent();
   return false;
}


void Httpfs::handleRequest(AsyncWebServerRequest *request) {
  #ifdef HTTPFS_AUTH 
     if (this->auth_enabled_) {
       if (!request->authenticate(this->username_.c_str(), this->password_.c_str())) {
         return request->requestAuthentication();
       }
    }
  #endif
  auto url = request->url();
  AsyncWebServerResponse *response=nullptr;

  //ESP_LOGV(TAG,"handleRequest %s: %s",(request->method() == HTTP_GET)?"GET":(request->method() == HTTP_POST)?"POST":"UNEXPECT",url.c_str());
  //if(url=="/favicon.ico"){ // файл иконки
     //this->httpError=200;
     //response = request->beginResponse_P(200, "image/x-icon", favicon_ico_gz, sizeof(favicon_ico_gz));
     //response->addHeader("Content-Encoding", "gzip");
  //} else if(url=="/style.css"){ // файл стиля
     //this->httpError=200;
     //response = request->beginResponse_P(200, "text/css", style_css);
     //response = request->beginResponse_P(200, "text/css", style_css_gz, sizeof(style_css_gz));
     //response->addHeader("Content-Encoding", "gzip");
  //} else 
      
  if (url==this->index_url_){
     this->httpError=200;
     response = request->beginResponse(200, "text/html", INDEX_GZ, sizeof(INDEX_GZ));
     response->addHeader("Cache-Control","max-age=86400");
     response->addHeader("Content-Encoding", "gzip"); 
     // ВНИМАНИЕ ДЛЯ ОТЛАДКИ, потом "max-age=86400"
     //response = request->beginResponse(200, this->"text/html", INDEX_HTML);
     //response->addHeader("Cache-Control","max-age=0"); // ВНИМАНИЕ ДЛЯ ОТЛАДКИ, потом "max-age=86400"
  } else if(url==this->json_url_){ // отправка списка файлов
     this->httpError=200;
     response = request->beginResponse(200, "application/json", jsData);
     response->addHeader("Cache-Control","no-cache"); //не кешируем
  } else if(url.find(this->delete_url_)==0){ // запроос на удаление
     PlatformString fileName = url.substr(strlen(this->delete_url_));
     const char* fName=fileName.c_str();
     if(_FS.exists(fName)){
        _FS.remove(fName);
        this->refreshJson();
        ESP_LOGD(TAG,"Delete file %s",fName);
        this->httpError=200;
        RESAON("Delete success (" TOSTRING(__LINE__) ")");        
     } else {
        this->httpError=500;
        RESAON("Missing file (" TOSTRING(__LINE__) ")");
     }
  } else if(url.find(this->download_url_)==0){ // запроос на передачу файла
     PlatformString fileName = url.substr(strlen(this->download_url_));
     if(this->initDownloadContent(fileName, request)){
        while(this->downCont && this->sendDownloadData());
        return;
     }
  } else if(url==this->upload_url_){ //окончание према файла
     //...
  }
  if(response==nullptr){
     if(this->httpError>0){
        if(this->httpError>201) ESP_LOGE(TAG,"Error: %s",this->errorReason);
        response = request->beginResponse(this->httpError, "text/plain", this->errorReason);
     } else {
        response = request->beginResponse(404,"text/plain",&(this->dongle)); // нет такого !
     }
  }
  request->send(response);
}


}  // namespace httpfs
}  // namespace esphome
