#!/usr/bin/env python3
"""Check every enabled native SASL getprop/setprop case has a typed binding."""

import pathlib
import re
import sys

sys.dont_write_bytecode = True
import auth_facade_signatures as facade

ROOT = pathlib.Path(__file__).resolve().parent.parent

TEXT_GET = ("USERNAME", "DEFUSERREALM", "IPLOCALPORT", "IPREMOTEPORT",
            "PLUGERR", "SERVICE", "SERVERFQDN", "AUTHSOURCE", "MECHNAME",
            "AUTHUSER", "APPNAME", "AUTH_EXTERNAL")
NUMBER_GET = ("SSF", "MAXOUTBUF", "SSF_EXTERNAL")
TEXT_SET = ("DEFUSERREALM", "IPLOCALPORT", "IPREMOTEPORT", "APPNAME",
            "AUTH_EXTERNAL")
GET = {"SASL_" + name: ("text", "get_text_property", "get_text_property")
       for name in TEXT_GET}
GET.update({"SASL_" + name: ("number", "get_number_property",
                             "get_number_property") for name in NUMBER_GET})
GET.update({
    "SASL_GETOPTCTX": ("callback context", "get_option_context",
                           "connection.get_option_context"),
    "SASL_CALLBACK": ("callbacks", "get_callback_record",
                      "connection.get_callback_record"),
    "SASL_DELEGATEDCREDS": ("opaque mechanism payload",
                            "get_delegated_payload",
                            "connection.get_delegated_payload"),
    "SASL_GSS_CREDS": ("GSS handle", "get_gss_credentials",
                       "connection.get_gss_credentials"),
    "SASL_GSS_LOCAL_NAME": ("GSS name", "get_gss_local_name",
                            "connection.get_gss_local_name"),
    "SASL_GSS_PEER_NAME": ("GSS name", "get_gss_peer_name",
                           "connection.get_gss_peer_name"),
    "SASL_HTTP_REQUEST": ("HTTP record", "get_http_request",
                          "connection.get_http_request"),
    "SASL_SEC_PROPS": ("security record", "get_security_properties",
                       "connection.get_security_properties"),
})
SET = {"SASL_" + name: ("copied text", "set_text_property",
                             "set_text_property") for name in TEXT_SET}
SET.update({
    "SASL_SSF_EXTERNAL": ("number", "set_external_ssf", "set_external_ssf"),
    "SASL_SEC_PROPS": ("security record", "set_security_properties",
                       "connection.set_security_properties"),
    "SASL_GSS_CREDS": ("borrowed GSS handle", "set_gss_credentials",
                       "connection.set_gss_credentials"),
    "SASL_CHANNEL_BINDING": ("borrowed nested record", "set_channel_binding",
                             "connection.set_channel_binding"),
    "SASL_HTTP_REQUEST": ("borrowed nested record", "set_http_request",
                          "connection.set_http_request"),
})


def cases(source, start, end):
    body = source[source.index(start):source.index(end)]
    return set(re.findall(r"\bcase\s+(SASL_\w+)\s*:", body))


def check(targets):
    for target in targets:
        source_path = (ROOT / ".cache/deps-build" / target /
                       "cyrus-sasl/src/lib/common.c")
        source = source_path.read_text()
        observed_get = cases(source, "int sasl_getprop(", "int sasl_setprop(")
        observed_set = cases(source, "int sasl_setprop(",
                             "static int sasl_usererr(")
        if observed_get != set(GET) or observed_set != set(SET):
            raise RuntimeError("{} property switch drift: get={} set={}".format(
                target, sorted(observed_get ^ set(GET)),
                sorted(observed_set ^ set(SET))))
        records = facade.inspect(target)["records"]
        receiver = records["cpkt_sasl"]
        utilities = records["cpkt_sasl_plugin_utils"]
        for property_name, (_, receiver_field, utility_field) in (
                list(GET.items()) + list(SET.items())):
            if receiver_field not in receiver:
                raise RuntimeError(property_name + " receiver operation missing")
            if utility_field.startswith("connection."):
                if "connection" not in utilities or utility_field[11:] not in receiver:
                    raise RuntimeError(property_name + " utility receiver path missing")
            elif utility_field not in utilities:
                raise RuntimeError(property_name + " utility operation missing")
    print("{} targets: 23 getprop and 10 setprop cases have typed receiver/utility paths".format(
        len(targets)))


if __name__ == "__main__":
    try:
        check(sys.argv[1:] or facade.native_contract.TARGETS)
    except (OSError, ValueError, RuntimeError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
