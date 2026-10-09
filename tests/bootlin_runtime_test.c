#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Observe loaded objects, not just ELF metadata. Also exercise a real exec. */
int main(int argc, char **argv) {
  FILE *maps;
  char line[4096];
  int libc_seen = 0;
  int loader_seen = 0;
  int status;
  pid_t child;
  if (argc < 2)
    return 2;
  maps = fopen("/proc/self/maps", "r");
  if (!maps)
    return 3;
  while (fgets(line, sizeof(line), maps)) {
    if (strstr(line, "libc.so") || strstr(line, "ld-musl-")) {
      if (!strstr(line, argv[1]))
        return 4;
      libc_seen = 1;
#ifndef __GLIBC__
      /* musl combines its loader and libc in this mapped object. */
      loader_seen = 1;
#endif
    }
    if (strstr(line, "ld-linux-") || strstr(line, "ld-musl-")) {
      if (!strstr(line, argv[1]))
        return 5;
      loader_seen = 1;
    }
  }
  fclose(maps);
  if (!libc_seen || !loader_seen) {
    fprintf(stderr, "selected runtime mappings missing: libc=%d loader=%d\n",
            libc_seen, loader_seen);
    return 6;
  }
  if (argc > 2)
    return strcmp(argv[2], "child argument with spaces") ? 7 : 0;
  child = fork();
  if (child < 0)
    return 8;
  if (!child) {
    execl("/proc/self/exe", argv[0], argv[1], "child argument with spaces",
          (char *)0);
    _exit(9);
  }
  if (waitpid(child, &status, 0) != child || status != 0)
    return 10;
  child = fork();
  if (child < 0)
    return 11;
  if (!child) {
    execl("/bin/sh", "sh", "-c",
          "while IFS= read -r line; do case \"$line\" in *\"$1\"*) exit 12;; "
          "esac; done < /proc/self/maps; exit 0",
          "host-shell", argv[1], (char *)0);
    _exit(13);
  }
  if (waitpid(child, &status, 0) != child || status != 0)
    return 14;
  puts("Bootlin parent and exec child; independent host shell runtime: passed");
  return 0;
}
