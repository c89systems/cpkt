#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cpkt_fuzz_one_input(const unsigned char *data, size_t size);

int main(int argc, char **argv) {
  FILE *input;
  unsigned char *data;
  long length;
  size_t read_count;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <input-file>\n", argv[0]);
    return 2;
  }
  input = fopen(argv[1], "rb");
  if (input == NULL) {
    fprintf(stderr, "failed to open %s: %s\n", argv[1], strerror(errno));
    return 2;
  }
  if (fseek(input, 0, SEEK_END) != 0 || (length = ftell(input)) < 0 ||
      fseek(input, 0, SEEK_SET) != 0) {
    fclose(input);
    return 2;
  }
  if (length > 1024 * 1024) {
    fclose(input);
    return 0;
  }
  data = malloc(length == 0 ? 1U : (size_t)length);
  if (data == NULL) {
    fclose(input);
    return 2;
  }
  read_count = fread(data, 1, (size_t)length, input);
  fclose(input);
  if (read_count != (size_t)length) {
    free(data);
    return 2;
  }
  (void)cpkt_fuzz_one_input(data, (size_t)length);
  free(data);
  return 0;
}
