#pragma once
#ifdef HTTPFS_USE_ESP_IDF
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <utility>
#include "esp_spiffs.h"

namespace esphome::httpfs {
class IdfFile {
 public:
  IdfFile() = default;
  IdfFile(FILE *f, std::string path) : fp_(f), path_(std::move(path)) {}
  explicit IdfFile(DIR *d) : dir_(d) {}
  ~IdfFile() { close(); }
  IdfFile(const IdfFile &) = delete;
  IdfFile &operator=(const IdfFile &) = delete;
  IdfFile(IdfFile &&o) noexcept { *this = std::move(o); }
  IdfFile &operator=(IdfFile &&o) noexcept {
    if (this != &o) { close(); fp_=std::exchange(o.fp_,nullptr); dir_=std::exchange(o.dir_,nullptr); path_=std::move(o.path_); }
    return *this;
  }
  explicit operator bool() const { return fp_ || dir_; }
  void close() { if(fp_) fclose(fp_); if(dir_) closedir(dir_); fp_=nullptr; dir_=nullptr; }
  void flush() { if (fp_) std::fflush(fp_); }
  bool isDirectory() const { return dir_ != nullptr; }
  const char *name() const { return path_.c_str(); }
  size_t size() const { if(!fp_) return 0; auto p=ftell(fp_); fseek(fp_,0,SEEK_END); auto n=ftell(fp_); fseek(fp_,p,SEEK_SET); return n<0?0:static_cast<size_t>(n); }
  // Перемещение по файлу и получение текущей позиции (как в Arduino fs::File).
  bool seek(uint32_t pos) { return fp_ && std::fseek(fp_, static_cast<long>(pos), SEEK_SET) == 0; }
  size_t position() const {
    if (!fp_) return 0;
    const long pos = std::ftell(fp_);
    return pos < 0 ? 0 : static_cast<size_t>(pos);
  }
  int available() { return fp_ && !feof(fp_) && ftell(fp_) < static_cast<long>(size()); }
  int read() { return fp_ ? fgetc(fp_) : -1; }
  size_t read(uint8_t *buf, size_t len) { return fp_ ? fread(buf, 1, len, fp_) : 0; }
  size_t write(const uint8_t *buf,size_t len) { return fp_ ? fwrite(buf,1,len,fp_) : 0; }
  IdfFile openNextFile() {
    if(!dir_) return {};
    struct dirent *e;
    while((e=readdir(dir_))) {
      if(!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
      std::string name="/"; name+=e->d_name;
      std::string full="/spiffs"+name;
      FILE *f=fopen(full.c_str(),"rb");
      if(f) return IdfFile(f,std::move(name));
    }
    return {};
  }
 private:
  FILE *fp_{nullptr}; DIR *dir_{nullptr}; std::string path_;
};
class IdfSpiffs {
 public:
  bool begin() {
    if(mounted_) return true;
    esp_vfs_spiffs_conf_t conf{};
    conf.base_path="/spiffs"; conf.partition_label="spiffs"; conf.max_files=8; conf.format_if_mount_failed=false;
    mounted_=esp_vfs_spiffs_register(&conf)==ESP_OK;
    return mounted_;
  }
  bool format() {
    if (mounted_) { esp_vfs_spiffs_unregister("spiffs"); mounted_=false; }
    if (esp_spiffs_format("spiffs") != ESP_OK) return false;
    return begin();
  }
  size_t totalBytes() { size_t t=0,u=0; esp_spiffs_info("spiffs",&t,&u); return t; }
  size_t usedBytes() { size_t t=0,u=0; esp_spiffs_info("spiffs",&t,&u); return u; }
  bool exists(const char *p) { struct stat st{}; return stat(full(p).c_str(),&st)==0; }
  bool remove(const char *p) { return ::remove(full(p).c_str())==0; }
  bool rename(const char *a,const char *b) { return ::rename(full(a).c_str(),full(b).c_str())==0; }
  IdfFile open(const char *p, const char *mode="rb", bool create=false) {
    if(!strcmp(p,"/")) return IdfFile(opendir("/spiffs"));
    return IdfFile(fopen(full(p).c_str(),mode),p);
  }
 private:
  std::string full(const char *p) const { return std::string("/spiffs")+(p[0]=='/'?"":"/")+p; }
  bool mounted_{false};
};
inline IdfSpiffs idf_spiffs;
} // namespace esphome::httpfs
#endif
