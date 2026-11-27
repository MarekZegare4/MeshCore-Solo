#pragma once
// An in-memory stand-in for Adafruit_LittleFS (the nRF52's), in its shape:
// open(path, FILE_O_READ / FILE_O_WRITE), mkdir, rmdir_r, exists, a File that
// is a handle (copies share it), openNextFile() over a directory, name() the
// last path part. Kept to what LittleFS does and HistoryLog relies on:
// - a file is created only in a directory that exists;
// - FILE_O_WRITE opens at the end (appends);
// - what is written reaches the flash on close (crash() drops what isn't);
// - failWritesAfter(n): writes past n more bytes come out short (the flash
//   full); then, as LittleFS 1.x (lfs_file_sync skips an LFS_F_ERRED file),
//   the close keeps none of what was written since the open -- or, with
//   keepCutWrites(), what did go in (SPIFFS: a record cut short).
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Adafruit_LittleFS_Namespace {
enum { FILE_O_READ = 0, FILE_O_WRITE = 1 };
}
using namespace Adafruit_LittleFS_Namespace;

class Adafruit_LittleFS;

class File {
public:
  explicit File(Adafruit_LittleFS& fs) : _fs(&fs) {}
  File() {}
  explicit operator bool() const { return _h && _h->open; }

  int read(uint8_t* buf, size_t n);
  size_t write(const uint8_t* buf, size_t n);
  bool seek(uint32_t pos);
  uint32_t size() const;
  void close();
  const char* name() const { return _h ? _h->name.c_str() : ""; }
  bool isDirectory() const { return _h && _h->dir; }
  File openNextFile(uint8_t mode = FILE_O_READ);

private:
  friend class Adafruit_LittleFS;
  struct Handle {
    bool open = true, dir = false, writing = false, erred = false;
    std::string path, name;
    std::vector<uint8_t> data;   // a file's: its contents as opened, then as written
    uint32_t pos = 0;
    std::vector<std::string> children;   // a directory's, at open
    size_t next = 0;
  };
  Adafruit_LittleFS* _fs = nullptr;
  std::shared_ptr<Handle> _h;
};

class Adafruit_LittleFS {
public:
  File open(const char* path, uint8_t mode = FILE_O_READ) {
    File f(*this);
    const std::string p = norm(path);
    if (_dirs.count(p)) {
      if (mode != FILE_O_READ) return f;
      f._h = std::make_shared<File::Handle>();
      f._h->dir = true;
      f._h->path = p;
      f._h->name = base(p);
      for (const auto& d : _dirs) if (d != p && parent(d) == p) f._h->children.push_back(d);
      for (const auto& e : _files) if (parent(e.first) == p) f._h->children.push_back(e.first);
      return f;
    }
    auto it = _files.find(p);
    if (mode == FILE_O_READ && it == _files.end()) return f;
    if (it == _files.end() && !_dirs.count(parent(p))) return f;   // no directory: no file
    f._h = std::make_shared<File::Handle>();
    f._h->path = p;
    f._h->name = base(p);
    f._h->writing = mode != FILE_O_READ;
    if (it != _files.end()) f._h->data = it->second;
    f._h->pos = f._h->writing ? (uint32_t)f._h->data.size() : 0;
    if (f._h->writing) _open_writes.insert(f._h.get());
    return f;
  }
  bool mkdir(const char* path) {
    const std::string p = norm(path);
    if (!_dirs.count(parent(p))) return false;
    _dirs.insert(p);
    return true;
  }
  bool exists(const char* path) {
    const std::string p = norm(path);
    return _dirs.count(p) || _files.count(p);
  }
  bool remove(const char* path) { return _files.erase(norm(path)) > 0; }
  bool rmdir_r(const char* path) {
    const std::string p = norm(path);
    if (!_dirs.count(p) || p == "/") return false;
    for (auto it = _files.begin(); it != _files.end();) it = under(it->first, p) ? _files.erase(it) : std::next(it);
    for (auto it = _dirs.begin(); it != _dirs.end();) it = (*it == p || under(*it, p)) ? _dirs.erase(it) : std::next(it);
    return true;
  }

  // ── For the tests ──
  void crash() { _open_writes.clear(); }   // a power cut: nothing still open reaches the flash
  void failWritesAfter(long bytes) { _write_budget = bytes; }
  void keepCutWrites(bool on) { _keep_cut = on; }
  bool isDir(const std::string& p) const { return _dirs.count(p) > 0; }
  const std::vector<uint8_t>* file(const std::string& p) const {
    auto it = _files.find(p);
    return it == _files.end() ? nullptr : &it->second;
  }
  std::vector<uint8_t>* fileMut(const std::string& p) {
    auto it = _files.find(p);
    return it == _files.end() ? nullptr : &it->second;
  }
  int dirsUnder(const std::string& p) const {
    int n = 0;
    for (const auto& d : _dirs) if (parent(d) == p) n++;
    return n;
  }

private:
  friend class File;
  static std::string norm(const char* path) {
    std::string p = path && path[0] == '/' ? path : std::string("/") + (path ? path : "");
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
  }
  static std::string parent(const std::string& p) {
    const size_t s = p.find_last_of('/');
    return s == 0 ? "/" : p.substr(0, s);
  }
  static std::string base(const std::string& p) { return p.substr(p.find_last_of('/') + 1); }
  static bool under(const std::string& p, const std::string& dir) {
    return p.size() > dir.size() && p.compare(0, dir.size(), dir) == 0 && p[dir.size()] == '/';
  }
  void commit(File::Handle& h) {
    if (!_open_writes.erase(&h)) return;   // dropped by crash()
    if (h.erred && !_keep_cut) return;
    if (_dirs.count(parent(h.path))) _files[h.path] = h.data;
  }

  std::set<std::string> _dirs = {"/"};
  std::map<std::string, std::vector<uint8_t>> _files;
  std::set<File::Handle*> _open_writes;
  long _write_budget = -1;   // -1: no limit
  bool _keep_cut = false;
};

inline int File::read(uint8_t* buf, size_t n) {
  if (!*this || _h->dir) return -1;
  size_t k = _h->pos < _h->data.size() ? std::min(n, _h->data.size() - _h->pos) : 0;
  memcpy(buf, _h->data.data() + _h->pos, k);
  _h->pos += (uint32_t)k;
  return (int)k;
}
inline size_t File::write(const uint8_t* buf, size_t n) {
  if (!*this || !_h->writing) return 0;
  long& budget = _fs->_write_budget;
  size_t k = budget < 0 ? n : std::min(n, (size_t)budget);
  if (budget >= 0) budget -= (long)k;
  if (k < n) _h->erred = true;
  _h->data.resize(std::max<size_t>(_h->data.size(), _h->pos + k));
  memcpy(_h->data.data() + _h->pos, buf, k);
  _h->pos += (uint32_t)k;
  return k;
}
inline bool File::seek(uint32_t pos) {
  if (!*this || pos > _h->data.size()) return false;
  _h->pos = pos;
  return true;
}
inline uint32_t File::size() const { return *this ? (uint32_t)_h->data.size() : 0; }
inline void File::close() {
  if (!*this) return;
  if (_h->writing) _fs->commit(*_h);
  _h->open = false;
}
inline File File::openNextFile(uint8_t mode) {
  if (!*this || !_h->dir || _h->next >= _h->children.size()) return File(*_fs);
  return _fs->open(_h->children[_h->next++].c_str(), mode);
}

#define FILESYSTEM Adafruit_LittleFS
