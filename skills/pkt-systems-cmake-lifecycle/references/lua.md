# Declared Lua facade/source products

Apply only to selected Lua surfaces. Lua 5.5.1 is the pkt.systems contract; alternate
Lua/LuaJIT support is not implied. An intentional bundle of upstream Lua headers,
libraries or interpreter is a separate SDK product, not a downstream source facade.

A binding uses installed public headers and the public shared C library, not a
private/static duplicate. Keep core C interfaces language-agnostic. Interop headers
and userdata conversion live with the facade. Expose generic borrowed views only
for real consumers, document lifetime/ABI rules and pin owners across callbacks.
Expected validation errors return project status rather than forcing longjmp.

Preserve actual streaming; name buffered/spooled alternatives explicitly. Test
ownership, callback/finalizer behavior and public workflow parity where applicable.

Linux tests loading Bootlin modules use a local interpreter/embedding executable
with that runtime; host Lua tooling alone does not prove target compatibility.
Use installed-SDK layout in local facade tests. LuaRocks state belongs under build,
with explicit dependency/header/compiler selection and usable diagnostics.

Declared Lua release products are a minimal source archive, release rockspec and
source rock. Include every source/generator/header needed to build the facade,
license, version and manifest. Source URLs are public/logical, never local file URLs.
Scan nested payload as final artifact bytes and include all selected Lua products
in the complete checksum inventory. Keep downstream source facade/package-manager
content out of per-target C SDKs unless a combined product is explicitly declared.
