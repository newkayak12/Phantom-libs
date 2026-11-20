# phantom-libs

Unmodified copies of open-source libraries that the [Phantom](https://github.com/newkayak12/Phantom) build ships and that only JetBrains hosts (`packages.jetbrains.team`, mostly JetBrains forks or repackagings). Phantom's Bazel build fetches them from here so that it does not depend on JetBrains hosting.

## What is here

- The files are assets of GitHub releases, not files in this repository. Each asset has its original file name.
  - Tag `jetbrains-hosted-1`: 39 libraries plus the 35 `-sources` jars that exist for them, 74 files.
  - Tag `jetbrains-hosted-2`: 4 libraries plus their 4 `-sources` jars, 8 files: debugger-agent, debugger-memory-agent, rwmutex-idea, and TeamCity serviceMessages. All four are Apache-2.0 according to their source headers or upstream repositories.
  - Tag `jetbrains-hosted-3`: JCEF 150.0.14 (Chromium 150.0.7871.129) jar and sources jar, 2 files. Phantom moved to it from JCEF 144 for Chromium security fixes.
- [SOURCES.csv](SOURCES.csv) lists for every file its Maven coordinates, file name, sha256, original URL on `packages.jetbrains.team`, license (SPDX), and the upstream project.
- [LICENSES/](LICENSES) has the full text of every license used: Apache-2.0, MIT, BSD-3-Clause, LGPL-2.1, LGPL-3.0 (with the GPL-3.0 it refers to), and GPL-2.0 with the Classpath exception. Copyright notices for each library are inside its jar and in its sources jar or upstream repository.

The files of these two tags are not changed in any way.

## Security patches

Tag `phantom-patched-1` holds patched builds of mirrored libraries with known vulnerabilities that their vendor has not fixed in the version Phantom ships. Each file name has a `-phantom<N>` suffix, so a patched file is never mistaken for the original.

- [PATCHED.csv](PATCHED.csv) lists for every patched file its sha256, the original file it was made from (with that file's sha256, as in SOURCES.csv), the patch, the vulnerability it fixes, and the license.
- [patches/](patches) has each patch as a unified diff against the original sources jar. Every changed place is marked with a `phantom:` comment.
- A patched jar is the original jar with only the classes of the changed source files replaced, compiled against the original jar. The patched sources jar is the original sources jar with the changed source files replaced.

| File | Fixes | Change |
|---|---|---|
| org.eclipse.jgit 6.6.1 (jb-202510311356) | CVE-2025-4949 (XXE) | `ManifestParser` and `AmazonS3` parse XML with DOCTYPE and external entities disabled, as JGit 6.10.1 does. |
| ini4j 0.5.5-2 | CVE-2022-41404 (DoS) | Variable substitution in `BasicProfile.fetch()` stops at a nesting depth of 32 and 1024 substitutions, instead of looping or overflowing the stack on a value that refers to itself. | This repository's own `LICENSE` (Apache-2.0) covers only the files written for this repository (README, SOURCES.csv). Each library keeps its own license.

Only libraries with an open-source license are mirrored. Libraries with no open-source license are not mirrored here.

## How the files are verified

Every file is pinned by sha256 in the Phantom tree (`lib/MODULE.bazel` and the other Bazel files), and the same values are in SOURCES.csv. Before upload, each file was downloaded from its original URL and checked against the pinned sha256. After upload, each asset was downloaded again from `https://github.com/newkayak12/phantom-libs/releases/download/<tag>/<file>` and checked again. Bazel checks the sha256 on every download, so a changed file fails the build.

## Adding or updating a file

1. Download the file from its original URL and check that its sha256 matches the value pinned in the Phantom tree.
2. Create a new release tag (for example `jetbrains-hosted-2`). Upload the file under its original name, then download it from the release URL and check it again.
3. Add a row to SOURCES.csv. If the license is new, add its text to LICENSES/.
4. In Phantom, add the file name to the rewrite in `build/bazel_downloader.cfg`, as described in `docs/phantom/BUILD.md`.
