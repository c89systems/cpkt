#define _POSIX_C_SOURCE 200809L
#include <cpkt/zlibng.h>
#include <zlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                 \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static unsigned int live_allocations;
static int fail_allocation;

static FILE *temporary_file(void) {
  char path[] = "zlibng-facade-XXXXXX";
  int descriptor = mkstemp(path);
  FILE *file;
  if (descriptor < 0)
    return NULL;
  unlink(path);
  file = fdopen(descriptor, "w+b");
  if (file == NULL)
    close(descriptor);
  return file;
}

static void *allocate(void *opaque, unsigned int items, unsigned int size) {
  void *memory;
  if (opaque != &live_allocations || fail_allocation)
    return NULL;
  memory = calloc(items, size);
  if (memory != NULL)
    ++live_allocations;
  return memory;
}

static void release(void *opaque, void *memory) {
  if (opaque != &live_allocations)
    abort();
  if (memory != NULL) {
    --live_allocations;
    free(memory);
  }
}

static void stream_init(cpkt_zlibng_stream *stream) {
  memset(stream, 0, sizeof(*stream));
  stream->zalloc = allocate;
  stream->zfree = release;
  stream->opaque = &live_allocations;
}

static int one_shot(void) {
  static const unsigned char input[] = "C89 zlib and zlib-ng interoperability";
  unsigned char packed[128];
  unsigned char output[128];
  size_t packed_size;
  size_t output_size;
  size_t consumed;
  uLongf native_size;
  cpkt_zlibng_off64 length;
  unsigned int left;
  unsigned int right;

  CHECK(strcmp(cpkt_zlibng_version(), CPKT_ZLIBNG_VERSION) == 0);
  CHECK(cpkt_zlibng_zError(CPKT_ZLIBNG_MEM_ERROR) != NULL);
  CHECK(cpkt_zlibng_compressBound(sizeof(input)) <= sizeof(packed));
  packed_size = sizeof(packed);
  CHECK(cpkt_zlibng_compress2(packed, &packed_size, input, sizeof(input), 6) ==
        0);
  native_size = sizeof(output);
  CHECK(uncompress(output, &native_size, packed, (uLong)packed_size) == Z_OK);
  CHECK(native_size == sizeof(input) &&
        memcmp(output, input, sizeof(input)) == 0);
  native_size = sizeof(packed);
  CHECK(compress(packed, &native_size, input, sizeof(input)) == Z_OK);
  output_size = sizeof(output);
  consumed = (size_t)native_size;
  CHECK(cpkt_zlibng_uncompress2(output, &output_size, packed, &consumed) == 0);
  CHECK(consumed == (size_t)native_size && output_size == sizeof(input));
  CHECK(memcmp(output, input, sizeof(input)) == 0);
  CHECK(cpkt_zlibng_crc32_z(0, (const unsigned char *)"123456789", 9) ==
        0xcbf43926U);
  CHECK(cpkt_zlibng_adler32_z(1, input, sizeof(input)) ==
        (unsigned int)adler32(1, input, sizeof(input)));
  left = cpkt_zlibng_crc32(0, input, 5);
  right = cpkt_zlibng_crc32(0, input + 5, (unsigned int)sizeof(input) - 5);
  length.high = 0;
  length.low = (unsigned int)sizeof(input) - 5;
  CHECK(cpkt_zlibng_crc32_combine(left, right, length) ==
        cpkt_zlibng_crc32_z(0, input, sizeof(input)));
  CHECK(cpkt_zlibng_crc32_combine_op(left, right,
                                     cpkt_zlibng_crc32_combine_gen(length)) ==
        cpkt_zlibng_crc32_z(0, input, sizeof(input)));
  CHECK(cpkt_zlibng_uncompress(output, &output_size,
                               (const unsigned char *)"invalid",
                               7) == CPKT_ZLIBNG_DATA_ERROR);
  return 0;
}

static int streaming(void) {
  static const unsigned char input[] = "small incremental streaming buffers";
  cpkt_zlibng_stream encoder;
  cpkt_zlibng_stream decoder;
  cpkt_zlibng_stream copy;
  cpkt_zlibng_gz_header header;
  cpkt_zlibng_gz_header observed;
  cpkt_zlibng_deflate_param_value parameter;
  unsigned char packed[256];
  unsigned char output[256];
  unsigned char dictionary[128];
  unsigned int dictionary_size;
  unsigned int pending;
  int bits;
  int level;
  int status;
  size_t packed_size;

  stream_init(&encoder);
  fail_allocation = 1;
  CHECK(cpkt_zlibng_deflateInit(&encoder, 6) == CPKT_ZLIBNG_MEM_ERROR);
  CHECK(live_allocations == 0);
  fail_allocation = 0;
  stream_init(&encoder);
  CHECK(cpkt_zlibng_deflateInit2(&encoder, 6, CPKT_ZLIBNG_DEFLATED, 31, 8,
                                 CPKT_ZLIBNG_DEFAULT_STRATEGY) == 0);
  memset(&header, 0, sizeof(header));
  header.os = 3;
  header.name = (unsigned char *)"test";
  CHECK(cpkt_zlibng_deflateSetHeader(&encoder, &header) == 0);
  level = 4;
  parameter.param = CPKT_ZLIBNG_DEFLATE_LEVEL;
  parameter.buf = &level;
  parameter.size = sizeof(level);
  parameter.status = -1;
  CHECK(cpkt_zlibng_deflateSetParams(&encoder, &parameter, 1) == 0);
  level = -1;
  CHECK(cpkt_zlibng_deflateGetParams(&encoder, &parameter, 1) == 0);
  CHECK(level == 4 && parameter.status == 0);
  CHECK(cpkt_zlibng_deflateCopy(&copy, &encoder) == 0);
  CHECK(cpkt_zlibng_deflateEnd(&copy) == 0);
  CHECK(cpkt_zlibng_deflateBound(&encoder, sizeof(input)) <= sizeof(packed));
  encoder.next_in = input;
  encoder.avail_in = sizeof(input);
  packed_size = 0;
  do {
    encoder.next_out = packed + packed_size;
    encoder.avail_out = 7;
    status = cpkt_zlibng_deflate(&encoder, CPKT_ZLIBNG_FINISH);
    packed_size += 7 - encoder.avail_out;
    CHECK(status == 0 || status == CPKT_ZLIBNG_STREAM_END);
    CHECK(packed_size + 7 < sizeof(packed));
  } while (status != CPKT_ZLIBNG_STREAM_END);
  CHECK(encoder.total_in == sizeof(input) && encoder.total_out == packed_size);
  CHECK(cpkt_zlibng_deflatePending(&encoder, &pending, &bits) == 0);
  CHECK(pending == 0 && bits == 0);
  CHECK(cpkt_zlibng_deflateGetDictionary(&encoder, dictionary,
                                         &dictionary_size) == 0);
  CHECK(dictionary_size == sizeof(input));
  CHECK(cpkt_zlibng_deflateReset(&encoder) == 0);
  CHECK(cpkt_zlibng_deflateEnd(&encoder) == 0);

  stream_init(&decoder);
  CHECK(cpkt_zlibng_inflateInit2(&decoder, 31) == 0);
  memset(&observed, 0, sizeof(observed));
  CHECK(cpkt_zlibng_inflateGetHeader(&decoder, &observed) == 0);
  decoder.next_in = packed;
  decoder.avail_in = (unsigned int)packed_size;
  decoder.next_out = output;
  decoder.avail_out = sizeof(output);
  CHECK(cpkt_zlibng_inflate(&decoder, CPKT_ZLIBNG_FINISH) ==
        CPKT_ZLIBNG_STREAM_END);
  CHECK(observed.done == 1 && observed.os == 3);
  CHECK(decoder.total_out == sizeof(input) &&
        memcmp(input, output, sizeof(input)) == 0);
  CHECK(cpkt_zlibng_inflateCopy(&copy, &decoder) == 0);
  CHECK(cpkt_zlibng_inflateEnd(&copy) == 0);
  CHECK(cpkt_zlibng_inflateGetDictionary(&decoder, dictionary,
                                         &dictionary_size) == 0);
  CHECK(cpkt_zlibng_inflateReset2(&decoder, 15) == 0);
  CHECK(cpkt_zlibng_inflateEnd(&decoder) == 0);
  CHECK(live_allocations == 0);
  return 0;
}

static int dictionary_stream(void) {
  static const unsigned char dictionary[] = "dictionary prefix";
  static const unsigned char input[] = "dictionary prefix and payload";
  cpkt_zlibng_stream encoder;
  cpkt_zlibng_stream decoder;
  unsigned char packed[128];
  unsigned char output[128];
  unsigned int packed_size;

  stream_init(&encoder);
  CHECK(cpkt_zlibng_deflateInit(&encoder, 6) == 0);
  CHECK(cpkt_zlibng_deflateSetDictionary(&encoder, dictionary,
                                         sizeof(dictionary)) == 0);
  encoder.next_in = input;
  encoder.avail_in = sizeof(input);
  encoder.next_out = packed;
  encoder.avail_out = sizeof(packed);
  CHECK(cpkt_zlibng_deflate(&encoder, CPKT_ZLIBNG_FINISH) ==
        CPKT_ZLIBNG_STREAM_END);
  packed_size = (unsigned int)encoder.total_out;
  CHECK(cpkt_zlibng_deflateEnd(&encoder) == 0);
  stream_init(&decoder);
  CHECK(cpkt_zlibng_inflateInit(&decoder) == 0);
  decoder.next_in = packed;
  decoder.avail_in = packed_size;
  decoder.next_out = output;
  decoder.avail_out = sizeof(output);
  CHECK(cpkt_zlibng_inflate(&decoder, CPKT_ZLIBNG_NO_FLUSH) ==
        CPKT_ZLIBNG_NEED_DICT);
  CHECK(cpkt_zlibng_inflateSetDictionary(&decoder, dictionary,
                                         sizeof(dictionary)) == 0);
  CHECK(cpkt_zlibng_inflate(&decoder, CPKT_ZLIBNG_FINISH) ==
        CPKT_ZLIBNG_STREAM_END);
  CHECK(decoder.total_out == sizeof(input) &&
        memcmp(input, output, sizeof(input)) == 0);
  CHECK(cpkt_zlibng_inflateEnd(&decoder) == 0);
  CHECK(live_allocations == 0);
  return 0;
}

struct back_input {
  const unsigned char *data;
  unsigned int size;
};
struct back_output {
  unsigned char data[128];
  unsigned int size;
};
static unsigned int back_input(void *opaque, const unsigned char **buffer) {
  struct back_input *input = opaque;
  unsigned int size = input->size;
  *buffer = input->data;
  input->size = 0;
  return size;
}
static int back_output(void *opaque, unsigned char *buffer, unsigned int size) {
  struct back_output *output = opaque;
  if (size > sizeof(output->data) - output->size)
    return 1;
  memcpy(output->data + output->size, buffer, size);
  output->size += size;
  return 0;
}
static int inflate_back(void) {
  static const unsigned char input[] = "callback streaming";
  cpkt_zlibng_stream stream;
  unsigned char packed[128];
  unsigned char *window;
  struct back_input source;
  struct back_output destination;

  stream_init(&stream);
  CHECK(cpkt_zlibng_deflateInit2(&stream, 6, CPKT_ZLIBNG_DEFLATED, -15, 8, 0) ==
        0);
  stream.next_in = input;
  stream.avail_in = sizeof(input);
  stream.next_out = packed;
  stream.avail_out = sizeof(packed);
  CHECK(cpkt_zlibng_deflate(&stream, CPKT_ZLIBNG_FINISH) ==
        CPKT_ZLIBNG_STREAM_END);
  source.data = packed;
  source.size = (unsigned int)stream.total_out;
  CHECK(cpkt_zlibng_deflateEnd(&stream) == 0);
  window = malloc(32768);
  CHECK(window != NULL);
  stream_init(&stream);
  CHECK(cpkt_zlibng_inflateBackInit(&stream, 15, window) == 0);
  memset(&destination, 0, sizeof(destination));
  CHECK(cpkt_zlibng_inflateBack(&stream, back_input, &source, back_output,
                                &destination) == CPKT_ZLIBNG_STREAM_END);
  CHECK(destination.size == sizeof(input) &&
        memcmp(destination.data, input, sizeof(input)) == 0);
  CHECK(cpkt_zlibng_inflateBackEnd(&stream) == 0);
  free(window);
  CHECK(live_allocations == 0);
  return 0;
}

static int gzip_io(void) {
  FILE *temporary;
  cpkt_zlibng_gzfile gzip;
  cpkt_zlibng_off64 offset;
  cpkt_zlibng_off64 position;
  char output[32];
  int error;

  temporary = temporary_file();
  CHECK(temporary != NULL);
  gzip = cpkt_zlibng_gzdopen(dup(fileno(temporary)), "wb");
  CHECK(gzip != NULL && cpkt_zlibng_gzbuffer(gzip, 1024) == 0);
  CHECK(cpkt_zlibng_gzprintf(gzip, "%s:%d", "value", 7) == 7);
  CHECK(cpkt_zlibng_gzputs(gzip, "\n") == 1);
  position = cpkt_zlibng_gztell(gzip);
  CHECK(position.high == 0 && position.low == 8);
  CHECK(cpkt_zlibng_gzclose_w(gzip) == 0);
  rewind(temporary);
  gzip = cpkt_zlibng_gzdopen(dup(fileno(temporary)), "rb");
  CHECK(gzip != NULL);
  CHECK(cpkt_zlibng_gzgetc(gzip) == 'v');
  CHECK(cpkt_zlibng_gzungetc('v', gzip) == 'v');
  CHECK(cpkt_zlibng_gzgets(gzip, output, sizeof(output)) == output);
  CHECK(strcmp(output, "value:7\n") == 0);
  CHECK(cpkt_zlibng_gzdirect(gzip) == 0);
  offset.high = 0;
  offset.low = 0;
  position = cpkt_zlibng_gzseek(gzip, offset, SEEK_SET);
  CHECK(position.high == 0 && position.low == 0);
  CHECK(cpkt_zlibng_gzfread(output, 2, 4, gzip) == 4);
  CHECK(cpkt_zlibng_gzread(gzip, output, sizeof(output)) == 0);
  CHECK(cpkt_zlibng_gzeof(gzip));
  CHECK(cpkt_zlibng_gzerror(gzip, &error) != NULL && error == 0);
  cpkt_zlibng_gzclearerr(gzip);
  CHECK(cpkt_zlibng_gzrewind(gzip) == 0);
  offset.high = ~0U;
  offset.low = ~0U;
  position = cpkt_zlibng_gzseek(gzip, offset, SEEK_SET);
  CHECK(position.high == ~0U && position.low == ~0U);
  CHECK(cpkt_zlibng_gzclose_r(gzip) == 0);
  CHECK(fclose(temporary) == 0);
  return 0;
}

int main(void) {
  return one_shot() || streaming() || dictionary_stream() || inflate_back() ||
         gzip_io();
}
