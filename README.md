# phantom-libs

Unmodified copies of open-source libraries that the [Phantom](https://github.com/newkayak12/Phantom) build ships and that only JetBrains hosts (`packages.jetbrains.team`, mostly JetBrains forks or repackagings). Phantom's Bazel build fetches them from here so that it does not depend on JetBrains hosting.

## What is here

- The files are assets of GitHub releases, not files in this repository. Each asset has its original file name.
  - Tag `jetbrains-hosted-1`: 39 libraries plus the 35 `-sources` jars that exist for them, 74 files.
  - Tag `jetbrains-hosted-2`: 4 libraries plus their 4 `-sources` jars, 8 files: debugger-agent, debugger-memory-agent, rwmutex-idea, and TeamCity serviceMessages. All four are Apache-2.0 according to their source headers or upstream repositories.
- [SOURCES.csv](SOURCES.csv) lists for every file its Maven coordinates, file name, sha256, original URL on `packages.jetbrains.team`, license (SPDX), and the upstream project.
- [LICENSES/](LICENSES) has the full text of every license used: Apache-2.0, MIT, BSD-3-Clause, LGPL-2.1, LGPL-3.0 (with the GPL-3.0 it refers to), and GPL-2.0 with the Classpath exception. Copyright notices for each library are inside its jar and in its sources jar or upstream repository.

The files are not changed in any way. This repository's own `LICENSE` (Apache-2.0) covers only the files written for this repository (README, SOURCES.csv). Each library keeps its own license.

Only libraries with an open-source license are mirrored. Libraries with no open-source license are not mirrored here.

## How the files are verified

Every file is pinned by sha256 in the Phantom tree (`lib/MODULE.bazel` and the other Bazel files), and the same values are in SOURCES.csv. Before upload, each file was downloaded from its original URL and checked against the pinned sha256. After upload, each asset was downloaded again from `https://github.com/newkayak12/phantom-libs/releases/download/<tag>/<file>` and checked again. Bazel checks the sha256 on every download, so a changed file fails the build.

## Adding or updating a file

1. Download the file from its original URL and check that its sha256 matches the value pinned in the Phantom tree.
2. Create a new release tag (for example `jetbrains-hosted-2`). Upload the file under its original name, then download it from the release URL and check it again.
3. Add a row to SOURCES.csv. If the license is new, add its text to LICENSES/.
4. In Phantom, add the file name to the rewrite in `build/bazel_downloader.cfg`, as described in `docs/phantom/BUILD.md`.
