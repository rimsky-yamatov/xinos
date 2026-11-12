#include "fat.hpp"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <utility>

namespace {

std::pair<const char*, bool>
NextPathElement(const char* path, char* path_elem) {
  const char* next_slash = strchr(path, '/');
  if (next_slash == nullptr) {
    if (strlen(path) > 12) {
      path_elem[0] = '\0';
    } else {
      strcpy(path_elem, path);
    }
    return { nullptr, false };
  }

  const auto elem_len = next_slash - path;
  if (elem_len > 12) {
    path_elem[0] = '\0';
  } else {
    strncpy(path_elem, path, elem_len);
    path_elem[elem_len] = '\0';
  }
  return { &next_slash[1], true };
}

} // namespace

namespace fat {

BPB* boot_volume_image;
unsigned long bytes_per_cluster;

void Initialize(void* volume_image) {
  boot_volume_image = reinterpret_cast<fat::BPB*>(volume_image);
  bytes_per_cluster =
    static_cast<unsigned long>(boot_volume_image->bytes_per_sector) *
    boot_volume_image->sectors_per_cluster;
}

uintptr_t GetClusterAddr(unsigned long cluster) {
  unsigned long sector_num =
    boot_volume_image->reserved_sector_count +
    boot_volume_image->num_fats * boot_volume_image->fat_size_32 +
    (cluster - 2) * boot_volume_image->sectors_per_cluster;
  uintptr_t offset = sector_num * boot_volume_image->bytes_per_sector;
  return reinterpret_cast<uintptr_t>(boot_volume_image) + offset;
}

void ReadName(const DirectoryEntry& entry, char* base, char* ext) {
  memcpy(base, &entry.name[0], 8);
  base[8] = 0;
  for (int i = 7; i >= 0 && base[i] == 0x20; --i) {
    base[i] = 0;
  }

  memcpy(ext, &entry.name[8], 3);
  ext[3] = 0;
  for (int i = 2; i >= 0 && ext[i] == 0x20; --i) {
    ext[i] = 0;
  }
}

void FormatName(const DirectoryEntry& entry, char* dest) {
  char ext[5] = ".";
  ReadName(entry, dest, &ext[1]);
  if (ext[1]) {
    strcat(dest, ext);
  }
}

void FormatWriteTime(const DirectoryEntry& entry, char* dest) {
  int year = ((entry.write_date >> 9) & 0x3f) + 1980;
  int month = (entry.write_date >> 5) & 0xf;
  int date = entry.write_date & 0x1f;
  int hour = (entry.write_time >> 11) & 0x1f;
  int minute = (entry.write_time >> 5) & 0x3f;
  int second = (entry.write_time & 0x1f) * 2;

  auto int_to_str = [](char* out, int value, int num_digits) {
    for (int i = 0; i < num_digits; i++) {
      out[num_digits - 1 - i] = '0' + (value % 10);
      value /= 10;
    }
  };
  int_to_str(&dest[0], year, 4);
  dest[4] = '-';
  int_to_str(&dest[5], month, 2);
  dest[7] = '-';
  int_to_str(&dest[8], date, 2);
  dest[10] = ' ';
  int_to_str(&dest[11], hour, 2);
  dest[13] = ':';
  int_to_str(&dest[14], minute, 2);
  dest[16] = ':';
  int_to_str(&dest[17], second, 2);
  dest[19] = '\0';
}

unsigned long NextCluster(unsigned long cluster) {
  uint32_t next = GetFAT()[cluster];
  if (IsEndOfClusterchain(next)) {
    return kEndOfClusterchain;
  }
  return next;
}

std::pair<DirectoryEntry*, bool>
FindFile(const char* path, unsigned long directory_cluster) {
  if (path[0] == '/') {
    directory_cluster = boot_volume_image->root_cluster;
    ++path;
  } else if (directory_cluster == 0) {
    directory_cluster = boot_volume_image->root_cluster;
  }

  char path_elem[13];
  const auto [ next_path, post_slash ] = NextPathElement(path, path_elem);
  const bool path_last = next_path == nullptr || next_path[0] == '\0';

  while (directory_cluster != kEndOfClusterchain) {
    auto dir = GetSectorByCluster<DirectoryEntry>(directory_cluster);
    for (int i = 0; i < bytes_per_cluster / sizeof(DirectoryEntry); ++i) {
      if (dir[i].name[0] == 0x00) {
        goto not_found;
      } else if (!NameIsEqual(dir[i], path_elem)) {
        continue;
      }

      if (dir[i].attr == Attribute::kDirectory && !path_last) {
        return FindFile(next_path, dir[i].FirstCluster());
      } else {
        // dir[i] がディレクトリではないか，パスの末尾に来てしまったので探索をやめる
        return { &dir[i], post_slash };
      }
    }

    directory_cluster = NextCluster(directory_cluster);
  }

not_found:
  return { nullptr, post_slash };
}

bool NameIsEqual(const DirectoryEntry& entry, const char* name) {
  unsigned char name83[11];
  memset(name83, 0x20, sizeof(name83));

  int i = 0;
  int i83 = 0;
  bool found_dot = false;
  for (; name[i] != 0 && i83 < sizeof(name83); ++i, ++i83) {
    if (name[i] == '.') {
      if (found_dot) return false; // ドットが2個以上ある
      i83 = 7;
      found_dot = true;
      continue;
    }
    if (!found_dot && i > 7) return false; // ドットの前に9文字以上ある
    name83[i83] = toupper(name[i]);
  }

  return name[i] == 0 && memcmp(entry.name, name83, sizeof(name83)) == 0;
}

size_t LoadFile(void* buf, size_t len, DirectoryEntry& entry) {
  return FileDescriptor{entry}.Read(buf, len);
}

bool IsEndOfClusterchain(unsigned long cluster) {
  return cluster >= 0x0ffffff8ul;
}

uint32_t* GetFAT() {
  uintptr_t fat_offset =
    boot_volume_image->reserved_sector_count *
    boot_volume_image->bytes_per_sector;
  return reinterpret_cast<uint32_t*>(
      reinterpret_cast<uintptr_t>(boot_volume_image) + fat_offset);
}

unsigned long ExtendCluster(unsigned long eoc_cluster, size_t n) {
  uint32_t* fat = GetFAT();
  while (!IsEndOfClusterchain(fat[eoc_cluster])) {
    eoc_cluster = fat[eoc_cluster];
  }

  size_t num_allocated = 0;
  auto current = eoc_cluster;

  for (unsigned long candidate = 2; num_allocated < n; ++candidate) {
    if (fat[candidate] != 0) { // candidate cluster is not free
      continue;
    }
    fat[current] = candidate;
    current = candidate;
    ++num_allocated;
  }
  fat[current] = kEndOfClusterchain;
  return current;
}

DirectoryEntry* AllocateEntry(unsigned long dir_cluster) {
  while (true) {
    auto dir = GetSectorByCluster<DirectoryEntry>(dir_cluster);
    for (int i = 0; i < bytes_per_cluster / sizeof(DirectoryEntry); ++i) {
      if (dir[i].name[0] == 0 || dir[i].name[0] == 0xe5) {
        return &dir[i];
      }
    }
    auto next = NextCluster(dir_cluster);
    if (next == kEndOfClusterchain) {
      break;
    }
    dir_cluster = next;
  }

  dir_cluster = ExtendCluster(dir_cluster, 1);
  auto dir = GetSectorByCluster<DirectoryEntry>(dir_cluster);
  memset(dir, 0, bytes_per_cluster);
  return &dir[0];
}

void SetFileName(DirectoryEntry& entry, const char* name) {
  const char* dot_pos = strrchr(name, '.');
  memset(entry.name, ' ', 8+3);
  if (dot_pos) {
    for (int i = 0; i < 8 && i < dot_pos - name; ++i) {
      entry.name[i] = toupper(name[i]);
    }
    for (int i = 0; i < 3 && dot_pos[i + 1]; ++i) {
      entry.name[8 + i] = toupper(dot_pos[i + 1]);
    }
  } else {
    for (int i = 0; i < 8 && name[i]; ++i) {
      entry.name[i] = toupper(name[i]);
    }
  }
}

WithError<DirectoryEntry*> CreateFile(const char* path) {
  auto parent_dir_cluster = fat::boot_volume_image->root_cluster;
  const char* filename = path;

  if (const char* slash_pos = strrchr(path, '/')) {
    filename = &slash_pos[1];
    if (slash_pos[1] == '\0') {
      return { nullptr, MAKE_ERROR(Error::kIsDirectory) };
    }

    char parent_dir_name[slash_pos - path + 1];
    strncpy(parent_dir_name, path, slash_pos - path);
    parent_dir_name[slash_pos - path] = '\0';

    if (parent_dir_name[0] != '\0') {
      auto [ parent_dir, post_slash2 ] = fat::FindFile(parent_dir_name);
      if (parent_dir == nullptr) {
        return { nullptr, MAKE_ERROR(Error::kNoSuchEntry) };
      }
      parent_dir_cluster = parent_dir->FirstCluster();
    }
  }

  auto dir = fat::AllocateEntry(parent_dir_cluster);
  if (dir == nullptr) {
    return { nullptr, MAKE_ERROR(Error::kNoEnoughMemory) };
  }
  memset(dir, 0, sizeof(*dir));  // 削除済みエントリの残骸を消す
  fat::SetFileName(*dir, filename);
  dir->file_size = 0;
  return { dir, MAKE_ERROR(Error::kSuccess) };
}

unsigned long AllocateClusterChain(size_t n) {
  uint32_t* fat = GetFAT();
  unsigned long first_cluster;
  for (first_cluster = 2; ; ++first_cluster) {
    if (fat[first_cluster] == 0) {
      fat[first_cluster] = kEndOfClusterchain;
      break;
    }
  }

  if (n > 1) {
    ExtendCluster(first_cluster, n - 1);
  }
  return first_cluster;
}

FileDescriptor::FileDescriptor(DirectoryEntry& fat_entry)
    : fat_entry_{fat_entry} {
}

size_t FileDescriptor::Read(void* buf, size_t len) {
  if (rd_cluster_ == 0) {
    rd_cluster_ = fat_entry_.FirstCluster();
  }
  uint8_t* buf8 = reinterpret_cast<uint8_t*>(buf);
  len = std::min(len, fat_entry_.file_size - rd_off_);

  size_t total = 0;
  while (total < len) {
    uint8_t* sec = GetSectorByCluster<uint8_t>(rd_cluster_);
    size_t n = std::min(len - total, bytes_per_cluster - rd_cluster_off_);
    memcpy(&buf8[total], &sec[rd_cluster_off_], n);
    total += n;

    rd_cluster_off_ += n;
    if (rd_cluster_off_ == bytes_per_cluster) {
      rd_cluster_ = NextCluster(rd_cluster_);
      rd_cluster_off_ = 0;
    }
  }

  rd_off_ += total;
  return total;
}

size_t FileDescriptor::Write(const void* buf, size_t len) {
  auto num_cluster = [](size_t bytes) {
    return (bytes + bytes_per_cluster - 1) / bytes_per_cluster;
  };

  if (wr_cluster_ == 0) {
    if (fat_entry_.FirstCluster() != 0) {
      wr_cluster_ = fat_entry_.FirstCluster();
    } else {
      wr_cluster_ = AllocateClusterChain(num_cluster(len));
      fat_entry_.first_cluster_low = wr_cluster_ & 0xffff;
      fat_entry_.first_cluster_high = (wr_cluster_ >> 16) & 0xffff;
    }
  }

  const uint8_t* buf8 = reinterpret_cast<const uint8_t*>(buf);

  size_t total = 0;
  while (total < len) {
    if (wr_cluster_off_ == bytes_per_cluster) {
      const auto next_cluster = NextCluster(wr_cluster_);
      if (next_cluster == kEndOfClusterchain) {
        wr_cluster_ = ExtendCluster(wr_cluster_, num_cluster(len - total));
      } else {
        wr_cluster_ = next_cluster;
      }
      wr_cluster_off_ = 0;
    }

    uint8_t* sec = GetSectorByCluster<uint8_t>(wr_cluster_);
    size_t n = std::min(len - total, bytes_per_cluster - wr_cluster_off_);
    memcpy(&sec[wr_cluster_off_], &buf8[total], n);
    total += n;

    wr_cluster_off_ += n;
  }

  wr_off_ += total;
  fat_entry_.file_size = wr_off_;
  return total;
}

size_t FileDescriptor::Load(void* buf, size_t len, size_t offset) {
  FileDescriptor fd{fat_entry_};
  fd.rd_off_ = offset;

  unsigned long cluster = fat_entry_.FirstCluster();
  while (offset >= bytes_per_cluster) {
    offset -= bytes_per_cluster;
    cluster = NextCluster(cluster);
  }

  fd.rd_cluster_ = cluster;
  fd.rd_cluster_off_ = offset;
  return fd.Read(buf, len);
}


namespace {

void SetFirstCluster(DirectoryEntry& e, unsigned long c) {
  e.first_cluster_low = c & 0xffff;
  e.first_cluster_high = (c >> 16) & 0xffff;
}

// path の親ディレクトリの開始クラスタと、末尾の名前を求める。
Error ResolveParent(const char* path, unsigned long* parent, const char** name) {
  *parent = boot_volume_image->root_cluster;
  *name = path;
  if (const char* slash = strrchr(path, '/')) {
    *name = &slash[1];
    char parent_name[256];
    const size_t len = slash - path;
    if (len >= sizeof(parent_name)) {
      return MAKE_ERROR(Error::kInvalidFile);
    }
    memcpy(parent_name, path, len);
    parent_name[len] = '\0';
    if (parent_name[0] != '\0') {
      auto [ dir, post_slash ] = FindFile(parent_name);
      if (dir == nullptr) {
        return MAKE_ERROR(Error::kNoSuchEntry);
      }
      if (dir->attr != Attribute::kDirectory) {
        return MAKE_ERROR(Error::kNotDirectory);
      }
      *parent = dir->FirstCluster();
    }
  }
  return MAKE_ERROR(Error::kSuccess);
}

void FreeClusterChain(unsigned long cluster) {
  uint32_t* fat = GetFAT();
  while (cluster >= 2 && !IsEndOfClusterchain(cluster)) {
    const unsigned long next = fat[cluster];
    fat[cluster] = 0;
    cluster = next;
  }
}

bool DirectoryIsEmpty(unsigned long cluster) {
  while (cluster != kEndOfClusterchain) {
    auto dir = GetSectorByCluster<DirectoryEntry>(cluster);
    for (size_t i = 0; i < bytes_per_cluster / sizeof(DirectoryEntry); ++i) {
      if (dir[i].name[0] == 0x00) return true;
      if (dir[i].name[0] == 0xe5 || dir[i].name[0] == '.') continue;
      if (dir[i].attr == Attribute::kLongName) continue;
      return false;
    }
    cluster = NextCluster(cluster);
  }
  return true;
}

}  // namespace

bool IsValidShortName(const char* name) {
  const size_t len = strlen(name);
  if (len == 0 || len > 12 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
    return false;
  }
  const char* dot = strchr(name, '.');
  if (dot && strchr(dot + 1, '.')) return false;
  const size_t base = dot ? dot - name : len;
  const size_t ext = dot ? len - base - 1 : 0;
  if (base == 0 || base > 8 || ext > 3) return false;
  for (const char* p = name; *p; ++p) {
    if (!isalnum(static_cast<unsigned char>(*p)) && *p != '_' && *p != '-' &&
        *p != '.') {
      return false;
    }
  }
  return true;
}

WithError<DirectoryEntry*> CreateDirectory(const char* path) {
  unsigned long parent;
  const char* name;
  if (auto err = ResolveParent(path, &parent, &name)) {
    return { nullptr, err };
  }
  if (!IsValidShortName(name)) {
    return { nullptr, MAKE_ERROR(Error::kInvalidFile) };
  }
  if (FindFile(path).first != nullptr) {
    return { nullptr, MAKE_ERROR(Error::kAlreadyExists) };
  }

  const unsigned long cluster = AllocateClusterChain(1);
  auto block = GetSectorByCluster<DirectoryEntry>(cluster);
  memset(block, 0, bytes_per_cluster);
  memset(block[0].name, ' ', 11);
  block[0].name[0] = '.';
  block[0].attr = Attribute::kDirectory;
  SetFirstCluster(block[0], cluster);
  memset(block[1].name, ' ', 11);
  block[1].name[0] = '.';
  block[1].name[1] = '.';
  block[1].attr = Attribute::kDirectory;
  SetFirstCluster(block[1], parent == boot_volume_image->root_cluster ? 0 : parent);

  auto entry = AllocateEntry(parent);
  if (entry == nullptr) {
    FreeClusterChain(cluster);
    return { nullptr, MAKE_ERROR(Error::kNoEnoughMemory) };
  }
  memset(entry, 0, sizeof(*entry));
  SetFileName(*entry, name);
  entry->attr = Attribute::kDirectory;
  SetFirstCluster(*entry, cluster);
  entry->file_size = 0;
  return { entry, MAKE_ERROR(Error::kSuccess) };
}

Error RemoveEntry(const char* path) {
  auto [ entry, post_slash ] = FindFile(path);
  if (entry == nullptr) {
    return MAKE_ERROR(Error::kNoSuchEntry);
  }
  if (entry->attr == Attribute::kDirectory) {
    if (!DirectoryIsEmpty(entry->FirstCluster())) {
      return MAKE_ERROR(Error::kNotEmpty);
    }
  } else if (post_slash) {
    return MAKE_ERROR(Error::kNotDirectory);
  }
  FreeClusterChain(entry->FirstCluster());
  entry->name[0] = 0xe5;
  return MAKE_ERROR(Error::kSuccess);
}

Error MoveEntry(const char* src, const char* dst) {
  auto [ s, post_slash ] = FindFile(src);
  if (s == nullptr) {
    return MAKE_ERROR(Error::kNoSuchEntry);
  }
  unsigned long parent;
  const char* name;
  if (auto err = ResolveParent(dst, &parent, &name)) {
    return err;
  }
  if (!IsValidShortName(name)) {
    return MAKE_ERROR(Error::kInvalidFile);
  }
  if (FindFile(dst).first != nullptr) {
    return MAKE_ERROR(Error::kAlreadyExists);
  }
  auto entry = AllocateEntry(parent);
  if (entry == nullptr) {
    return MAKE_ERROR(Error::kNoEnoughMemory);
  }
  *entry = *s;
  SetFileName(*entry, name);
  s->name[0] = 0xe5;
  if (entry->attr == Attribute::kDirectory && entry->FirstCluster() >= 2) {
    // 移動先に合わせて ".." を付け替える
    auto block = GetSectorByCluster<DirectoryEntry>(entry->FirstCluster());
    SetFirstCluster(block[1], parent == boot_volume_image->root_cluster ? 0 : parent);
  }
  return MAKE_ERROR(Error::kSuccess);
}

} // namespace fat
