"""Select the same native Apple tools for producers and SDK consumers."""


def discover(command):
    tools = {variable: command(['xcrun', '--find', tool], capture=True).strip()
             for variable, tool in (
                 ('CMAKE_C_COMPILER', 'clang'),
                 ('CMAKE_CXX_COMPILER', 'clang++'),
                 ('CMAKE_NM', 'nm'),
                 ('CMAKE_AR', 'ar'),
                 ('CMAKE_OTOOL', 'otool'))}
    tools['CMAKE_OSX_SYSROOT'] = command(['xcrun', '--show-sdk-path'], capture=True).strip()
    return tools
