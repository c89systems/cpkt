#!/usr/bin/env python3
"""Generate the complete strict-C89 public boundary for pinned MQTT-C.

MQTT-C exposes transparent records and callback-bearing client state.  The
facade therefore preserves the native record layouts using C89 spellings and
forwards every header-declared native function.  Its two variadic packet
builders are implemented in the facade because ISO C does not provide a way
to forward an arbitrary va_list to another variadic function.
"""

import argparse
import pathlib
import re
import sys
from typing import Iterable, List, Tuple


TYPE_REPLACEMENTS: Tuple[Tuple[str, str], ...] = (
    ("mqtt_pal_socket_handle", "cpkt_mqtt_socket_handle"),
    ("mqtt_pal_mutex_t", "cpkt_mqtt_mutex_t"),
    ("mqtt_pal_time_t", "cpkt_mqtt_time_t"),
    ("MQTTControlPacketType", "cpkt_mqtt_control_packet_type"),
    ("MQTTConnackReturnCode", "cpkt_mqtt_connack_return_code"),
    ("MQTTSubackReturnCodes", "cpkt_mqtt_suback_return_codes"),
    ("MQTTConnectFlags", "cpkt_mqtt_connect_flags"),
    ("MQTTPublishFlags", "cpkt_mqtt_publish_flags"),
    ("MQTTQueuedMessageState", "cpkt_mqtt_queued_message_state"),
    ("MQTTErrors", "cpkt_mqtt_errors"),
    ("uint32_t", "cpkt_mqtt_u32"),
    ("uint16_t", "cpkt_mqtt_u16"),
    ("uint8_t", "cpkt_mqtt_u8"),
    ("ssize_t", "cpkt_mqtt_ssize"),
    ("mqtt_", "cpkt_mqtt_"),
)


def transform(text: str) -> str:
    """Translate upstream declarations into the public CPKT spelling."""
    text = text.replace("__ALL_MQTT_ERRORS", "CPKT_MQTT_ALL_ERRORS")
    text = text.replace("GENERATE_ENUM", "CPKT_MQTT_GENERATE_ENUM")
    text = text.replace("GENERATE_STRING", "CPKT_MQTT_GENERATE_STRING")
    text = re.sub(r"(?<![A-Za-z0-9_])__mqtt_", "cpkt_mqtt_internal_", text)
    text = re.sub(r"(?<![A-Za-z0-9_])MQTT_", "CPKT_MQTT_", text)
    for native, facade in TYPE_REPLACEMENTS:
        if native.endswith("_"):
            text = re.sub(r"(?<![A-Za-z0-9_])" + re.escape(native),
                          facade, text)
        else:
            text = re.sub(r"\b" + re.escape(native) + r"\b", facade, text)
    return text


def normalize(text: str) -> str:
    return " ".join(text.split())


def strip_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)


def split_parameters(parameters: str) -> Iterable[str]:
    if normalize(parameters) == "void":
        return []
    depth = 0
    result: List[str] = []
    current: List[str] = []
    for character in parameters:
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        if character == "," and depth == 0:
            result.append("".join(current).strip())
            current = []
        else:
            current.append(character)
    tail = "".join(current).strip()
    if tail:
        result.append(tail)
    return result


def parameter_type_and_name(parameter: str) -> Tuple[str, str]:
    callback_match = re.search(
        r"\(\s*\*\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\)", parameter)
    if callback_match:
        name = callback_match.group("name")
        return parameter.replace("*" + name, "*", 1), name
    match = re.match(r"(?P<type>.+?)(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*$",
                     parameter.strip(), re.DOTALL)
    if not match:
        raise ValueError("cannot identify parameter name: " + parameter)
    return match.group("type").rstrip(), match.group("name")


def native_name(name: str) -> str:
    if name.startswith("cpkt_mqtt_internal_"):
        return "__mqtt_" + name[len("cpkt_mqtt_internal_"):]
    if not name.startswith("cpkt_mqtt_"):
        raise ValueError("unexpected public MQTT-C name: " + name)
    return "mqtt_" + name[len("cpkt_mqtt_"):]


def functions(header_text: str, expected_count: int = 42) -> List[Tuple[str, str, str]]:
    declarations: List[Tuple[str, str, str]] = []
    for candidate in strip_comments(header_text).split(";"):
        match = re.search(
            r"(?P<result>[A-Za-z_][A-Za-z0-9_\s\*]*?)\s+"
            r"(?P<name>(?:__)?mqtt_[A-Za-z0-9_]+)\s*"
            r"\((?P<parameters>.*)\)\s*$", candidate, re.DOTALL)
        if match:
            declarations.append((match.group("result"), match.group("name"),
                                 match.group("parameters")))
    if len(declarations) != expected_count:
        raise ValueError("expected {} MQTT-C public functions, found {}".format(
            expected_count, len(declarations)))
    return declarations


def generic_definition(declaration: Tuple[str, str, str]) -> str:
    raw_result_text, raw_name, raw_parameters = declaration
    if raw_name in ("mqtt_pack_subscribe_request",
                    "mqtt_pack_unsubscribe_request"):
        return ""
    raw_result = normalize(raw_result_text)
    raw_parameters = raw_parameters.strip()
    public_result = transform(raw_result_text).strip()
    public_name = transform(raw_name)
    public_parameters = transform(raw_parameters)
    arguments: List[str] = []
    for parameter in split_parameters(raw_parameters):
        if parameter == "...":
            raise ValueError("unexpected variadic MQTT-C function: " + raw_name)
        native_type, name = parameter_type_and_name(parameter)
        arguments.append("({}){}".format(normalize(native_type),
                                           transform(name)))
    call = "{}({})".format(raw_name, ", ".join(arguments))
    definition = "CPKT_MQTTC_API {} {}({}) {{\n".format(
        public_result, public_name, public_parameters)
    if raw_result == "void":
        definition += "  {};\n".format(call)
    else:
        definition += "  return ({}){};\n".format(public_result, call)
    return definition + "}\n"


def facade_header(header_text: str) -> str:
    start = header_text.find("enum MQTTControlPacketType")
    end = header_text.rfind("#if defined(__cplusplus)")
    if start < 0 or end < 0 or end <= start:
        raise ValueError("unable to locate MQTT-C public header body")
    body = strip_comments(header_text[start:end])
    body = transform(body)
    return """/* Generated by tools/generate_mqttc_c89_facade.py; do not edit. */
#ifndef CPKT_MQTTC_H
#define CPKT_MQTTC_H

#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32) && defined(CPKT_MQTTC_BUILDING_SHARED)
#define CPKT_MQTTC_API __declspec(dllexport)
#elif defined(_WIN32) && !defined(CPKT_MQTTC_STATIC)
#define CPKT_MQTTC_API __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define CPKT_MQTTC_API __attribute__((visibility("default")))
#else
#define CPKT_MQTTC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char cpkt_mqtt_u8;
typedef unsigned short cpkt_mqtt_u16;
typedef unsigned int cpkt_mqtt_u32;
typedef ptrdiff_t cpkt_mqtt_ssize;
typedef int cpkt_mqtt_socket_handle;
typedef time_t cpkt_mqtt_time_t;
typedef pthread_mutex_t cpkt_mqtt_mutex_t;

CPKT_MQTTC_API cpkt_mqtt_ssize cpkt_mqtt_pal_sendall(
    cpkt_mqtt_socket_handle socket, const void *buffer, size_t size, int flags);
CPKT_MQTTC_API cpkt_mqtt_ssize cpkt_mqtt_pal_recvall(
    cpkt_mqtt_socket_handle socket, void *buffer, size_t size, int flags);

__CPKT_MQTTC_BODY__

#ifdef __cplusplus
}
#endif

#endif /* CPKT_MQTTC_H */
""".replace("__CPKT_MQTTC_BODY__", body)


def variadic_definitions() -> str:
    return """CPKT_MQTTC_API cpkt_mqtt_ssize
cpkt_mqtt_pack_subscribe_request(cpkt_mqtt_u8 *buf, size_t bufsz,
                                 unsigned int packet_id, ...)
{
  va_list arguments;
  const cpkt_mqtt_u8 *start;
  cpkt_mqtt_ssize result;
  struct cpkt_mqtt_fixed_header fixed_header;
  unsigned int count;
  unsigned int index;
  const char *topic[CPKT_MQTT_SUBSCRIBE_REQUEST_MAX_NUM_TOPICS];
  cpkt_mqtt_u8 maximum_qos[CPKT_MQTT_SUBSCRIBE_REQUEST_MAX_NUM_TOPICS];

  start = buf;
  count = 0U;
  va_start(arguments, packet_id);
  for (;;) {
    topic[count] = va_arg(arguments, const char *);
    if (topic[count] == NULL) {
      break;
    }
    maximum_qos[count] = (cpkt_mqtt_u8)va_arg(arguments, unsigned int);
    ++count;
    if (count >= CPKT_MQTT_SUBSCRIBE_REQUEST_MAX_NUM_TOPICS) {
      va_end(arguments);
      return CPKT_MQTT_ERROR_SUBSCRIBE_TOO_MANY_TOPICS;
    }
  }
  va_end(arguments);

  fixed_header.control_type = CPKT_MQTT_CONTROL_SUBSCRIBE;
  fixed_header.control_flags = 2U;
  fixed_header.remaining_length = 2U;
  for (index = 0U; index < count; ++index) {
    fixed_header.remaining_length +=
        cpkt_mqtt_internal_packed_cstrlen(topic[index]) + 1U;
  }
  result = cpkt_mqtt_pack_fixed_header(buf, bufsz, &fixed_header);
  if (result <= 0) {
    return result;
  }
  buf += result;
  bufsz -= (size_t)result;
  if (bufsz < fixed_header.remaining_length) {
    return 0;
  }
  buf += cpkt_mqtt_internal_pack_uint16(buf, (cpkt_mqtt_u16)packet_id);
  for (index = 0U; index < count; ++index) {
    buf += cpkt_mqtt_internal_pack_str(buf, topic[index]);
    *buf++ = maximum_qos[index];
  }
  return (cpkt_mqtt_ssize)(buf - start);
}

CPKT_MQTTC_API cpkt_mqtt_ssize
cpkt_mqtt_pack_unsubscribe_request(cpkt_mqtt_u8 *buf, size_t bufsz,
                                   unsigned int packet_id, ...)
{
  va_list arguments;
  const cpkt_mqtt_u8 *start;
  cpkt_mqtt_ssize result;
  struct cpkt_mqtt_fixed_header fixed_header;
  unsigned int count;
  unsigned int index;
  const char *topic[CPKT_MQTT_UNSUBSCRIBE_REQUEST_MAX_NUM_TOPICS];

  start = buf;
  count = 0U;
  va_start(arguments, packet_id);
  for (;;) {
    topic[count] = va_arg(arguments, const char *);
    if (topic[count] == NULL) {
      break;
    }
    ++count;
    if (count >= CPKT_MQTT_UNSUBSCRIBE_REQUEST_MAX_NUM_TOPICS) {
      va_end(arguments);
      return CPKT_MQTT_ERROR_UNSUBSCRIBE_TOO_MANY_TOPICS;
    }
  }
  va_end(arguments);

  fixed_header.control_type = CPKT_MQTT_CONTROL_UNSUBSCRIBE;
  fixed_header.control_flags = 2U;
  fixed_header.remaining_length = 2U;
  for (index = 0U; index < count; ++index) {
    fixed_header.remaining_length += cpkt_mqtt_internal_packed_cstrlen(topic[index]);
  }
  result = cpkt_mqtt_pack_fixed_header(buf, bufsz, &fixed_header);
  if (result <= 0) {
    return result;
  }
  buf += result;
  bufsz -= (size_t)result;
  if (bufsz < fixed_header.remaining_length) {
    return 0;
  }
  buf += cpkt_mqtt_internal_pack_uint16(buf, (cpkt_mqtt_u16)packet_id);
  for (index = 0U; index < count; ++index) {
    buf += cpkt_mqtt_internal_pack_str(buf, topic[index]);
  }
  return (cpkt_mqtt_ssize)(buf - start);
}
"""


def facade_source(header_text: str) -> str:
    definitions = "\n".join(
        definition for definition in
        (generic_definition(declaration) for declaration in functions(header_text))
        if definition)
    return """/* Generated by tools/generate_mqttc_c89_facade.py; do not edit. */
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>

#include <mqtt.h>

#include <cpkt/mqttc.h>

typedef char cpkt_mqtt_u8_is_eight_bits[
    (sizeof(cpkt_mqtt_u8) * CHAR_BIT == 8) ? 1 : -1];
typedef char cpkt_mqtt_u16_is_sixteen_bits[
    (sizeof(cpkt_mqtt_u16) * CHAR_BIT == 16) ? 1 : -1];
typedef char cpkt_mqtt_u32_is_thirty_two_bits[
    (sizeof(cpkt_mqtt_u32) * CHAR_BIT == 32) ? 1 : -1];
typedef char cpkt_mqtt_ssize_matches_native[
    (sizeof(cpkt_mqtt_ssize) == sizeof(ssize_t)) ? 1 : -1];
typedef char cpkt_mqtt_client_layout_matches_native[
    (sizeof(struct cpkt_mqtt_client) == sizeof(struct mqtt_client)) ? 1 : -1];
typedef char cpkt_mqtt_response_layout_matches_native[
    (sizeof(struct cpkt_mqtt_response) == sizeof(struct mqtt_response)) ? 1 : -1];
typedef char cpkt_mqtt_queue_layout_matches_native[
    (sizeof(struct cpkt_mqtt_message_queue) ==
     sizeof(struct mqtt_message_queue)) ? 1 : -1];

CPKT_MQTTC_API cpkt_mqtt_ssize
cpkt_mqtt_pal_sendall(cpkt_mqtt_socket_handle socket, const void *buffer,
                      size_t size, int flags)
{
  return (cpkt_mqtt_ssize)mqtt_pal_sendall((mqtt_pal_socket_handle)socket,
                                           buffer, size, flags);
}

CPKT_MQTTC_API cpkt_mqtt_ssize
cpkt_mqtt_pal_recvall(cpkt_mqtt_socket_handle socket, void *buffer,
                      size_t size, int flags)
{
  return (cpkt_mqtt_ssize)mqtt_pal_recvall((mqtt_pal_socket_handle)socket,
                                           buffer, size, flags);
}

__CPKT_MQTTC_DEFINITIONS__

__CPKT_MQTTC_VARIADIC__
""".replace("__CPKT_MQTTC_DEFINITIONS__", definitions).replace(
        "__CPKT_MQTTC_VARIADIC__", variadic_definitions())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--include-dir", required=True, type=pathlib.Path)
    parser.add_argument("--header", required=True, type=pathlib.Path)
    parser.add_argument("--source", required=True, type=pathlib.Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    native_header = args.include_dir / "mqtt.h"
    if not native_header.is_file():
        raise ValueError("required input is missing: " + str(native_header))
    header_text = native_header.read_text(encoding="utf-8")
    functions(header_text)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.source.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(facade_header(header_text), encoding="utf-8")
    args.source.write_text(facade_source(header_text), encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print("generate_mqttc_c89_facade.py: " + str(error), file=sys.stderr)
        raise SystemExit(1)
