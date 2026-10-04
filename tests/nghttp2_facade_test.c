#include <cpkt/nghttp2.h>

#include <stddef.h>

static int nghttp2_test_send_calls;

static cpkt_nghttp2_ssize nghttp2_test_send(cpkt_nghttp2_session *session,
                                            const cpkt_nghttp2_u8 *data,
                                            size_t length, int flags,
                                            void *context) {
  (void)session;
  (void)data;
  (void)length;
  (void)flags;
  (void)context;
  ++nghttp2_test_send_calls;
  return CPKT_NGHTTP2_ERR_WOULDBLOCK;
}

int main(void) {
  cpkt_nghttp2_session_callbacks *callbacks;
  cpkt_nghttp2_session *session;
  cpkt_nghttp2_option *option;
  cpkt_nghttp2_settings_entry setting;
  cpkt_nghttp2_u64 rate;
  const cpkt_nghttp2_info *info;
  int status;

  callbacks = NULL;
  session = NULL;
  option = NULL;
  status = cpkt_nghttp2_session_callbacks_new(&callbacks);
  if (status != 0 || callbacks == NULL)
    return 1;
  cpkt_nghttp2_session_callbacks_set_send_callback2(callbacks,
                                                    nghttp2_test_send);
  status = cpkt_nghttp2_option_new(&option);
  if (status != 0 || option == NULL)
    return 2;
  rate.high = 1UL;
  rate.low = 0UL;
  cpkt_nghttp2_option_set_glitch_rate_limit(option, rate, rate);
  status =
      cpkt_nghttp2_session_client_new3(&session, callbacks, NULL, option, NULL);
  if (status != 0 || session == NULL)
    return 3;
  setting.settings_id = CPKT_NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS;
  setting.value = 1U;
  status = cpkt_nghttp2_submit_settings(session, CPKT_NGHTTP2_FLAG_NONE,
                                        &setting, 1);
  if (status != 0 || cpkt_nghttp2_session_want_write(session) == 0)
    return 4;
  /* nghttp2 retains the queued frame and reports a successful send pass when
   * the transport callback says would-block. */
  if (cpkt_nghttp2_session_send(session) != 0 || nghttp2_test_send_calls != 1)
    return 5;
  info = cpkt_nghttp2_version(0);
  if (info == NULL || info->version_num != CPKT_NGHTTP2_VERSION_NUM)
    return 6;
  cpkt_nghttp2_session_del(session);
  cpkt_nghttp2_option_del(option);
  cpkt_nghttp2_session_callbacks_del(callbacks);
  return 0;
}
