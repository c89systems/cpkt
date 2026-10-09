#include <cpkt/libssh2.h>
#include <libssh2_sftp.h>

#include <stdio.h>
#include <string.h>

/* Exercise the generated bridge with defined poison, never stack garbage. */
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "SFTP attribute check failed at line %d\n", __LINE__);   \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static unsigned long backend_flags;
static int backend_result;
static int backend_empty;
static int expect_null;
static int backend_bad;
static int backend_calls;
static int handle_token;
static int sftp_token;
static int opened_token;
static const char path[] = "file";
static char name[8];

static libssh2_uint64_t size_value(void) {
  return ((libssh2_uint64_t)0x12345678UL << 32) | 0x9abcdef0UL;
}

static void output_attributes(LIBSSH2_SFTP_ATTRIBUTES *attrs) {
  ++backend_calls;
  if ((attrs == NULL) != expect_null)
    backend_bad = 1;
  if (attrs == NULL || backend_empty)
    return;
  memset(attrs, 0x5a, sizeof(*attrs));
  attrs->flags = backend_flags;
  if (backend_flags & LIBSSH2_SFTP_ATTR_SIZE)
    attrs->filesize = size_value();
  if (backend_flags & LIBSSH2_SFTP_ATTR_UIDGID) {
    attrs->uid = 101UL;
    attrs->gid = 202UL;
  }
  if (backend_flags & LIBSSH2_SFTP_ATTR_PERMISSIONS)
    attrs->permissions = 0100640UL;
  if (backend_flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
    attrs->atime = 123456789UL;
    attrs->mtime = 987654321UL;
  }
}

static void input_attributes(LIBSSH2_SFTP_ATTRIBUTES *attrs) {
  ++backend_calls;
  if ((attrs == NULL) != expect_null)
    backend_bad = 1;
  if (attrs == NULL)
    return;
  if (attrs->flags != backend_flags ||
      attrs->filesize !=
          ((backend_flags & LIBSSH2_SFTP_ATTR_SIZE) ? size_value() : 0) ||
      attrs->uid !=
          ((backend_flags & LIBSSH2_SFTP_ATTR_UIDGID) ? 101UL : 0UL) ||
      attrs->gid !=
          ((backend_flags & LIBSSH2_SFTP_ATTR_UIDGID) ? 202UL : 0UL) ||
      attrs->permissions !=
          ((backend_flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ? 0100640UL : 0UL) ||
      attrs->atime !=
          ((backend_flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? 123456789UL : 0UL) ||
      attrs->mtime !=
          ((backend_flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? 987654321UL : 0UL))
    backend_bad = 1;
  /* Even a backend mutation must not be copied back on setter paths. */
  memset(attrs, 0x3c, sizeof(*attrs));
}

int __wrap_libssh2_sftp_readdir_ex(LIBSSH2_SFTP_HANDLE *handle, char *buffer,
                                   size_t buffer_maxlen, char *longentry,
                                   size_t longentry_maxlen,
                                   LIBSSH2_SFTP_ATTRIBUTES *attrs) {
  if (handle != (LIBSSH2_SFTP_HANDLE *)&handle_token || buffer != name ||
      buffer_maxlen != sizeof(name) || longentry != NULL ||
      longentry_maxlen != 0)
    backend_bad = 1;
  output_attributes(attrs);
  return backend_result;
}

int __wrap_libssh2_sftp_fstat_ex(LIBSSH2_SFTP_HANDLE *handle,
                                 LIBSSH2_SFTP_ATTRIBUTES *attrs, int setstat) {
  if (handle != (LIBSSH2_SFTP_HANDLE *)&handle_token)
    backend_bad = 1;
  if (setstat)
    input_attributes(attrs);
  else
    output_attributes(attrs);
  return backend_result;
}

int __wrap_libssh2_sftp_stat_ex(LIBSSH2_SFTP *sftp, const char *filename,
                                unsigned int filename_len, int stat_type,
                                LIBSSH2_SFTP_ATTRIBUTES *attrs) {
  if (sftp != (LIBSSH2_SFTP *)&sftp_token || filename != path ||
      filename_len != sizeof(path) - 1)
    backend_bad = 1;
  if (stat_type == LIBSSH2_SFTP_SETSTAT)
    input_attributes(attrs);
  else
    output_attributes(attrs);
  return backend_result;
}

LIBSSH2_SFTP_HANDLE *
__wrap_libssh2_sftp_open_ex_r(LIBSSH2_SFTP *sftp, const char *filename,
                              size_t filename_len, unsigned long flags,
                              long mode, int open_type,
                              LIBSSH2_SFTP_ATTRIBUTES *attrs) {
  if (sftp != (LIBSSH2_SFTP *)&sftp_token || filename != path ||
      filename_len != sizeof(path) - 1 || flags != LIBSSH2_FXF_CREAT ||
      mode != 0640 || open_type != LIBSSH2_SFTP_OPENFILE)
    backend_bad = 1;
  input_attributes(attrs);
  return backend_result == 0 ? (LIBSSH2_SFTP_HANDLE *)&opened_token : NULL;
}

/* fstat, stat, lstat and readdir all marshal through the real generated code.
 */
static int get_attributes(int getter, cpkt_libssh2_sftp_attributes *attrs) {
  cpkt_libssh2_sftp_handle *handle = (cpkt_libssh2_sftp_handle *)&handle_token;
  cpkt_libssh2_sftp *sftp = (cpkt_libssh2_sftp *)&sftp_token;
  if (getter == 0)
    return cpkt_libssh2_sftp_fstat_ex(handle, attrs, 0);
  if (getter == 3)
    return cpkt_libssh2_sftp_readdir_ex(handle, name, sizeof(name), NULL, 0,
                                        attrs);
  return cpkt_libssh2_sftp_stat_ex(
      sftp, path, sizeof(path) - 1,
      getter == 1 ? LIBSSH2_SFTP_STAT : LIBSSH2_SFTP_LSTAT, attrs);
}

static int set_attributes(int setter, cpkt_libssh2_sftp_attributes *attrs) {
  cpkt_libssh2_sftp_handle *handle = (cpkt_libssh2_sftp_handle *)&handle_token;
  cpkt_libssh2_sftp *sftp = (cpkt_libssh2_sftp *)&sftp_token;
  cpkt_libssh2_sftp_handle *opened;
  if (setter == 0)
    return cpkt_libssh2_sftp_fstat_ex(handle, attrs, 1);
  if (setter == 1)
    return cpkt_libssh2_sftp_stat_ex(sftp, path, sizeof(path) - 1,
                                     LIBSSH2_SFTP_SETSTAT, attrs);
  opened = cpkt_libssh2_sftp_open_ex_r(sftp, path, sizeof(path) - 1,
                                       LIBSSH2_FXF_CREAT, 0640,
                                       LIBSSH2_SFTP_OPENFILE, attrs);
  if (opened !=
      (backend_result == 0 ? (cpkt_libssh2_sftp_handle *)&opened_token : NULL))
    backend_bad = 1;
  return backend_result;
}

static void selected_public(cpkt_libssh2_sftp_attributes *attrs,
                            unsigned long flags) {
  attrs->flags = flags;
  if (flags & LIBSSH2_SFTP_ATTR_SIZE) {
    attrs->filesize.high = 0x12345678UL;
    attrs->filesize.low = 0x9abcdef0UL;
  }
  if (flags & LIBSSH2_SFTP_ATTR_UIDGID) {
    attrs->uid = 101UL;
    attrs->gid = 202UL;
  }
  if (flags & LIBSSH2_SFTP_ATTR_PERMISSIONS)
    attrs->permissions = 0100640UL;
  if (flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
    attrs->atime = 123456789UL;
    attrs->mtime = 987654321UL;
  }
}

static int same_fields(const cpkt_libssh2_sftp_attributes *a,
                       const cpkt_libssh2_sftp_attributes *b) {
  return a->flags == b->flags && a->filesize.high == b->filesize.high &&
         a->filesize.low == b->filesize.low && a->uid == b->uid &&
         a->gid == b->gid && a->permissions == b->permissions &&
         a->atime == b->atime && a->mtime == b->mtime;
}

int main(void) {
  static const unsigned long flag_bits[] = {
      LIBSSH2_SFTP_ATTR_SIZE, LIBSSH2_SFTP_ATTR_UIDGID,
      LIBSSH2_SFTP_ATTR_PERMISSIONS, LIBSSH2_SFTP_ATTR_ACMODTIME};
  static const int results[] = {0, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                LIBSSH2_ERROR_EAGAIN};
  cpkt_libssh2_sftp_attributes attrs, expected;
  unsigned int mask, extra, bit, getter, setter, result, cases = 0;

  /* All 16 subsets, both with and without uninterpreted flags. */
  for (extra = 0; extra < 2; ++extra) {
    for (mask = 0; mask < 16; ++mask) {
      backend_flags = extra ? LIBSSH2_SFTP_ATTR_EXTENDED | 0x40000000UL : 0;
      for (bit = 0; bit < 4; ++bit)
        if (mask & (1U << bit))
          backend_flags |= flag_bits[bit];
      memset(&expected, 0, sizeof(expected));
      selected_public(&expected, backend_flags);
      for (getter = 0; getter < 4; ++getter) {
        memset(&attrs, 0xa5, sizeof(attrs));
        backend_result = getter == 3 ? 4 : 0;
        CHECK(get_attributes(getter, &attrs) == backend_result);
        CHECK(!backend_bad && same_fields(&attrs, &expected));
        ++cases;
      }
      for (setter = 0; setter < 3; ++setter) {
        for (result = 0; result < sizeof(results) / sizeof(results[0]);
             ++result) {
          /* Defined poison proves absent input is discarded, not consumed. */
          memset(&attrs, 0xa5, sizeof(attrs));
          selected_public(&attrs, backend_flags);
          memcpy(&expected, &attrs, sizeof(expected));
          backend_result = results[result];
          CHECK(set_attributes(setter, &attrs) == backend_result);
          CHECK(!backend_bad && memcmp(&attrs, &expected, sizeof(attrs)) == 0);
          ++cases;
        }
      }
    }
  }
  for (getter = 0; getter < 4; ++getter) {
    for (result = getter == 3 ? 0 : 1;
         result < sizeof(results) / sizeof(results[0]); ++result) {
      memset(&attrs, 0xa5, sizeof(attrs));
      memcpy(&expected, &attrs, sizeof(expected));
      backend_result = results[result];
      CHECK(get_attributes(getter, &attrs) == backend_result);
      CHECK(!backend_bad && memcmp(&attrs, &expected, sizeof(attrs)) == 0);
      ++cases;
    }
    /* A success response containing no record still produces zero fields. */
    backend_empty = 1;
    backend_result = getter == 3 ? 4 : 0;
    memset(&attrs, 0xa5, sizeof(attrs));
    memset(&expected, 0, sizeof(expected));
    CHECK(get_attributes(getter, &attrs) == backend_result);
    CHECK(!backend_bad && same_fields(&attrs, &expected));
    backend_empty = 0;
    ++cases;
    expect_null = 1;
    for (result = 0; result < sizeof(results) / sizeof(results[0]); ++result) {
      backend_result = getter == 3 && result == 0 ? 4 : results[result];
      CHECK(get_attributes(getter, NULL) == backend_result);
      CHECK(!backend_bad);
      ++cases;
    }
    if (getter == 3) {
      backend_result = 0;
      CHECK(get_attributes(getter, NULL) == 0 && !backend_bad);
      ++cases;
    }
    expect_null = 0;
  }
  expect_null = 1;
  for (setter = 0; setter < 3; ++setter) {
    for (result = 0; result < sizeof(results) / sizeof(results[0]); ++result) {
      backend_result = results[result];
      CHECK(set_attributes(setter, NULL) == backend_result && !backend_bad);
      ++cases;
    }
  }
  CHECK(backend_calls == (int)cases);
  printf("SFTP attributes: %u wrapped backend scenarios passed\n", cases);
  return 0;
}
