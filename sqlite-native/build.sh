#!/usr/bin/env bash
# Builds sqlite-native-<version>.jar: libsqliteij for the six platforms the IntelliJ Platform's sqlite module loads
# (sqlite/<os>-<arch>/<lib> plus <lib>.sha256), from NativeDB.c here and the SQLite amalgamation.
# Needs zig (cross-compiles every target from one machine) and a JDK (JAVA_HOME, for jni.h).
#   ./build.sh            -> target/sqlite-native-<version>.jar
set -euo pipefail
cd "$(dirname "$0")"

SQLITE_VERSION=3.54.1
SQLITE_ZIP=sqlite-amalgamation-3540100.zip
SQLITE_YEAR=2026
# SHA3-256 published on https://sqlite.org/download.html
SQLITE_SHA3=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
PHANTOM_REVISION=1
VERSION="$SQLITE_VERSION-phantom$PHANTOM_REVISION"

JAVA_HOME="${JAVA_HOME:-$(/usr/libexec/java_home 2>/dev/null || true)}"
[ -f "$JAVA_HOME/include/jni.h" ] || { echo "JAVA_HOME with include/jni.h is required" >&2; exit 1; }
command -v zig >/dev/null || { echo "zig is required" >&2; exit 1; }

mkdir -p target
if [ ! -f "target/$SQLITE_ZIP" ]; then
  curl -fsSL -o "target/$SQLITE_ZIP" "https://sqlite.org/$SQLITE_YEAR/$SQLITE_ZIP"
fi
actual=$(python3 -c "import hashlib,sys;print(hashlib.sha3_256(open(sys.argv[1],'rb').read()).hexdigest())" "target/$SQLITE_ZIP")
[ "$actual" = "$SQLITE_SHA3" ] || { echo "SHA3-256 mismatch for $SQLITE_ZIP: $actual" >&2; exit 1; }
rm -rf target/amalgamation && mkdir target/amalgamation
unzip -qj "target/$SQLITE_ZIP" '*/sqlite3.c' '*/sqlite3.h' -d target/amalgamation

# Compile options of upstream platform/sqlite/make.sh (intellij-community), unchanged
SQLITE_DEFINES=(
  -DSQLITE_DQS=1
  -DSQLITE_THREADSAFE=1
  -DSQLITE_DEFAULT_MEMSTATUS=0
  -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1
  -DSQLITE_LIKE_DOESNT_MATCH_BLOBS
  -DSQLITE_MAX_EXPR_DEPTH=0
  -DSQLITE_OMIT_DECLTYPE
  -DSQLITE_OMIT_DEPRECATED
  -DSQLITE_OMIT_PROGRESS_CALLBACK
  -DSQLITE_OMIT_SHARED_CACHE
  -DSQLITE_USE_ALLOCA
  -DSQLITE_OMIT_AUTOINIT
  -DSQLITE_HAVE_ISNAN
  -DHAVE_USLEEP=1
  -DSQLITE_TEMP_STORE=2
  -DSQLITE_DEFAULT_CACHE_SIZE=2000
  -DSQLITE_CORE
  -DSQLITE_ENABLE_FTS5
  -DSQLITE_ENABLE_STAT4
  -DSQLITE_MAX_MMAP_SIZE=1099511627776
)

# <dir in jar> <zig target> <library file> <jni_md.h dir>
TARGETS=(
  "mac-aarch64   aarch64-macos.11.0     libsqliteij.jnilib $JAVA_HOME/include/darwin"
  "mac-x86_64    x86_64-macos.11.0      libsqliteij.jnilib $JAVA_HOME/include/darwin"
  "linux-x86_64  x86_64-linux-gnu.2.17  libsqliteij.so     $JAVA_HOME/include/darwin"
  "linux-aarch64 aarch64-linux-gnu.2.17 libsqliteij.so     $JAVA_HOME/include/darwin"
  "win-x86_64    x86_64-windows-gnu     sqliteij.dll       include/win32"
  "win-aarch64   aarch64-windows-gnu    sqliteij.dll       include/win32"
)

rm -rf target/sqlite target/obj
for t in "${TARGETS[@]}"; do
  read -r dir zigTarget lib mdDir <<<"$t"
  out="target/sqlite/$dir"
  obj="target/obj/$dir"
  mkdir -p "$out" "$obj"
  cflags=(-target "$zigTarget" -O3 -fPIC -fvisibility=hidden -Itarget/amalgamation -I"$JAVA_HOME/include" -I"$mdDir")
  zig cc "${cflags[@]}" "${SQLITE_DEFINES[@]}" -c target/amalgamation/sqlite3.c -o "$obj/sqlite3.o"
  zig cc "${cflags[@]}" -c NativeDB.c -o "$obj/NativeDB.o"
  zig cc -target "$zigTarget" -shared -s -o "$out/$lib" "$obj/NativeDB.o" "$obj/sqlite3.o"
  shasum -a 256 "$out/$lib" | head -c 64 >"$out/$lib.sha256"
  echo "built $dir/$lib"
done

rm -f "target/sqlite-native-$VERSION.jar"
(cd target && TZ=UTC find sqlite -exec touch -t 202601010000 {} + && zip -qrX "sqlite-native-$VERSION.jar" sqlite)
shasum -a 256 "target/sqlite-native-$VERSION.jar"
