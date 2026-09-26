import argparse
import json
import re
import zipfile
from pathlib import Path

VALUE_PATTERN = re.compile(
    r'^\s*inline\s+constexpr\s+std::string_view\s+(Name|Author|Description|Version)\s*=\s*"((?:\\.|[^"\\])*)";\s*$'
)


def parse_version(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = VALUE_PATTERN.match(line)
        if match:
            values[match.group(1)] = bytes(match.group(2), "utf-8").decode("unicode_escape")
    required = ("Name", "Author", "Description", "Version")
    missing = [name for name in required if not values.get(name)]
    if missing:
        raise ValueError("Missing version metadata: " + ", ".join(missing))
    return values


def build_manifest(values: dict[str, str]) -> dict[str, object]:
    return {
        "type": "preload-native",
        "name": values["Name"],
        "author": values["Author"],
        "description": values["Description"],
        "version": values["Version"],
        "entry": "libSodiumSDLL.so",
        "icon": "icon.png",
        "overwrite_files": [],
        "overwrite_folders": ["resources/minecraft_resource_packs"],
    }


def add_tree(archive: zipfile.ZipFile, root: Path, prefix: str) -> None:
    for path in sorted(root.rglob("*")):
        if path.is_file():
            archive.write(path, f"{prefix}/{path.relative_to(root).as_posix()}")


def write_package(library: Path, icon: Path, version_header: Path, resource_pack: Path, output: Path) -> None:
    for path in (library, icon, version_header):
        if not path.is_file():
            raise FileNotFoundError(path)
    if not resource_pack.is_dir():
        raise FileNotFoundError(resource_pack)

    manifest = build_manifest(parse_version(version_header))
    output.parent.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.writestr("manifest.json", json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
        archive.write(library, "libSodiumSDLL.so")
        archive.write(icon, "icon.png")
        add_tree(archive, resource_pack, "resources/minecraft_resource_packs/sodium")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--icon", required=True, type=Path)
    parser.add_argument("--version-header", required=True, type=Path)
    parser.add_argument("--resource-pack", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    write_package(
        args.library.resolve(),
        args.icon.resolve(),
        args.version_header.resolve(),
        args.resource_pack.resolve(),
        args.output.resolve(),
    )
    print(args.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
