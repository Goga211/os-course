#define _GNU_SOURCE
#include "vtpc.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Configuration
#ifndef VTPC_BLOCK_SIZE
#define VTPC_BLOCK_SIZE 4096
#endif

#ifndef VTPC_DEFAULT_CACHE_PAGES
#define VTPC_DEFAULT_CACHE_PAGES 128
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

// Types
typedef struct cache_entry cache_entry_t;

typedef struct {
  int used;
  dev_t dev;
  ino_t ino;
  int os_fd;
  int open_flags;
  int refcnt;
  off_t logical_size;
} file_state_t;

typedef struct {
  int used;
  file_state_t* st;
  off_t pos;
  int user_flags;
} handle_t;

struct cache_entry {
  int valid;
  int dirty;
  dev_t dev;
  ino_t ino;
  off_t block_no;
  unsigned char* data;
  cache_entry_t* prev;
  cache_entry_t* next;
  cache_entry_t* hnext;
};

// Global state
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_inited = 0;
static size_t g_block_size = VTPC_BLOCK_SIZE;
static size_t g_cache_pages = VTPC_DEFAULT_CACHE_PAGES;

static handle_t g_handles[VTPC_MAX_HANDLES];
static file_state_t g_states[VTPC_MAX_FILE_STATES];
static cache_entry_t* g_entries = NULL;
static cache_entry_t* g_free_list = NULL;
static cache_entry_t* g_lru_head = NULL;
static cache_entry_t* g_lru_tail = NULL;
static cache_entry_t* g_hash[VTPC_HASH_SIZE];

// Helper macros
#define VALIDATE_FD(fd) \
  do { \
    if ((fd) < 0 || (fd) >= VTPC_MAX_HANDLES || !g_handles[(fd)].used) { \
      errno = EBADF; \
      return -1; \
    } \
  } while (0)

// Hash function
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

// LRU list operations
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

// Hash table operations
static void hash_remove(cache_entry_t* e) {
  size_t idx = hash_key(e->dev, e->ino, e->block_no);
  cache_entry_t* cur = g_hash[idx];
  cache_entry_t* prev = NULL;
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

static void hash_insert(cache_entry_t* e) {
  size_t idx = hash_key(e->dev, e->ino, e->block_no);
  e->hnext = g_hash[idx];
  g_hash[idx] = e;
}

static cache_entry_t* hash_find(dev_t dev, ino_t ino, off_t block_no) {
  size_t idx = hash_key(dev, ino, block_no);
  cache_entry_t* cur = g_hash[idx];
  while (cur) {
    if (cur->valid && cur->dev == dev && cur->ino == ino && cur->block_no == block_no)
      return cur;
    cur = cur->hnext;
  }
  return NULL;
}

// I/O helpers
static int write_full(int fd, const void* buf, size_t count, off_t off) {
  const unsigned char* p = (const unsigned char*)buf;
  size_t left = count;
  while (left > 0) {
    ssize_t w = pwrite(fd, p, left, off);
    if (w < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (w == 0) {
      errno = EIO;
      return -1;
    }
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
    if (r < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (r == 0) break;
    got += (size_t)r;
  }
  if (got < block_size) memset(p + got, 0, block_size - got);
  return 0;
}

// File state management
static file_state_t* find_state_locked(dev_t dev, ino_t ino) {
  for (int i = 0; i < VTPC_MAX_FILE_STATES; i++) {
    if (g_states[i].used && g_states[i].dev == dev && g_states[i].ino == ino)
      return &g_states[i];
  }
  return NULL;
}

static file_state_t* alloc_state_locked(void) {
  for (int i = 0; i < VTPC_MAX_FILE_STATES; i++) {
    if (!g_states[i].used) {
      g_states[i].used = 1;
      g_states[i].os_fd = -1;
      g_states[i].open_flags = 0;
      g_states[i].refcnt = 0;
      g_states[i].logical_size = 0;
      return &g_states[i];
    }
  }
  errno = EMFILE;
  return NULL;
}

static int alloc_handle_locked(file_state_t* st, int user_flags) {
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

// Cache entry management
static void invalidate_entry_locked(cache_entry_t* e) {
  hash_remove(e);
  lru_remove(e);
  e->valid = 0;
  e->dirty = 0;
  e->dev = 0;
  e->ino = 0;
  e->block_no = 0;
  e->hnext = NULL;
  e->next = g_free_list;
  e->prev = NULL;
  g_free_list = e;
}

static int flush_entry_locked(file_state_t* owner, cache_entry_t* e) {
  if (!e->dirty) return 0;
  if (!owner) {
    errno = EBADF;
    return -1;
  }
  off_t off = (off_t)(e->block_no * (off_t)g_block_size);
  if (write_full(owner->os_fd, e->data, g_block_size, off) < 0) return -1;
  e->dirty = 0;
  return 0;
}

static cache_entry_t* alloc_cache_entry_locked(void) {
  if (g_free_list) {
    cache_entry_t* e = g_free_list;
    g_free_list = g_free_list->next;
    e->next = e->prev = e->hnext = NULL;
    return e;
  }

  cache_entry_t* e = g_lru_tail;
  if (!e) {
    errno = ENOMEM;
    return NULL;
  }

  file_state_t* owner = find_state_locked(e->dev, e->ino);
  if (owner) {
    if (flush_entry_locked(owner, e) < 0) return NULL;
  } else if (e->dirty) {
    errno = EIO;
    return NULL;
  }

  invalidate_entry_locked(e);
  e = g_free_list;
  g_free_list = g_free_list->next;
  e->next = e->prev = e->hnext = NULL;
  return e;
}

static cache_entry_t* get_block_locked(file_state_t* st, off_t block_no, int need_load) {
  cache_entry_t* e = hash_find(st->dev, st->ino, block_no);
  if (e) {
    lru_touch(e);
    return e;
  }

  e = alloc_cache_entry_locked();
  if (!e) return NULL;

  e->valid = 1;
  e->dirty = 0;
  e->dev = st->dev;
  e->ino = st->ino;
  e->block_no = block_no;

  if (need_load) {
    off_t off = (off_t)(block_no * (off_t)g_block_size);
    if (read_full_block(st->os_fd, e->data, g_block_size, off) < 0) {
      e->valid = 0;
      e->next = g_free_list;
      g_free_list = e;
      return NULL;
    }
  } else {
    memset(e->data, 0, g_block_size);
  }

  hash_insert(e);
  lru_push_front(e);
  return e;
}

static int flush_all_for_state_locked(file_state_t* st) {
  cache_entry_t* cur = g_lru_head;
  while (cur) {
    cache_entry_t* next = cur->next;
    if (cur->valid && cur->dev == st->dev && cur->ino == st->ino) {
      if (flush_entry_locked(st, cur) < 0) return -1;
    }
    cur = next;
  }

  // O_DIRECT writes whole blocks, so file may be larger than logical_size
  if (ftruncate(st->os_fd, st->logical_size) < 0) return -1;
  if (fsync(st->os_fd) < 0) return -1;
  return 0;
}

static void invalidate_all_for_state_locked(file_state_t* st) {
  cache_entry_t* cur = g_lru_head;
  while (cur) {
    cache_entry_t* next = cur->next;
    if (cur->valid && cur->dev == st->dev && cur->ino == st->ino)
      invalidate_entry_locked(cur);
    cur = next;
  }
}

static void apply_truncate_locked(file_state_t* st) {
  if (ftruncate(st->os_fd, 0) < 0) return;
  st->logical_size = 0;
  invalidate_all_for_state_locked(st);
}

// Initialization
static int ensure_init_locked(void) {
  if (g_inited) return 0;

  const char* env_pages = getenv("VTPC_CACHE_PAGES");
  if (env_pages && env_pages[0]) {
    long v = strtol(env_pages, NULL, 10);
    if (v > 0 && v < 100000) g_cache_pages = (size_t)v;
  }

  g_entries = (cache_entry_t*)calloc(g_cache_pages, sizeof(cache_entry_t));
  if (!g_entries) return -1;

  size_t align = (size_t)sysconf(_SC_PAGESIZE);
  if (align < 512) align = 512;

  for (size_t i = 0; i < g_cache_pages; i++) {
    void* ptr = NULL;
    int rc = posix_memalign(&ptr, align, g_block_size);
    if (rc != 0) {
      errno = rc;
      return -1;
    }
    g_entries[i].data = (unsigned char*)ptr;
    g_entries[i].next = g_free_list;
    g_free_list = &g_entries[i];
  }

  memset(g_hash, 0, sizeof(g_hash));
  memset(g_handles, 0, sizeof(g_handles));
  memset(g_states, 0, sizeof(g_states));

  g_inited = 1;
  return 0;
}

// Public API
int vtpc_open(const char* path, int mode, int access) {
  if (!path) {
    errno = EINVAL;
    return -1;
  }

  pthread_mutex_lock(&g_lock);
  if (ensure_init_locked() < 0) {
    pthread_mutex_unlock(&g_lock);
    return -1;
  }
  pthread_mutex_unlock(&g_lock);

  int user_flags = mode;
  int real_mode = mode;

  // For partial writes we need to read the block, so convert O_WRONLY to O_RDWR
  if ((mode & O_ACCMODE) == O_WRONLY) {
    real_mode = (mode & ~O_ACCMODE) | O_RDWR;
  }

  real_mode |= O_DIRECT;

  int os_fd = open(path, real_mode, access);
  if (os_fd < 0) return -1;

  struct stat sb;
  if (fstat(os_fd, &sb) < 0) {
    close(os_fd);
    return -1;
  }

  pthread_mutex_lock(&g_lock);

  file_state_t* st = find_state_locked(sb.st_dev, sb.st_ino);
  if (!st) {
    st = alloc_state_locked();
    if (!st) {
      pthread_mutex_unlock(&g_lock);
      close(os_fd);
      return -1;
    }

    st->dev = sb.st_dev;
    st->ino = sb.st_ino;
    st->os_fd = os_fd;
    st->open_flags = real_mode;
    st->refcnt = 0;
    st->logical_size = sb.st_size;

    if (mode & O_TRUNC) {
      apply_truncate_locked(st);
    }
  } else {
    close(os_fd);
    if (mode & O_TRUNC) {
      apply_truncate_locked(st);
    }
  }

  int h = alloc_handle_locked(st, user_flags);
  if (h < 0) {
    pthread_mutex_unlock(&g_lock);
    return -1;
  }

  st->refcnt++;
  pthread_mutex_unlock(&g_lock);
  return h;
}

int vtpc_close(int fd) {
  pthread_mutex_lock(&g_lock);

  if (fd < 0 || fd >= VTPC_MAX_HANDLES || !g_handles[fd].used) {
    pthread_mutex_unlock(&g_lock);
    errno = EBADF;
    return -1;
  }

  file_state_t* st = g_handles[fd].st;
  g_handles[fd].used = 0;
  g_handles[fd].st = NULL;
  g_handles[fd].pos = 0;
  g_handles[fd].user_flags = 0;

  if (!st || !st->used) {
    pthread_mutex_unlock(&g_lock);
    errno = EBADF;
    return -1;
  }

  if (flush_all_for_state_locked(st) < 0) {
    pthread_mutex_unlock(&g_lock);
    return -1;
  }

  st->refcnt--;
  if (st->refcnt <= 0) {
    invalidate_all_for_state_locked(st);
    int os_fd = st->os_fd;
    st->used = 0;
    st->os_fd = -1;
    st->open_flags = 0;
    st->refcnt = 0;
    st->dev = 0;
    st->ino = 0;
    st->logical_size = 0;
    pthread_mutex_unlock(&g_lock);
    return close(os_fd);
  }

  pthread_mutex_unlock(&g_lock);
  return 0;
}

off_t vtpc_lseek(int fd, off_t offset, int whence) {
  pthread_mutex_lock(&g_lock);

  if (fd < 0 || fd >= VTPC_MAX_HANDLES || !g_handles[fd].used) {
    pthread_mutex_unlock(&g_lock);
    errno = EBADF;
    return (off_t)-1;
  }

  file_state_t* st = g_handles[fd].st;
  off_t cur = g_handles[fd].pos;
  off_t base;

  switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = cur; break;
    case SEEK_END: base = st->logical_size; break;
    default:
      pthread_mutex_unlock(&g_lock);
      errno = EINVAL;
      return (off_t)-1;
  }

  off_t np = base + offset;
  if (np < 0) {
    pthread_mutex_unlock(&g_lock);
    errno = EINVAL;
    return (off_t)-1;
  }

  g_handles[fd].pos = np;
  if (lseek(st->os_fd, np, SEEK_SET) == (off_t)-1) {
    g_handles[fd].pos = cur;
    pthread_mutex_unlock(&g_lock);
    return (off_t)-1;
  }

  pthread_mutex_unlock(&g_lock);
  return np;
}

ssize_t vtpc_read(int fd, void* buf, size_t count) {
  if (!buf && count > 0) {
    errno = EINVAL;
    return -1;
  }

  pthread_mutex_lock(&g_lock);

  if (fd < 0 || fd >= VTPC_MAX_HANDLES || !g_handles[fd].used) {
    pthread_mutex_unlock(&g_lock);
    errno = EBADF;
    return -1;
  }

  if ((g_handles[fd].user_flags & O_ACCMODE) == O_WRONLY) {
    pthread_mutex_unlock(&g_lock);
    errno = EBADF;
    return -1;
  }

  file_state_t* st = g_handles[fd].st;
  off_t pos = g_handles[fd].pos;

  if (pos >= st->logical_size) {
    pthread_mutex_unlock(&g_lock);
    return 0;
  }

  size_t done = 0;
  while (done < count) {
    off_t avail = st->logical_size - pos;
    if (avail <= 0) break;

    off_t block_no = pos / (off_t)g_block_size;
    size_t in_block = (size_t)(pos % (off_t)g_block_size);
    size_t need = g_block_size - in_block;
    size_t left = count - done;
    if (need > left) need = left;
    if ((off_t)need > avail) need = (size_t)avail;

    cache_entry_t* e = get_block_locked(st, block_no, 1);
    if (!e) {
      pthread_mutex_unlock(&g_lock);
      return -1;
    }

    memcpy((unsigned char*)buf + done, e->data + in_block, need);
    done += need;
    pos += (off_t)need;
  }

  g_handles[fd].pos = pos;
  pthread_mutex_unlock(&g_lock);
  return (ssize_t)done;
}

ssize_t vtpc_write(int fd, const void* buf, size_t count) {
  if (!buf && count > 0) {
    errno = EINVAL;
    return -1;
  }

  pthread_mutex_lock(&g_lock);

  if (fd < 0 || fd >= VTPC_MAX_HANDLES || !g_handles[fd].used) {
    pthread_mutex_unlock(&g_lock);
    errno = EBADF;
    return -1;
  }

  if ((g_handles[fd].user_flags & O_ACCMODE) == O_RDONLY) {
    pthread_mutex_unlock(&g_lock);
    errno = EBADF;
    return -1;
  }

  file_state_t* st = g_handles[fd].st;

  if (g_handles[fd].user_flags & O_APPEND) {
    g_handles[fd].pos = st->logical_size;
  }

  off_t pos = g_handles[fd].pos;
  size_t done = 0;

  while (done < count) {
    off_t block_no = pos / (off_t)g_block_size;
    size_t in_block = (size_t)(pos % (off_t)g_block_size);
    size_t need = g_block_size - in_block;
    size_t left = count - done;
    if (need > left) need = left;

    int full_block_overwrite = (in_block == 0 && need == g_block_size);
    cache_entry_t* e = get_block_locked(st, block_no, !full_block_overwrite);
    if (!e) {
      pthread_mutex_unlock(&g_lock);
      return -1;
    }

    memcpy(e->data + in_block, (const unsigned char*)buf + done, need);
    e->dirty = 1;
    lru_touch(e);

    done += need;
    pos += (off_t)need;
    if (pos > st->logical_size) st->logical_size = pos;
  }

  g_handles[fd].pos = pos;
  pthread_mutex_unlock(&g_lock);
  return (ssize_t)done;
}

int vtpc_fsync(int fd) {
  pthread_mutex_lock(&g_lock);

  if (fd < 0 || fd >= VTPC_MAX_HANDLES || !g_handles[fd].used) {
    pthread_mutex_unlock(&g_lock);
    errno = EBADF;
    return -1;
  }

  file_state_t* st = g_handles[fd].st;
  int rc = flush_all_for_state_locked(st);

  pthread_mutex_unlock(&g_lock);
  return rc;
}