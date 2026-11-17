# ESPHome-HTTP-File-Server
The web file server component allows you to create a small file server on ESP32 for storing and sharing data in the ESPHome environment.

Этот компонент позволяет организовать небольшой веб файл сервер с ограниченным, но во многих случаях, достаточным функционалом. Работает ТОЛЬКО НА ESP32.
Сервер имеет встроенный веб интерфейс, аскетичный, без "бантиков". Адрес нахождения веб итерфейса: http://ip/files  , где ip - ip адрес вашей ESP32 в сети или ее имя. Можно удалять, загружать и скачивать файлы из веб браузера. Структура расположения файлов - плоская, папок нет, все лежит в корне. Гарантированно работает Chrome, другие браузеры я не проверял. Вы можете сделать свой веб интерфейс и просто загрузить его на диск, только помните, корень вашего проекта будет .../files/get/ .

## Настройка компонента проста:

```yaml
httpfs:
  
  filesystem: spiffs     # выбор файловой системы (LittleFS, SPIFFS, FFat) - только для Фреймворка Arduino, для ESP-IDF необязателен, либо только SPIFFS 
  
  disk_kilobytes: 1500   # желаемый размер диска в киллобайтах

  auth:                  # авторизация для входа в веб интерфейс управления файлами (http://ip/files/), 
    username: "USER"     # если требуется установка пароля/логина, а в web_server таких установок нет, 
    password: "PASSWORD" # если не задать логин и пароль, то настройки будут аналогичны компоненту web_server
  
  web_auth: false        # при наличии настроек аутентификации в web_server, делает доступ к файл серверу безпарольным
  
  ext_http_report: false # отключить текствую расшифровку ошибок в веб интерфейсе (небольшое уменьшение размера прошивки)
```
Есть компоненты без которых сервер работает, но при их наличии, использует некоторые их настройки

```yaml
esp32:
  flash_size: 4MB #<<<<<<<<< эту настройку использует компонент httpfs

web_server:
  auth: #<<<<<<<<< эти настройки может использовать компонент httpfs 
    username: !secret web_user
    password: !secret web_pass
```
- filesystem - может принимать значения только LittleFS, SPIFFS, FFat для Arduino Фреймворка; только SPIFFS для ESP-IDF
- disk_kilobytes - желаемый размер диска, при компиляции размер может быть изменен системой в небольших пределах. Учитывайте, что чем больше размер диска, тем меньше места для вашей прошивки. Использование 2 мегабайтных esp32, скорее не имеет смысла, поскольку выделить память больше 100 киллобайт под файл сервер не получится. Компонент изменяет стандартную ESPHOME таблицу разделов, но с учетом требований ESPHOME. Если вы не укажете disk_kilobytes, то система использует стандартную таблицу разделов. Либо, если вы поместите в папку компонента файл partition.csv с нужной вам структурой компилятор использует ее. Либо можете использовать модификатор platformio для своей таблицы разделов:
```yaml
esphome:
  platformio_options: 
    board_build.partitions: ../partitions.csv 
```
- auth: - настройка работает только при отсутствии аналогичной настройки в компоненте web_server или самого компонента. Вы можете задать свою авторизацию для входа на страницу веб сервера и выполнение действий с файлами.
- web_auth: false - отключит авторизацию для входа на страницу веб сервера, если она есть у компонента web_server.
- ext_http_report - включает или отключает текстовую расшифровку ошибок в веб интерфейсе при работе с файлами. По умолчанию расшифровка ошибок включена. Отключение сэкономит немного места в прошивке.

При `httpfs.web_auth: false` доступ к файлам открыт без авторизации, даже если основной веб-сервер защищён. Не используйте это для доступных из недоверенных сетей устройств. При отсутствии `web_server.auth` можно задать отдельный `httpfs.auth` (в этом случае используется Basic).

## Если нужно форматирование, можно добавить кнопку. 
Форматирование удаляет все данные с диска. Кнопку рекомендуется использовать только при необходимости.

```yaml
button:
  - platform: template
    name: "Format Storage"
    on_press:
      - lambda: |-
          id(http_fs_id).storFormat();
```
## Внешний вид интерфейса:
 
 <img width="482" height="278" alt="image" src="https://github.com/user-attachments/assets/131eb651-1b08-4932-b94a-89ae20476e25" />

## В текущей версии HTTPFS предоставляет другим компонентам единый интерфейс поверх Arduino FS или ESP-IDF SPIFFS:

```cpp
bool storage_ready() const;
bool file_exists(const char *path);
bool file_remove(const char *path);
bool file_rename(const char *from, const char *to);
HttpfsFile file_open(const char *path, const char *mode = "r");
std::vector<std::string> file_list();
size_t storage_total_bytes();
size_t storage_used_bytes();
```

Вызывайте файловые методы **после успешного монтирования**, проверяя `storage_ready()`. Пути начинаются с `/`, например `/backup.bin`. `file_list()` возвращает имена с ведущим `/`. Тип `HttpfsFile` зависит от фреймворка, но поддерживает необходимые операции чтения и записи. Публичный API используется, в частности, компонентом резервного копирования NM8036.

## Чтение, запись и перемещение по файлу из C++

HTTPFS предоставляет другим компонентам доступ к файлам через `Httpfs::file_open()`.
Возвращаемый объект — `HttpfsFile`: для Arduino это `fs::File`, для ESP-IDF —
`IdfFile`. Используйте `auto`, чтобы один и тот же код работал с обоими фреймворками.

### Режимы открытия

| Режим | Назначение |
|---|---|
| `"r"` / `"rb"` | Чтение существующего файла |
| `"w"` / `"wb"` | Запись с созданием или очисткой файла |
| `"a"` / `"ab"` | Добавление данных в конец файла |

Суффикс `b` означает двоичный режим; в ESP-IDF используется стандартный `fopen()`.
Не открывайте важные файлы в режиме `w`, если не собираетесь перезаписать их содержимое.

### Операции с открытым файлом

| Метод | Назначение |
|---|---|
| `file.read()` | Прочитать один байт; `-1` при отсутствии данных/ошибке |
| `file.read(buffer, length)` | Прочитать до `length` байт; возвращает фактическое число байт |
| `file.write(buffer, length)` | Записать массив байт; возвращает число записанных байт |
| `file.seek(offset)` | Установить позицию чтения/записи от начала файла; возвращает `bool` |
| `file.position()` | Получить текущую позицию в байтах |
| `file.size()` | Получить размер файла в байтах |
| `file.available()` | Проверить наличие непрочитанных данных (ненулевое значение) |
| `file.flush()` | Сбросить буфер записи |
| `file.close()` | Закрыть файл |
| `if (file)` | Проверить, что файл успешно открыт |

В ESP-IDF методы `seek(offset)` и `position()` добавлены в `IdfFile`.
Для переносимого кода используется **перемещение от начала файла**.
Расширенные варианты `seek()` с перечислением `SeekMode` Arduino здесь не гарантируются.

### Пример: запись и чтение

```cpp
#include "esphome/components/httpfs/httpfs.h"

void write_example(esphome::httpfs::Httpfs *storage) {
  if (!storage || !storage->storage_ready()) return;

  auto file = storage->file_open("/example.bin", "w");
  if (!file) return;

  const uint8_t data[] = {0x10, 0x20, 0x30, 0x40};
  const size_t written = file.write(data, sizeof(data));
  file.flush();
  file.close();
  // written == sizeof(data) означает, что передано нужное число байт.
}

void read_example(esphome::httpfs::Httpfs *storage) {
  if (!storage || !storage->file_exists("/example.bin")) return;

  auto file = storage->file_open("/example.bin", "r");
  if (!file) return;

  const size_t file_size = file.size();
  uint8_t buffer[2]{};
  const size_t first_read = file.read(buffer, sizeof(buffer));
  const size_t current_pos = file.position();

  // Возвращаемся в начало и читаем два байта ещё раз.
  if (file.seek(0)) {
    const size_t second_read = file.read(buffer, sizeof(buffer));
    (void) second_read;
  }
  file.close();
  (void) file_size;
  (void) first_read;
  (void) current_pos;
}
```

Указатель `storage` должен ссылаться на экземпляр HTTPFS, полученный через
механизм `use_id` ESPHome в компоненте-потребителе. Не выполняйте файловые
операции до монтирования файловой системы (`storage_ready()`).

### Остальные операции HTTPFS

```cpp
storage->file_exists("/example.bin");
storage->file_rename("/example.bin", "/renamed.bin");
storage->file_remove("/renamed.bin");
auto files = storage->file_list();
size_t total = storage->storage_total_bytes();
size_t used = storage->storage_used_bytes();
```

`file_list()` возвращает список файлов корня с ведущим `/`.
SPIFFS не поддерживает настоящие вложенные каталоги. Одновременная запись
в один файл через HTTP-интерфейс и другой компонент не синхронизируется.

