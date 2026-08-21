#!/usr/bin/env python3
"""Generates one Breathe stub page per documentable `Halide::` API entity.

Reads Doxygen's XML output (see doc/CMakeLists.txt's `doc_doxygen` target)
and writes <output-dir>/<Name>.md pages, one <output-dir>/group_<Header>.md
per originating header (e.g. everything Doxygen attributes to Type.h), and
an api/reference.md toctree over the groups -- grouping by header instead of
listing all types flat gives the left-sidebar "Section Navigation" real
hierarchy to show, and roughly matches Halide's own convention of one
concept per header. doc/CMakeLists.txt points --output-dir at a staged copy
of the Sphinx source tree under the build directory, not doc/api/ itself,
so this never writes into the checked-out source tree. doc/api/index.md
(the API Reference landing page) is hand-authored and toctrees to the
generated reference.md.

Coverage is seeded from:

- every top-level class/struct/union directly in `Halide::` or one of its
  non-`Internal` sub-namespaces (e.g. `Halide::Runtime::Buffer`), but not
  nested inside a *class* (e.g. `Pipeline::RealizationArg`) -- those are
  already documented inline wherever their parent's `:members:` expands
  them, so a separate page would just produce a Sphinx "Duplicate C++
  declaration" warning for the same symbol;
- every enum/typedef declared directly in `Halide::` or one of those same
  non-`Internal` sub-namespaces (e.g. `Halide::DeviceAPI`);
- every enum/typedef declared directly in HalideRuntime.h (e.g.
  `halide_type_code_t`) -- the C ABI surface user code and generated code
  both link against, which isn't inside any C++ namespace at all.

From those roots, every class/struct/union any included type's own members
reference (e.g. `Halide::Type`'s conversions to/from the ABI's
`halide_type_t`) is pulled in transitively, so the reference doesn't
dead-end on an undocumented page -- except through `Halide::Internal`,
which is cut off after exactly one hop (e.g. `Module::get_conceptual_stmt`
returning `Halide::Internal::Stmt` gets `Stmt` a page, but nothing `Stmt`
itself references is pulled in -- otherwise this would eventually reach
most of the IR). `Internal` entries reached this way are grouped into a
single blanket "Internal" section rather than by header, since being
reachable from the public API is what earned them a page, not being part
of any particular public-facing concept.

`src/runtime/` is excluded from all of the above for now, apart from
HalideRuntime.h and HalideBuffer.h (the two headers whose C/C++ ABI surface
is meant to be public) -- the rest of that directory is the runtime's own
implementation, mixes forward declarations with separate full definitions
(e.g. `halide_device_interface_impl_t`), and needs its own rules to surface
sensibly, which hasn't been worked out yet.
"""

import argparse
import xml.etree.ElementTree as ET
from pathlib import Path

_COMPOUND_DIRECTIVES = {
    "class": "doxygenclass",
    "struct": "doxygenstruct",
    "union": "doxygenunion",
}
_MEMBER_DIRECTIVES = {
    "enum": "doxygenenum",
    "typedef": "doxygentypedef",
}
# Higher-priority kinds win when a name is discovered under more than one
# kind (e.g. `halide_buffer_t` is both a real `struct` compound and a
# `typedef` alias of the same name for it).
_KIND_PRIORITY = ["class", "struct", "union", "enum", "typedef"]


class Entry:
    def __init__(self, name: str, kind: str, file: str):
        self.name = name
        self.kind = kind
        self.file = file

    @property
    def directive(self) -> str:
        return {**_COMPOUND_DIRECTIVES, **_MEMBER_DIRECTIVES}[self.kind]

    @property
    def is_internal(self) -> bool:
        return _is_internal(self.name)

    @property
    def display_name(self) -> str:
        return self.name.removeprefix("Halide::")


def _is_internal(name: str) -> bool:
    # Segment-exact match, not a substring check: "Halide::Runtime::Internal"
    # (the Internal namespace itself, as opposed to something nested inside
    # it) needs to match too, but "Halide::InternalFoo" shouldn't.
    return "Internal" in name.split("::")


def _is_real_name(name: str) -> bool:
    # Template specializations (e.g. SFINAE detection traits) and
    # anonymous-namespace-derived names aren't real top-level types.
    return "<" not in name and "@" not in name


_RUNTIME_ALLOWED_FILES = {"HalideRuntime.h", "HalideBuffer.h"}


def _is_excluded_runtime_file(file: str) -> bool:
    if not file.startswith("src/runtime/"):
        return False
    return Path(file).name not in _RUNTIME_ALLOWED_FILES


def _is_nested_in_class(name: str, namespace_names: set[str]) -> bool:
    """True if `name`'s immediate enclosing scope is a class/struct rather
    than a namespace (e.g. Pipeline::RealizationArg, nested in the
    Pipeline *class* -- as opposed to Runtime::Buffer, nested in the
    Runtime *namespace*, which is a real top-level type)."""
    prefix, sep, _ = name.rpartition("::")
    return bool(sep) and prefix not in namespace_names


def _load_index(xml_dir: Path):
    """Returns (by_refid, by_name, namespace_names, root_namespaces, halide_runtime_h_refid).

    `namespace_names` covers every Halide:: namespace (Internal ones
    included) and is only used to distinguish namespace-nesting from
    class-nesting. `root_namespaces` maps qualified name -> refid for just
    the non-Internal ones, which is what root enum/typedef members get
    seeded from.
    """
    root = ET.parse(xml_dir / "index.xml").getroot()
    by_refid = {}
    by_name = {}
    namespace_names = set()
    root_namespaces = {}
    halide_runtime_h_refid = None
    for compound in root.findall("compound"):
        kind = compound.get("kind")
        refid = compound.get("refid")
        name = compound.find("name").text
        if kind == "namespace" and (name == "Halide" or name.startswith("Halide::")):
            if not _is_real_name(name):
                continue
            namespace_names.add(name)
            if not _is_internal(name):
                root_namespaces[name] = refid
        elif kind == "file" and name == "HalideRuntime.h":
            halide_runtime_h_refid = refid
        elif kind in _COMPOUND_DIRECTIVES:
            by_refid[refid] = (name, kind)
            by_name[name] = (refid, kind)
    return by_refid, by_name, namespace_names, root_namespaces, halide_runtime_h_refid


def _member_names(xml_dir: Path, container_refid: str, member_kinds, name_prefix: str):
    """Direct enum/typedef members of a namespace or file compound (not nested further)."""
    root = ET.parse(xml_dir / f"{container_refid}.xml").getroot()
    members = []
    for sectiondef in root.iter("sectiondef"):
        if sectiondef.get("kind") not in member_kinds:
            continue
        for memberdef in sectiondef.findall("memberdef"):
            name = memberdef.find("name").text
            # Anonymous enums get a synthetic "@N" name on newer Doxygen, but
            # an empty <name/> (None here) on older ones (e.g. 1.9.8) -- both
            # mean the same "no real name" thing and should be skipped.
            if not name or "@" in name:
                continue
            location = memberdef.find("location")
            file = location.get("file") if location is not None else "unknown"
            members.append((f"{name_prefix}{name}", memberdef.get("kind"), file))
    return members


def _compound_refs_and_file(xml_dir: Path, refid: str):
    """Returns (file, referenced_compound_refids) for a class/struct/union compound."""
    root = ET.parse(xml_dir / f"{refid}.xml").getroot()
    location = root.find("compounddef/location")
    file = location.get("file") if location is not None else "unknown"
    refs = {
        ref.get("refid") for ref in root.iter("ref") if ref.get("kindref") == "compound"
    }
    return file, refs


def _discover_types(xml_dir: Path) -> list[Entry]:
    """Returns a sorted list of documentable Entry objects."""
    by_refid, by_name, namespace_names, root_namespaces, halide_runtime_h_refid = (
        _load_index(xml_dir)
    )

    # kind_by_name/file_by_name track the highest-priority kind seen so far
    # for each name (see _KIND_PRIORITY), and where it's declared.
    kind_by_name: dict[str, str] = {}
    file_by_name: dict[str, str] = {}

    def offer(name: str, kind: str, file: str) -> None:
        current = kind_by_name.get(name)
        if current is None or _KIND_PRIORITY.index(kind) < _KIND_PRIORITY.index(
            current
        ):
            kind_by_name[name] = kind
            file_by_name[name] = file

    # Root enums/typedefs: direct members of Halide:: and its non-Internal
    # sub-namespaces, plus HalideRuntime.h (no namespace prefix -- it's C ABI).
    for namespace_name, namespace_refid in root_namespaces.items():
        for name, kind, file in _member_names(
            xml_dir, namespace_refid, ("enum", "typedef"), f"{namespace_name}::"
        ):
            if _is_excluded_runtime_file(file):
                continue
            offer(name, kind, file)
    if halide_runtime_h_refid is not None:
        for name, kind, file in _member_names(
            xml_dir, halide_runtime_h_refid, ("enum", "typedef"), ""
        ):
            offer(name, kind, file)

    def is_includable_root(name: str) -> bool:
        return (
            name.startswith("Halide::")
            and _is_real_name(name)
            and not _is_internal(name)
            and not _is_nested_in_class(name, namespace_names)
        )

    # Root compounds (top-level Halide:: class/struct/union, including
    # non-Internal sub-namespaces), transitively closed over compound
    # (class/struct/union) cross-references -- but only one hop into
    # Halide::Internal (see module docstring). Every refid that reaches this
    # worklist -- root or closure-discovered -- gets its own detail XML
    # opened exactly once, for both its file and its refs.
    worklist = [
        refid for name, (refid, _) in by_name.items() if is_includable_root(name)
    ]
    visited_refids = set(worklist)
    while worklist:
        refid = worklist.pop()
        name, kind = by_refid[refid]
        file, refs = _compound_refs_and_file(xml_dir, refid)
        if _is_excluded_runtime_file(file):
            # Excluded outright: neither included nor a source of further
            # references (see module docstring).
            continue
        offer(name, kind, file)
        if _is_internal(name):
            # One-hop cutoff: don't chase this Internal entry's own
            # references any further.
            continue
        for ref_refid in refs:
            if ref_refid in visited_refids or ref_refid not in by_refid:
                continue
            ref_name, _ = by_refid[ref_refid]
            if not _is_real_name(ref_name) or _is_nested_in_class(
                ref_name, namespace_names
            ):
                continue
            visited_refids.add(ref_refid)
            worklist.append(ref_refid)

    return sorted(
        (Entry(name, kind, file_by_name[name]) for name, kind in kind_by_name.items()),
        key=lambda e: e.name,
    )


def _slug(name: str) -> str:
    return name.removeprefix("Halide::").replace("::", "_")


def _write_toctree(path: Path, title: str, entries: list[str]) -> None:
    lines = [
        f"# {title}",
        "",
        "```{toctree}",
        ":titlesonly:",
        ":maxdepth: 1",
        "",
        *entries,
        "```\n",
    ]
    path.write_text("\n".join(lines))


def generate(xml_dir: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    entries = _discover_types(xml_dir)

    groups: dict[str, list[Entry]] = {}
    for entry in entries:
        key = "Internal" if entry.is_internal else Path(entry.file).name
        groups.setdefault(key, []).append(entry)

    # (display_label, slug) pairs, sorted by label at the end so a header
    # with only one entry (displayed under its own name, not its header's)
    # sorts alongside everything else by that displayed name, not by the
    # invisible header it happened to come from.
    top_level: list[tuple[str, str]] = []
    for header, group_entries in groups.items():
        for entry in group_entries:
            slug = _slug(entry.name)
            options = (
                ":members:\n"
                if entry.directive in _COMPOUND_DIRECTIVES.values()
                else ""
            )
            (output_dir / f"{slug}.md").write_text(
                f"# {entry.display_name}\n\n```{{{entry.directive}}} {entry.name}\n{options}```\n"
            )

        if header != "Internal" and len(group_entries) == 1:
            # A header with only one documentable entity (almost always
            # named after itself, e.g. Buffer.h -> Buffer) doesn't need its
            # own wrapper page -- link straight to the entity's own page.
            entry = group_entries[0]
            top_level.append((entry.display_name, _slug(entry.name)))
            continue

        group_slug = f"group_{header.replace('.', '_')}"
        _write_toctree(
            output_dir / f"{group_slug}.md",
            header,
            [_slug(e.name) for e in sorted(group_entries, key=lambda e: e.name)],
        )
        top_level.append((header, group_slug))

    # Case-insensitive: labels mix Capitalized headers/types with lowercase
    # C-ABI names (halide_device_interface_impl_t), and a plain sort would
    # dump every lowercase-first label after every uppercase-first one
    # rather than interleaving them the way a reader would expect.
    top_level.sort(key=lambda pair: pair[0].casefold())
    _write_toctree(
        output_dir / "reference.md", "All Types", [slug for _, slug in top_level]
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--xml-dir", type=Path, required=True, help="Doxygen XML output directory"
    )
    parser.add_argument(
        "--output-dir", type=Path, required=True, help="Where to write <Name>.md"
    )
    args = parser.parse_args()
    generate(args.xml_dir, args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
