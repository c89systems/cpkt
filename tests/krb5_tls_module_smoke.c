#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static int check_dependency(const char *soname, const char *symbol,
                            const char *expected_dir) {
  Dl_info info;
  void *library;
  void *address;
  size_t prefix_length;

  library = dlopen(soname, RTLD_NOW | RTLD_NOLOAD);
  if (library == NULL) {
    fprintf(stderr, "%s was not loaded: %s\n", soname, dlerror());
    return 1;
  }
  address = dlsym(library, symbol);
  if (address == NULL || dladdr(address, &info) == 0) {
    fprintf(stderr, "%s symbol %s is unavailable\n", soname, symbol);
    return 1;
  }
  prefix_length = strlen(expected_dir);
  if (strncmp(info.dli_fname, expected_dir, prefix_length) != 0 ||
      info.dli_fname[prefix_length] != '/') {
    fprintf(stderr, "%s loaded from %s, expected %s\n", soname, info.dli_fname,
            expected_dir);
    return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  void *module;
  if (argc != 3) {
    fprintf(stderr, "usage: krb5_tls_module_smoke MODULE LIBDIR\n");
    return 2;
  }
  module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (module == NULL) {
    fprintf(stderr, "Kerberos TLS module cannot load: %s\n", dlerror());
    return 1;
  }
  if (dlsym(module, "tls_k5tls_initvt") == NULL) {
    fprintf(stderr, "Kerberos TLS module lacks its entry point\n");
    return 1;
  }
  if (check_dependency("libkrb5.so.3", "krb5_init_context", argv[2]) != 0 ||
      check_dependency("libssl.so.3", "SSL_new", argv[2]) != 0 ||
      check_dependency("libcrypto.so.3", "OPENSSL_init_crypto", argv[2]) != 0)
    return 1;
  dlclose(module);
  return 0;
}
