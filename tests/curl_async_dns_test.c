#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

struct cpkt_socket {
  curl_socket_t fd;
  int interest;
};

struct cpkt_multi_state {
  struct cpkt_socket sockets[64];
  size_t socket_count;
};

struct cpkt_body {
  size_t bytes;
};

static int cpkt_socket_callback(CURL *easy, curl_socket_t fd, int interest,
                                void *context, void *socket_context) {
  struct cpkt_multi_state *state = (struct cpkt_multi_state *)context;
  size_t index;
  (void)easy;
  (void)socket_context;
  for (index = 0; index < state->socket_count; ++index) {
    if (state->sockets[index].fd == fd)
      break;
  }
  if (interest == CURL_POLL_REMOVE) {
    if (index < state->socket_count) {
      state->sockets[index] = state->sockets[--state->socket_count];
    }
  } else if (index < state->socket_count) {
    state->sockets[index].interest = interest;
  } else if (state->socket_count < 64) {
    state->sockets[state->socket_count].fd = fd;
    state->sockets[state->socket_count].interest = interest;
    ++state->socket_count;
  } else {
    return -1;
  }
  return 0;
}

static size_t cpkt_write_callback(char *data, size_t size, size_t count,
                                  void *context) {
  struct cpkt_body *body = (struct cpkt_body *)context;
  (void)data;
  body->bytes += size * count;
  return size * count;
}

static int cpkt_drive(CURLM *multi, struct cpkt_multi_state *state,
                      int *running) {
  fd_set read_set;
  fd_set write_set;
  struct timeval timeout;
  struct cpkt_socket ready[64];
  size_t ready_count = 0;
  size_t index;
  int maximum = -1;
  int selected;
  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  for (index = 0; index < state->socket_count; ++index) {
    int fd = (int)state->sockets[index].fd;
    if (fd >= FD_SETSIZE)
      return 0;
    if (state->sockets[index].interest & CURL_POLL_IN)
      FD_SET(fd, &read_set);
    if (state->sockets[index].interest & CURL_POLL_OUT)
      FD_SET(fd, &write_set);
    if (fd > maximum)
      maximum = fd;
  }
  timeout.tv_sec = 0;
  timeout.tv_usec = 100000;
  selected = select(maximum + 1, &read_set, &write_set, NULL, &timeout);
  if (selected < 0)
    return 0;
  for (index = 0; index < state->socket_count; ++index) {
    int fd = (int)state->sockets[index].fd;
    if (FD_ISSET(fd, &read_set) || FD_ISSET(fd, &write_set))
      ready[ready_count++] = state->sockets[index];
  }
  for (index = 0; index < ready_count; ++index) {
    int fd = (int)ready[index].fd;
    int event = 0;
    if (FD_ISSET(fd, &read_set))
      event |= CURL_CSELECT_IN;
    if (FD_ISSET(fd, &write_set))
      event |= CURL_CSELECT_OUT;
    if (curl_multi_socket_action(multi, ready[index].fd, event, running) !=
        CURLM_OK)
      return 0;
  }
  return curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, running) ==
         CURLM_OK;
}

static int cpkt_multi_test(const char *port) {
  CURLM *multi = NULL;
  CURL *slow = NULL;
  CURL *fast = NULL;
  struct cpkt_multi_state state;
  struct cpkt_body slow_body;
  struct cpkt_body fast_body;
  char slow_url[128];
  char fast_url[128];
  time_t deadline;
  int running = 0;
  int fast_added = 0;
  int fast_before_slow = 0;
  int slow_done = 0;
  int fast_done = 0;
  int success = 0;
  long slow_status = 0;
  long fast_status = 0;
  CURLMsg *message;
  int messages;
  memset(&state, 0, sizeof(state));
  memset(&slow_body, 0, sizeof(slow_body));
  memset(&fast_body, 0, sizeof(fast_body));
  fprintf(stderr, "multi socket: initializing hostname transfers\n");
  if (snprintf(slow_url, sizeof(slow_url), "http://localhost:%s/slow", port) <
          0 ||
      snprintf(fast_url, sizeof(fast_url), "http://localhost:%s/fast", port) <
          0)
    return 0;
  multi = curl_multi_init();
  slow = curl_easy_init();
  fast = curl_easy_init();
  if (multi == NULL || slow == NULL || fast == NULL)
    goto done;
  curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, cpkt_socket_callback);
  curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, &state);
  curl_easy_setopt(slow, CURLOPT_URL, slow_url);
  curl_easy_setopt(slow, CURLOPT_PROXY, "");
  curl_easy_setopt(slow, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(slow, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
  curl_easy_setopt(slow, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
  curl_easy_setopt(slow, CURLOPT_TIMEOUT_MS, 10000L);
  curl_easy_setopt(slow, CURLOPT_WRITEFUNCTION, cpkt_write_callback);
  curl_easy_setopt(slow, CURLOPT_WRITEDATA, &slow_body);
  curl_easy_setopt(fast, CURLOPT_URL, fast_url);
  curl_easy_setopt(fast, CURLOPT_PROXY, "");
  curl_easy_setopt(fast, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(fast, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
  curl_easy_setopt(fast, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
  curl_easy_setopt(fast, CURLOPT_TIMEOUT_MS, 10000L);
  curl_easy_setopt(fast, CURLOPT_WRITEFUNCTION, cpkt_write_callback);
  curl_easy_setopt(fast, CURLOPT_WRITEDATA, &fast_body);
  if (curl_multi_add_handle(multi, slow) != CURLM_OK ||
      curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, &running) !=
          CURLM_OK)
    goto done;
  fprintf(stderr, "multi socket: slow hostname transfer started\n");
  deadline = time(NULL) + 12;
  while (time(NULL) < deadline && (!slow_done || !fast_done)) {
    if (slow_body.bytes > 0 && !fast_added) {
      if (curl_multi_add_handle(multi, fast) != CURLM_OK ||
          curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, &running) !=
              CURLM_OK)
        goto done;
      fast_added = 1;
      fprintf(stderr, "multi socket: slow transfer progressed; fast started\n");
    }
    if (!cpkt_drive(multi, &state, &running))
      goto done;
    while ((message = curl_multi_info_read(multi, &messages)) != NULL) {
      if (message->msg != CURLMSG_DONE || message->data.result != CURLE_OK)
        goto done;
      if (message->easy_handle == fast) {
        fast_done = 1;
        fast_before_slow = !slow_done;
        fprintf(stderr, "multi socket: fast transfer completed\n");
      } else if (message->easy_handle == slow) {
        slow_done = 1;
        fprintf(stderr, "multi socket: slow transfer completed\n");
      }
    }
  }
  curl_easy_getinfo(slow, CURLINFO_RESPONSE_CODE, &slow_status);
  curl_easy_getinfo(fast, CURLINFO_RESPONSE_CODE, &fast_status);
  success = slow_done && fast_done && fast_before_slow && slow_status == 200 &&
            fast_status == 200 && slow_body.bytes > fast_body.bytes &&
            fast_body.bytes > 0;
done:
  if (multi != NULL) {
    if (slow != NULL)
      curl_multi_remove_handle(multi, slow);
    if (fast != NULL && fast_added)
      curl_multi_remove_handle(multi, fast);
  }
  if (slow != NULL)
    curl_easy_cleanup(slow);
  if (fast != NULL)
    curl_easy_cleanup(fast);
  if (multi != NULL)
    curl_multi_cleanup(multi);
  return success;
}

int main(int argc, char **argv) {
  const curl_version_info_data *version = curl_version_info(CURLVERSION_NOW);
  if (version == NULL || !(version->features & CURL_VERSION_ASYNCHDNS)) {
    fprintf(stderr, "bundled libcurl lacks CURL_VERSION_ASYNCHDNS\n");
    return 1;
  }
  if (argc == 1)
    return 0;
  if (argc != 3 || strcmp(argv[1], "--multi") != 0)
    return 2;
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    return 3;
  if (!cpkt_multi_test(argv[2])) {
    fprintf(stderr, "multi socket hostname concurrency failed\n");
    curl_global_cleanup();
    return 4;
  }
  curl_global_cleanup();
  return 0;
}
