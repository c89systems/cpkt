file(READ "${CPKT_MANIFEST}" manifest)
string(JSON id GET "${manifest}" package_id)
string(JSON ids SET "{}" core "\"${id}\"")
file(WRITE "${CPKT_OUTPUT}" "${ids}")
