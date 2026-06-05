#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import subprocess
from datetime import datetime, timezone
from pathlib import Path

PLATFORMS = ("macos-universal", "windows-x64", "linux-x64")


def platform_from_name(name: str) -> str:
    for platform in PLATFORMS:
        if name.endswith(f"-{platform}.zip"):
            return platform
    raise SystemExit(f"cannot infer platform from asset name: {name}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_rev(path: Path):
    try:
        return subprocess.check_output(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            text=True,
        ).strip()
    except subprocess.CalledProcessError:
        return None


def find_nozzle_core_sha():
    for candidate in (Path("deps/nozzle"), Path("nozzle"), Path("libs/nozzle")):
        if candidate.is_dir():
            value = git_rev(candidate)
            if value:
                return value
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Write standalone app release manifest from built artifacts.")
    parser.add_argument("--app-id", required=True)
    parser.add_argument("--expected-platforms", nargs="+", required=True)
    parser.add_argument("--output", default="manifest.json")
    parser.add_argument("assets", nargs="+")
    args = parser.parse_args()

    expected_platforms = args.expected_platforms
    seen_platforms = set()
    assets = []
    for asset_arg in args.assets:
        path = Path(asset_arg)
        if not path.is_file():
            raise SystemExit(f"missing package: {path}")
        name = path.name
        platform = platform_from_name(name)
        if platform in seen_platforms:
            raise SystemExit(f"duplicate platform asset: {platform}")
        seen_platforms.add(platform)
        assets.append({
            "name": name,
            "platform": platform,
            "sha256": sha256(path),
            "required": platform in expected_platforms,
        })

    missing = sorted(set(expected_platforms) - seen_platforms)
    if missing:
        raise SystemExit(f"missing required platform assets: {', '.join(missing)}")

    release_type = "latest" if os.environ["GITHUB_REF"] == "refs/heads/main" else "versioned"
    release_tag = "latest" if release_type == "latest" else os.environ["GITHUB_REF_NAME"]
    manifest = {
        "schema_version": "1.0",
        "app_id": args.app_id,
        "source_repo": os.environ["GITHUB_REPOSITORY"],
        "source_commit_sha": os.environ["GITHUB_SHA"],
        "nozzle_core_sha": find_nozzle_core_sha(),
        "release_tag": release_tag,
        "release_type": release_type,
        "generated_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "workflow_run_url": f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}/actions/runs/{os.environ['GITHUB_RUN_ID']}",
        "expected_platforms": expected_platforms,
        "assets": assets,
    }
    output = Path(args.output)
    output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
