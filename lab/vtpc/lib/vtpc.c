#define _GNU_SOURCE
#include "vtpc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>   // +++

static uint64_t g_read_ops = 0, g_write_ops = 0;
static uint64_t g_read_bytes = 0, g_write_bytes = 0;

#ifndef VTPC_BLOCK_SIZE
#define VTPC_BLOCK_SIZE 4096
#endif

#ifndef VTPC_DEFAULT_CACHE_PAGES
#define VTPC_DEFAULT_CACHE_PAGES 8192
#endif

#ifndef VTPC_MAX_HANDLES
#define VTPC_MAX_HANDLES 1024
#endif

#ifndef VTPC_MAX_FILE_STATES
#define VTPC_MAX_FILE_STATES 256
#endif

#ifndef VTPC_HASH_SIZE
#define VTPC_HASH_SIZE 4096
#endif

typedef struct cache_entry cache_entry_t;

typedef struct {
  int   used;
  dev_t dev;
  ino_t ino;

  int   os_fd;       // opened with O_DIRECT
  int   open_flags;  // real flags used for os_fd

  int   refcnt;
  off_t logical_size;
} file_state_t;

typedef struct {
  int used;
  file_state_t* st;
  off_t pos;
  int user_flags; // flags requested by user (not forced upgrades)
} handle_t;

struct cache_entry {
  int valid;
  int dirty;

  dev_t dev;
  ino_t ino;
  off_t block_no;

  unsigned char* data;

  cache_entry_t* prev;   // LRU
  cache_entry_t* next;   // LRU
  cache_entry_t* hnext;  // hash chain
};

/* ---------------- Global state ---------------- */

static int    g_inited     = 0;
static size_t g_block_size = VTPC_BLOCK_SIZE;
static size_t g_cache_pages = VTPC_DEFAULT_CACHE_PAGES;

// metrics (kept)
static uint64_t g_cache_hits   = 0;
static uint64_t g_cache_misses = 0;

static handle_t     g_handles[VTPC_MAX_HANDLES];
static file_state_t g_states[VTPC_MAX_FILE_STATES];

static cache_entry_t* g_entries   = NULL;
static cache_entry_t* g_free_list = NULL;
static cache_entry_t* g_lru_head  = NULL;
static cache_entry_t* g_lru_tail  = NULL;
static cache_entry_t* g_hash[VTPC_HASH_SIZE];

/* ---------------- Small helpers ---------------- */

static inline int accmode(int flags) { return flags & O_ACCMODE; }
static inline int can_write_flags(int flags) { return accmode(flags) != O_RDONLY; }

static inline int validate_fd(int fd) {
  if (fd < 0 || fd >= VTPC_MAX_HANDLES || !g_handles[fd].used) {
    errno = EBADF;
    return -1;
  }
  return 0;
}

/* ---------------- Hashing ---------------- */

static inline uint64_t mix64(uint64_t x) {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}
  
static inline size_t hash_key(dev_t dev, ino_t ino, off_t block_no) {
  uint64_t x = (uint64_t)dev;
  x = mix64(x ^ (uint64_t)ino);
  x = mix64(x ^ (uint64_t)block_no);
  return (size_t)(x % VTPC_HASH_SIZE);
}

static cache_entry_t* hash_find(dev_t dev, ino_t ino, off_t block_no) {
  for (cache_entry_t* cur = g_hash[hash_key(dev, ino, block_no)]; cur; cur = cur->hnext) {
    if (cur->valid && cur->dev == dev && cur->ino == ino && cur->block_no == block_no)
      return cur;
  }
  return NULL;
}

static void hash_insert(cache_entry_t* e) {
  size_t idx = hash_key(e->dev, e->ino, e->block_no);
  e->hnext = g_hash[idx];
  g_hash[idx] = e;
}

static void hash_remove(cache_entry_t* e) {
  size_t idx = hash_key(e->dev, e->ino, e->block_no);
  cache_entry_t *cur = g_hash[idx], *prev = NULL;

  while (cur) {
    if (cur == e) {
      if (prev) prev->hnext = cur->hnext;
      else g_hash[idx] = cur->hnext;
      cur->hnext = NULL;
      return;
    }
    prev = cur;
    cur = cur->hnext;
  }
}

/* ---------------- LRU ---------------- */

static void lru_remove(cache_entry_t* e) {
  if (!e) return;

  if (e->prev) e->prev->next = e->next;
  else g_lru_head = e->next;

  if (e->next) e->next->prev = e->prev;
  else g_lru_tail = e->prev;

  e->prev = e->next = NULL;
}

static void lru_push_front(cache_entry_t* e) {
  e->prev = NULL;
  e->next = g_lru_head;
  if (g_lru_head) g_lru_head->prev = e;
  g_lru_head = e;
  if (!g_lru_tail) g_lru_tail = e;
}

static void lru_touch(cache_entry_t* e) {
  lru_remove(e);
  lru_push_front(e);
}

/* ---------------- Low-level I/O ---------------- */

static int write_full(int fd, const void* buf, size_t count, off_t off) {
  const unsigned char* p = (const unsigned char*)buf;
  size_t left = count;

  while (left) {
    ssize_t w = pwrite(fd, p, left, off);
    if (w < 0) { if (errno == EINTR) continue; return -1; }
    if (w == 0) { errno = EIO; return -1; }

    p += (size_t)w;
    off += (off_t)w;
    left -= (size_t)w;
  }
  return 0;
}

static int read_full_block(int fd, void* buf, size_t block_size, off_t off) {
  unsigned char* p = (unsigned char*)buf;
  size_t got = 0;

  while (got < block_size) {
    ssize_t r = pread(fd, p + got, block_size - got, off + (off_t)got);
    if (r < 0) { if (errno == EINTR) continue; return -1; }
    if (r == 0) break;
    got += (size_t)r;
  }

  if (got < block_size) memset(p + got, 0, block_size - got);
  return 0;
}

/* ---------------- File states/handles ---------------- */

static file_state_t* find_state(dev_t dev, ino_t ino) {
  for (int i = 0; i < VTPC_MAX_FILE_STATES; i++)
    if (g_states[i].used && g_states[i].dev == dev && g_states[i].ino == ino)
      return &g_states[i];
  return NULL;
}

static file_state_t* alloc_state(void) {
  for (int i = 0; i < VTPC_MAX_FILE_STATES; i++) {
    if (!g_states[i].used) {
      memset(&g_states[i], 0, sizeof(g_states[i]));
      g_states[i].used = 1;
      g_states[i].os_fd = -1;
      return &g_states[i];
    }
  }
  errno = EMFILE;
  return NULL;
}

static int alloc_handle(file_state_t* st, int user_flags) {
  for (int i = 0; i < VTPC_MAX_HANDLES; i++) {
    if (!g_handles[i].used) {
      g_handles[i].used = 1;
      g_handles[i].st = st;
      g_handles[i].pos = 0;
      g_handles[i].user_flags = user_flags;
      return i;
    }
  }
  errno = EMFILE;
  return -1;
}

/* ---------------- Cache entries ---------------- */

static void free_push(cache_entry_t* e) {
  e->valid = 0;
  e->dirty = 0;
  e->dev = 0;
  e->ino = 0;
  e->block_no = 0;
  e->prev = e->next = e->hnext = NULL;

  e->next = g_free_list;
  g_free_list = e;
}

static void detach_from_structs(cache_entry_t* e) {
  if (!e->valid) return;
  hash_remove(e);
  lru_remove(e);
  e->valid = 0;
}

static int flush_entry(file_state_t* owner, cache_entry_t* e) {
  if (!e->dirty) return 0;

  if (!owner || !owner->used) { errno = EBADF; return -1; }
  if (!can_write_flags(owner->open_flags)) { errno = EBADF; return -1; }

  off_t off = (off_t)(e->block_no * (off_t)g_block_size);
  if (write_full(owner->os_fd, e->data, g_block_size, off) < 0) return -1;

  e->dirty = 0;
  return 0;
}

static cache_entry_t* alloc_entry(void) {
  if (g_free_list) {
    cache_entry_t* e = g_free_list;
    g_free_list = g_free_list->next;
    e->next = e->prev = e->hnext = NULL;
    return e;
  }

  // evict LRU tail
  cache_entry_t* victim = g_lru_tail;
  if (!victim) { errno = ENOMEM; return NULL; }

  file_state_t* owner = find_state(victim->dev, victim->ino);
  if (owner) {
    if (flush_entry(owner, victim) < 0) return NULL;
  } else if (victim->dirty) {
    errno = EIO;
    return NULL;
  }

  detach_from_structs(victim);
  victim->dirty = 0;
  victim->hnext = victim->prev = victim->next = NULL;
  return victim;
}

static void prefetch_block_nostat(file_state_t* st, off_t block_no) {
  if (hash_find(st->dev, st->ino, block_no)) return;

  off_t off = (off_t)(block_no * (off_t)g_block_size);
  if (off >= st->logical_size) return;

  cache_entry_t* e = alloc_entry();
  if (!e) return;

  e->valid = 1;
  e->dirty = 0;
  e->dev = st->dev;
  e->ino = st->ino;
  e->block_no = block_no;

  if (read_full_block(st->os_fd, e->data, g_block_size, off) < 0) {
    free_push(e);
    return;
  }

  hash_insert(e);

  lru_push_front(e);
}


static cache_entry_t* get_block(file_state_t* st, off_t block_no, int need_load) {
  cache_entry_t* e = hash_find(st->dev, st->ino, block_no);
  if (e) {
    g_cache_hits++;
    lru_touch(e);
    return e;
  }

  g_cache_misses++;

  e = alloc_entry();
  if (!e) return NULL;

  e->valid = 1;
  e->dirty = 0;
  e->dev = st->dev;
  e->ino = st->ino;
  e->block_no = block_no;

  if (need_load) {
    off_t off = (off_t)(block_no * (off_t)g_block_size);
    if (read_full_block(st->os_fd, e->data, g_block_size, off) < 0) {
      free_push(e);
      return NULL;
    }
  } else {
    memset(e->data, 0, g_block_size);
  }

    hash_insert(e);
  lru_push_front(e);

  if (need_load && g_cache_pages >= 2) {
    prefetch_block_nostat(st, block_no + 1);
  }

  return e;

}

static void invalidate_all_for_state(file_state_t* st) {
  cache_entry_t* cur = g_lru_head;
  while (cur) {
    cache_entry_t* next = cur->next;
    if (cur->valid && cur->dev == st->dev && cur->ino == st->ino) {
      detach_from_structs(cur);
      free_push(cur);
    }
    cur = next;
  }
}

static int flush_all_for_state(file_state_t* st, int force_sync) {
  int had_dirty = 0;

  for (cache_entry_t* cur = g_lru_head; cur; cur = cur->next) {
    if (cur->valid && cur->dev == st->dev && cur->ino == st->ino && cur->dirty) {
      had_dirty = 1;
      if (flush_entry(st, cur) < 0) return -1;
    }
  }

  if (can_write_flags(st->open_flags) && (force_sync || had_dirty)) {
    if (ftruncate(st->os_fd, st->logical_size) < 0) return -1;
    if (fsync(st->os_fd) < 0) return -1;
  }

  return 0;
}

/* ---------------- Init ---------------- */
static void vtpc_dump_stats_atexit(void) {
  const char* on = getenv("VTPC_PRINT_STATS");
  if (!on || !on[0] || strcmp(on, "0") == 0) return;

  uint64_t hits = g_cache_hits, miss = g_cache_misses;
  double hr = (hits + miss) ? (100.0 * (double)hits / (double)(hits + miss)) : 0.0;

  fprintf(stderr,
    "[vtpc-stats] hits=%llu misses=%llu hitrate=%.2f%% read_ops=%llu write_ops=%llu read_bytes=%llu write_bytes=%llu\n",
    (unsigned long long)hits,
    (unsigned long long)miss,
    hr,
    (unsigned long long)g_read_ops,
    (unsigned long long)g_write_ops,
    (unsigned long long)g_read_bytes,
    (unsigned long long)g_write_bytes
  );
}


static int ensure_init(void) {
  if (g_inited) return 0;

  g_entries = (cache_entry_t*)calloc(g_cache_pages, sizeof(cache_entry_t));
  if (!g_entries) return -1;

  size_t align = (size_t)sysconf(_SC_PAGESIZE);
  if (align < 512) align = 512;

  for (size_t i = 0; i < g_cache_pages; i++) {
    void* ptr = NULL;
    int rc = posix_memalign(&ptr, align, g_block_size);
    if (rc != 0) { errno = rc; return -1; }
    g_entries[i].data = (unsigned char*)ptr;
    free_push(&g_entries[i]);
  }

  memset(g_hash, 0, sizeof(g_hash));
  memset(g_handles, 0, sizeof(g_handles));
  memset(g_states, 0, sizeof(g_states));

  g_inited = 1;
  atexit(vtpc_dump_stats_atexit);
  return 0;
}

/* ---------------- Public API ---------------- */

int vtpc_open(const char* path, int mode, int access) {
  if (!path) { errno = EINVAL; return -1; }
  if (ensure_init() < 0) return -1;

  int user_flags = mode;

  // need read-modify-write for partial blocks => upgrade O_WRONLY to O_RDWR
  int real_mode = mode;
  if (accmode(real_mode) == O_WRONLY)
    real_mode = (real_mode & ~O_ACCMODE) | O_RDWR;

  real_mode |= O_DIRECT;

  int os_fd = open(path, real_mode, access);
  if (os_fd < 0) return -1;

  struct stat sb;
  if (fstat(os_fd, &sb) < 0) { close(os_fd); return -1; }

  file_state_t* st = find_state(sb.st_dev, sb.st_ino);

  if (!st) {
    st = alloc_state();
    if (!st) { close(os_fd); return -1; }

    st->dev = sb.st_dev;
    st->ino = sb.st_ino;
    st->os_fd = os_fd;
    st->open_flags = real_mode;
    st->logical_size = sb.st_size;
  } else {
    // Upgrade underlying fd to write-capable if needed
    if (!can_write_flags(st->open_flags) && can_write_flags(real_mode)) {
      close(st->os_fd);
      st->os_fd = os_fd;
      st->open_flags = real_mode;
    } else {
      close(os_fd);
    }
  }

  if (mode & O_TRUNC) {
    if (!can_write_flags(st->open_flags)) { errno = EBADF; return -1; }
    if (ftruncate(st->os_fd, 0) < 0) return -1;
    st->logical_size = 0;
    invalidate_all_for_state(st);
  }

  int h = alloc_handle(st, user_flags);
  if (h < 0) return -1;

  st->refcnt++;
  return h;
}

int vtpc_close(int fd) {
  if (validate_fd(fd) < 0) return -1;

  file_state_t* st = g_handles[fd].st;

  g_handles[fd].used = 0;
  g_handles[fd].st = NULL;
  g_handles[fd].pos = 0;
  g_handles[fd].user_flags = 0;

  if (!st || !st->used) { errno = EBADF; return -1; }

  // flush if there were writes; no forced fsync/truncate here
  if (flush_all_for_state(st, 0) < 0) return -1;

  if (--st->refcnt <= 0) {
    invalidate_all_for_state(st);

    int os_fd = st->os_fd;
    memset(st, 0, sizeof(*st));
    st->os_fd = -1;

    return close(os_fd);
  }

  return 0;
}

off_t vtpc_lseek(int fd, off_t offset, int whence) {
  if (validate_fd(fd) < 0) return (off_t)-1;

  file_state_t* st = g_handles[fd].st;
  off_t cur = g_handles[fd].pos;
  off_t base;

  switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = cur; break;
    case SEEK_END: base = st->logical_size; break;
    default: errno = EINVAL; return (off_t)-1;
  }

  off_t np = base + offset;
  if (np < 0) { errno = EINVAL; return (off_t)-1; }

  g_handles[fd].pos = np;
  return np;
}

ssize_t vtpc_read(int fd, void* buf, size_t count) {
  if (!buf && count) { errno = EINVAL; return -1; }
  if (validate_fd(fd) < 0) return -1;

  if (accmode(g_handles[fd].user_flags) == O_WRONLY) { errno = EBADF; return -1; }

  file_state_t* st = g_handles[fd].st;
  off_t pos = g_handles[fd].pos;
  if (pos >= st->logical_size) return 0;

  size_t done = 0;
  while (done < count) {
    off_t avail = st->logical_size - pos;
    if (avail <= 0) break;

    off_t block_no = pos / (off_t)g_block_size;
    size_t in_block = (size_t)(pos % (off_t)g_block_size);

    size_t n = g_block_size - in_block;
    size_t left = count - done;
    if (n > left) n = left;
    if ((off_t)n > avail) n = (size_t)avail;

    cache_entry_t* e = get_block(st, block_no, 1);
    if (!e) return -1;

    memcpy((unsigned char*)buf + done, e->data + in_block, n);
    done += n;
    pos += (off_t)n;
  }

  g_handles[fd].pos = pos;
  g_read_ops++;
  g_read_bytes += (uint64_t)done;
  return (ssize_t)done;
}

ssize_t vtpc_write(int fd, const void* buf, size_t count) {
  if (!buf && count) { errno = EINVAL; return -1; }
  if (validate_fd(fd) < 0) return -1;

  if (accmode(g_handles[fd].user_flags) == O_RDONLY) { errno = EBADF; return -1; }

  file_state_t* st = g_handles[fd].st;

  if (g_handles[fd].user_flags & O_APPEND)
    g_handles[fd].pos = st->logical_size;

  off_t pos = g_handles[fd].pos;
  size_t done = 0;

  while (done < count) {
    off_t block_no = pos / (off_t)g_block_size;
    size_t in_block = (size_t)(pos % (off_t)g_block_size);

    size_t n = g_block_size - in_block;
    size_t left = count - done;
    if (n > left) n = left;

    int full_overwrite = (in_block == 0 && n == g_block_size);

    cache_entry_t* e = get_block(st, block_no, !full_overwrite);
    if (!e) return -1;

    memcpy(e->data + in_block, (const unsigned char*)buf + done, n);
    e->dirty = 1;
    lru_touch(e);

    done += n;
    pos += (off_t)n;
    if (pos > st->logical_size) st->logical_size = pos;
  }

  g_handles[fd].pos = pos;
  g_write_ops++;
  g_write_bytes += (uint64_t)done;
  return (ssize_t)done;
}

int vtpc_fsync(int fd) {
  if (validate_fd(fd) < 0) return -1;
  return flush_all_for_state(g_handles[fd].st, 1);
}

/* ---- Metrics (kept) ---- */
uint64_t vtpc_cache_hits(void)   { return g_cache_hits; }
uint64_t vtpc_cache_misses(void) { return g_cache_misses; }
void vtpc_cache_reset_stats(void) { g_cache_hits = g_cache_misses = 0; }
