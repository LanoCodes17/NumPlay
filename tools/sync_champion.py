#!/usr/bin/env python3
"""Champion Island as a repository of its own, from NumPlay's games/championisland.

The game lives in NumPlay, next to the others. Its own repository,
https://github.com/Mason363/NumWorks-Champion-Island, is a mirror: this script
builds the tree that goes there, and .github/workflows/sync-champion-island.yml
runs it on every push to main, so the mirror follows NumPlay and is never
edited by hand.

The mirror's root is the game's folder. What the game shares with NumPlay comes
along (the two Epsilon headers, the font's license, the README's animation) and
the few paths that point into NumPlay are rewritten for the new layout. Each
rewrite has to match exactly once, so a change in NumPlay that breaks one fails
the sync instead of shipping a broken mirror.

Usage: sync_champion.py DEST [--rev REV] [--commit]
  DEST      the mirror's checkout; everything in it but .git is replaced
  --rev     the NumPlay commit to build from (default HEAD): files come from
            that commit, not from the working tree
  --commit  commit the result in DEST if it changed, as the author of the last
            NumPlay commit that touched the game
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
REPO = "https://github.com/Mason363/NumPlay"

GAME = "games/championisland"
# NumPlay's files the game needs, and where they go in the mirror
SHARED = {
    "LICENSE": "LICENSE",
    "LICENSES/PixelMplus.txt": "LICENSES/PixelMplus.txt",
    "games/common/epsilon_app.h": "common/epsilon_app.h",
    "games/common/epsilon_files.h": "common/epsilon_files.h",
    "docs/media/championisland.gif": "docs/championisland.gif",
}
# paths that point into NumPlay: (file, old, new), each found exactly once
REWRITES = [
    ("Makefile", "../common/epsilon_app.h ../common/epsilon_files.h", "common/epsilon_app.h common/epsilon_files.h"),
    ("src/plat_eadk.c", '"../../common/epsilon_app.h"', '"../common/epsilon_app.h"'),
    ("src/plat_eadk.c", '"../../common/epsilon_files.h"', '"../common/epsilon_files.h"'),
    ("README.md", "](../../README.md)", "](%s#readme)" % REPO),
    ("README.md", 'src="../../docs/media/championisland.gif"', 'src="docs/championisland.gif"'),
    ("README.md", "](../../LICENSES/PixelMplus.txt)", "](LICENSES/PixelMplus.txt)"),
]
# what NumPlay's own .gitignore covers for the game, and the game's doesn't
GITIGNORE = ["output/", "build/", "__pycache__/", "*.pyc", ".DS_Store"]
NOTE = ("> This repository mirrors the `games/championisland` folder of NumPlay and is updated automatically: "
        "changes go to NumPlay, and edits made here are overwritten.")


def git(*args, cwd=ROOT, env=None, check=True, **kw):
    return subprocess.run(["git", "-C", cwd, *args], check=check, env=env, **kw)


def read(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def export(rev, dest):
    """The game and what it shares, as of rev, laid out as in NumPlay."""
    tar = git("archive", "--format=tar", rev, "--", GAME, *SHARED, stdout=subprocess.PIPE).stdout
    subprocess.run(["tar", "-x", "-C", dest], input=tar, check=True)


def build(rev, out):
    """The mirror's tree, as of rev, in the empty folder out."""
    with tempfile.TemporaryDirectory() as tmp:
        export(rev, tmp)
        shutil.copytree(os.path.join(tmp, GAME), out, dirs_exist_ok=True)
        for src, dst in SHARED.items():
            os.makedirs(os.path.dirname(os.path.join(out, dst)), exist_ok=True)
            shutil.copy2(os.path.join(tmp, src), os.path.join(out, dst))

    for name, old, new in REWRITES:
        path = os.path.join(out, name)
        text = read(path)
        if text.count(old) != 1:
            sys.exit("sync_champion: %s has %d matches of %r, expected 1: update REWRITES" % (name, text.count(old), old))
        write(path, text.replace(old, new))

    path = os.path.join(out, "README.md")
    text = read(path)
    first, sep, rest = text.partition("\n")
    if not first.startswith("> ") or not sep:
        sys.exit("sync_champion: README.md no longer starts with its quote: update NOTE")
    write(path, first + "\n>\n" + NOTE + "\n" + rest)

    path = os.path.join(out, ".gitignore")
    have = read(path).split("\n") if os.path.exists(path) else []
    have = [line for line in have if line]
    write(path, "\n".join(have + [line for line in GITIGNORE if line not in have]) + "\n")

    check_includes(out)


def check_includes(root):
    """Every quoted #include in the mirror finds its file (the ARM build isn't run here)."""
    missing = []
    for base, dirs, files in os.walk(root):
        for name in files:
            if not name.endswith((".c", ".h")):
                continue
            for inc in re.findall(r'^\s*#\s*include\s+"([^"]+)"', read(os.path.join(base, name)), re.M):
                if not any(os.path.exists(os.path.join(where, inc)) for where in (base, os.path.join(root, "src"))):
                    missing.append("%s: %s" % (os.path.relpath(os.path.join(base, name), root), inc))
    if missing:
        sys.exit("sync_champion: includes that no longer resolve in the mirror:\n  " + "\n  ".join(missing))


def replace(dest, out):
    """Make dest hold exactly out, keeping its .git."""
    os.makedirs(dest, exist_ok=True)
    for name in os.listdir(dest):
        if name != ".git":
            path = os.path.join(dest, name)
            shutil.rmtree(path) if os.path.isdir(path) and not os.path.islink(path) else os.remove(path)
    shutil.copytree(out, dest, dirs_exist_ok=True)


def commit(dest, rev):
    """Commit dest if it changed: the last NumPlay commit that touched the game gives the author and the title."""
    # without its own .git, git would climb to the repository around dest (NumPlay's checkout, in CI)
    if not os.path.isdir(os.path.join(dest, ".git")):
        sys.exit("sync_champion: %s is not a git checkout of the mirror, nothing to commit to" % dest)
    git("add", "-A", cwd=dest)
    if git("diff", "--cached", "--quiet", cwd=dest, check=False).returncode == 0:
        print("sync_champion: the mirror is already up to date")
        return
    name, email, date, subject = git(
        "log", "-1", "--format=%an%n%ae%n%aI%n%s", rev, "--", GAME, *SHARED,
        stdout=subprocess.PIPE, text=True).stdout.strip().split("\n", 3)
    sha = git("rev-parse", rev, stdout=subprocess.PIPE, text=True).stdout.strip()
    env = dict(os.environ, GIT_AUTHOR_NAME=name, GIT_AUTHOR_EMAIL=email, GIT_AUTHOR_DATE=date)
    git("commit", "-q", "-m", "%s\n\nSynced from %s/commit/%s" % (subject, REPO, sha), cwd=dest, env=env)
    print("sync_champion: committed %s" % subject)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dest")
    ap.add_argument("--rev", default="HEAD")
    ap.add_argument("--commit", action="store_true")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "mirror")
        build(args.rev, out)
        replace(args.dest, out)
    if args.commit:
        commit(args.dest, args.rev)


if __name__ == "__main__":
    main()
