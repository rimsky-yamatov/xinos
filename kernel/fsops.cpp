#include "fsops.hpp"

#include <cstdio>
#include <cstring>
#include <strings.h>

#include "fat.hpp"

namespace fsops {

namespace {

constexpr char kTrashDir[] = "/trash";
constexpr char kIndexPath[] = "/trash/RESTORE.IDX";
constexpr size_t kIndexMax = 4096;

bool DirCluster(const char* path, unsigned long* cluster) {
  if (strcmp(path, "/") == 0 || path[0] == '\0') {
    *cluster = fat::boot_volume_image->root_cluster;
    return true;
  }
  auto [ e, post_slash ] = fat::FindFile(path);
  if (e == nullptr || e->attr != fat::Attribute::kDirectory) {
    return false;
  }
  *cluster = e->FirstCluster();
  return true;
}

void MakeName(const fat::DirectoryEntry& e, char* out) {
  char base[9], ext[4];
  fat::ReadName(e, base, ext);
  if (ext[0]) {
    snprintf(out, 16, "%s.%s", base, ext);
  } else {
    snprintf(out, 16, "%s", base);
  }
}

bool Visible(const fat::DirectoryEntry& e) {
  const uint8_t first = static_cast<uint8_t>(e.name[0]);
  if (first == 0xe5 || first == '.') return false;
  if (e.attr == fat::Attribute::kLongName) return false;
  if (static_cast<uint8_t>(e.attr) & 0x08) return false;  // ボリュームラベル
  return true;
}

const char* BaseName(const char* path) {
  const char* s = strrchr(path, '/');
  return s ? s + 1 : path;
}

// dir/name が既に存在するなら name~N.ext の形で空き名を探して out に dir/... を書く
bool UniquePath(const char* dir, const char* name, char* out, size_t out_size) {
  snprintf(out, out_size, "%s%s%s", dir, strcmp(dir, "/") == 0 ? "" : "/", name);
  if (fat::FindFile(out).first == nullptr) return true;

  char base[16], ext[8] = "";
  const char* dot = strrchr(name, '.');
  const size_t base_len = dot ? static_cast<size_t>(dot - name) : strlen(name);
  memcpy(base, name, base_len);
  base[base_len] = '\0';
  if (dot) snprintf(ext, sizeof(ext), "%s", dot);

  for (int n = 1; n < 100; ++n) {
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "~%d", n);
    const size_t keep = 8 - strlen(suffix) < base_len ? 8 - strlen(suffix) : base_len;
    char candidate[32];
    snprintf(candidate, sizeof(candidate), "%.*s%s%s", static_cast<int>(keep), base, suffix, ext);
    snprintf(out, out_size, "%s%s%s", dir, strcmp(dir, "/") == 0 ? "" : "/", candidate);
    if (fat::FindFile(out).first == nullptr) return true;
  }
  return false;
}

Error EnsureTrashDir() {
  auto [ e, post_slash ] = fat::FindFile(kTrashDir);
  if (e != nullptr) {
    return e->attr == fat::Attribute::kDirectory ? MAKE_ERROR(Error::kSuccess)
                                                 : MAKE_ERROR(Error::kNotDirectory);
  }
  return fat::CreateDirectory(kTrashDir).error;
}

// ---- 復元用インデックス(/trash/RESTORE.IDX: "NAME\t元のパス\n" の繰り返し) ----
size_t ReadIndex(char* buf, size_t cap) {
  auto [ e, post_slash ] = fat::FindFile(kIndexPath);
  if (e == nullptr) return 0;
  size_t len = e->file_size < cap - 1 ? e->file_size : cap - 1;
  len = fat::LoadFile(buf, len, *e);
  buf[len] = '\0';
  return len;
}

void WriteIndex(const char* data, size_t len) {
  if (fat::FindFile(kIndexPath).first != nullptr) {
    fat::RemoveEntry(kIndexPath);
  }
  if (len == 0) return;
  auto [ e, err ] = fat::CreateFile(kIndexPath);
  if (err) return;
  fat::FileDescriptor fd{*e};
  fd.Write(data, len);
}

void AppendIndex(const char* name, const char* original) {
  static char buf[kIndexMax];
  size_t len = ReadIndex(buf, sizeof(buf));
  const int n = snprintf(buf + len, sizeof(buf) - len, "%s\t%s\n", name, original);
  if (n > 0 && len + n < sizeof(buf)) {
    WriteIndex(buf, len + n);
  }
}

// name の行を取り除き、元のパスを orig に返す。無ければ false。
bool TakeIndex(const char* name, char* orig, size_t orig_size) {
  static char buf[kIndexMax], out[kIndexMax];
  const size_t len = ReadIndex(buf, sizeof(buf));
  size_t out_len = 0;
  bool found = false;
  const size_t name_len = strlen(name);
  for (char* line = buf; line < buf + len;) {
    char* nl = strchr(line, '\n');
    const size_t line_len = nl ? static_cast<size_t>(nl - line) : strlen(line);
    if (!found && line_len > name_len && strncasecmp(line, name, name_len) == 0 &&
        line[name_len] == '\t') {
      const size_t olen = line_len - name_len - 1;
      if (olen < orig_size) {
        memcpy(orig, line + name_len + 1, olen);
        orig[olen] = '\0';
        found = true;
      }
    } else {
      memcpy(out + out_len, line, line_len);
      out[out_len + line_len] = '\n';
      out_len += line_len + 1;
    }
    line += line_len + 1;
  }
  if (found) WriteIndex(out, out_len);
  return found;
}

}  // namespace

bool ReadDir(const char* path, int index, DirInfo* out) {
  unsigned long cluster;
  if (!DirCluster(path, &cluster)) return false;
  int n = 0;
  while (cluster != fat::kEndOfClusterchain) {
    auto dir = fat::GetSectorByCluster<fat::DirectoryEntry>(cluster);
    for (size_t i = 0; i < fat::bytes_per_cluster / sizeof(fat::DirectoryEntry); ++i) {
      if (dir[i].name[0] == 0x00) return false;
      if (!Visible(dir[i])) continue;
      char name[16];
      MakeName(dir[i], name);
      if (strcmp(name, "RESTORE.IDX") == 0) continue;  // ゴミ箱の管理ファイルは隠す
      if (n++ == index) {
        memset(out, 0, sizeof(*out));
        memcpy(out->name, name, sizeof(out->name));
        out->is_dir = dir[i].attr == fat::Attribute::kDirectory;
        out->size = dir[i].file_size;
        return true;
      }
    }
    cluster = fat::NextCluster(cluster);
  }
  return false;
}

Error Move(const char* src, const char* dst) {
  if (strcmp(src, "/") == 0) return MAKE_ERROR(Error::kInvalidFile);
  char final_dst[300];
  unsigned long dummy;
  if (DirCluster(dst, &dummy)) {  // 既存ディレクトリの中へ
    snprintf(final_dst, sizeof(final_dst), "%s%s%s", dst,
             strcmp(dst, "/") == 0 ? "" : "/", BaseName(src));
  } else {
    snprintf(final_dst, sizeof(final_dst), "%s", dst);
  }
  const size_t src_len = strlen(src);
  if (strncmp(final_dst, src, src_len) == 0 &&
      (final_dst[src_len] == '/' || final_dst[src_len] == '\0')) {
    return MAKE_ERROR(Error::kInvalidFile);  // 自分自身の中には移せない
  }
  return fat::MoveEntry(src, final_dst);
}

Error RemoveRecursive(const char* path) {
  auto [ e, post_slash ] = fat::FindFile(path);
  if (e == nullptr) return MAKE_ERROR(Error::kNoSuchEntry);
  if (e->attr == fat::Attribute::kDirectory) {
    DirInfo info;
    while (ReadDir(path, 0, &info)) {
      char child[300];
      snprintf(child, sizeof(child), "%s/%s", path, info.name);
      if (auto err = RemoveRecursive(child)) return err;
    }
  }
  return fat::RemoveEntry(path);
}

Error Trash(const char* path) {
  if (strcmp(path, "/") == 0 || strcmp(path, kTrashDir) == 0 ||
      strncmp(path, "/trash/", 7) == 0) {
    return MAKE_ERROR(Error::kInvalidFile);
  }
  if (fat::FindFile(path).first == nullptr) return MAKE_ERROR(Error::kNoSuchEntry);
  if (auto err = EnsureTrashDir()) return err;

  char dest[64];
  if (!UniquePath(kTrashDir, BaseName(path), dest, sizeof(dest))) {
    return MAKE_ERROR(Error::kAlreadyExists);
  }
  if (auto err = fat::MoveEntry(path, dest)) return err;
  AppendIndex(BaseName(dest), path);
  return MAKE_ERROR(Error::kSuccess);
}

Error EmptyTrash() {
  DirInfo info;
  Error result = MAKE_ERROR(Error::kSuccess);
  while (ReadDir(kTrashDir, 0, &info)) {
    char p[64];
    snprintf(p, sizeof(p), "%s/%s", kTrashDir, info.name);
    if (auto err = RemoveRecursive(p)) {
      result = err;
      break;  // 同じ項目を延々と試さない
    }
  }
  if (!result) WriteIndex("", 0);
  return result;
}

Error Restore(const char* name) {
  char from[64];
  snprintf(from, sizeof(from), "%s/%s", kTrashDir, name);
  if (fat::FindFile(from).first == nullptr) return MAKE_ERROR(Error::kNoSuchEntry);

  char orig[256];
  char parent[256] = "/home";
  if (TakeIndex(name, orig, sizeof(orig))) {
    const char* slash = strrchr(orig, '/');
    if (slash) {
      const size_t plen = slash == orig ? 1 : static_cast<size_t>(slash - orig);
      char cand[256];
      memcpy(cand, orig, plen);
      cand[plen] = '\0';
      unsigned long dummy;
      if (DirCluster(cand, &dummy)) snprintf(parent, sizeof(parent), "%s", cand);
    }
  }
  char dest[300];
  if (!UniquePath(parent, name, dest, sizeof(dest))) {
    return MAKE_ERROR(Error::kAlreadyExists);
  }
  return fat::MoveEntry(from, dest);
}

bool TrashIsEmpty() {
  DirInfo info;
  return !ReadDir(kTrashDir, 0, &info);
}

}  // namespace fsops
