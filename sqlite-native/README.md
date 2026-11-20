# sqlite-native

`libsqliteij`, the native library of the IntelliJ Platform's SQLite module (`intellij.platform.sqlite`, Kotlin class `org.jetbrains.sqlite.NativeDB`), built for Phantom.

JetBrains publishes the Kotlin side and the build script (`platform/sqlite/make.sh` in intellij-community) but not the C side, and its binary (`org.sqlite:native:3.42.0-jb.1`) is stuck on SQLite 3.42.0. Here:

- `NativeDB.c`: the JNI functions for the 33 `external fun` declarations in `NativeDB.kt`, adapted from xerial/sqlite-jdbc's `NativeDB.c` (ISC notice kept in the file).
- `include/win32/jni_md.h`: JNI machine types for cross-compiling the Windows DLLs.
- `build.sh`: downloads the SQLite amalgamation (checked against the SHA3-256 on sqlite.org), compiles it with the same options as upstream `make.sh`, and links `libsqliteij` for mac, linux and win on x86_64 and aarch64 with zig. The jar layout (`sqlite/<os>-<arch>/<lib>` plus `<lib>.sha256`) is what `sqliteLibLoader.kt` reads.

Releases are tagged `sqlite-native-<sqlite version>-phantom<N>`; Phantom pins the jar's sha256 in `lib/MODULE.bazel`.

To update SQLite: change `SQLITE_VERSION`, `SQLITE_ZIP`, `SQLITE_YEAR` and `SQLITE_SHA3` in `build.sh`, run it (needs zig and a JDK), run Phantom's `//platform/sqlite:sqlite-tests_test` against the new jar, then release it under a new tag.
