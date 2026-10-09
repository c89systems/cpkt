"""Preflight explicit project-owned generator outputs without resolving redirects."""
from pathlib import Path


def validate_output_paths(*paths):
    # Inspect lexical ancestry before mkdir, writes, chmod, removal or children.
    # Inputs and upstream/toolchain symlinks are intentionally outside this set.
    for value in paths:
        path = Path(value)
        if not path.is_absolute():
            path = Path.cwd() / path
        for ancestor in (path, *path.parents):
            if ancestor.is_symlink():
                raise ValueError("Generated output has a symlink ancestor: " + str(ancestor))


def write_generated_text(path, text, encoding="utf-8"):
    """Keep identical output bytes, modes and timestamps for native restat."""
    path = Path(path)
    validate_output_paths(path)
    content = text.encode(encoding)
    if path.is_file() and path.read_bytes() == content:
        return
    path.write_bytes(content)
