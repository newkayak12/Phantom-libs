/*
 * JNI layer of org.jetbrains.sqlite.NativeDB (IntelliJ Platform module intellij.platform.sqlite) over the SQLite C API.
 *
 * Written for Phantom: JetBrains publishes the Kotlin side (platform/sqlite in intellij-community, Apache-2.0) but not
 * the C side of its libsqliteij. The functions follow the Kotlin declarations in NativeDB.kt and SqliteDb.kt; their
 * bodies are adapted from NativeDB.c of xerial/sqlite-jdbc (https://github.com/xerial/sqlite-jdbc), whose notice
 * follows.
 *
 * Copyright (c) 2007 David Crawshaw <david@zentus.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jni.h>
#include "sqlite3.h"

#define EXPORT(ret) JNIEXPORT ret JNICALL __attribute__((visibility("default")))

static jclass dbclass = 0;
static jfieldID dbpointer = 0;
static jfieldID db_busyHandler = 0;
static jmethodID mth_throwex = 0;
static jmethodID mth_throwexcode = 0;
static jmethodID mth_throwexmsg = 0;

static jclass bhandleclass = 0;
static jmethodID bhandle_mth_callback = 0;

static jclass pobserverclass = 0;
static jmethodID pobserver_mth_progress = 0;

static jclass illegalstateclass = 0;

static void *toref(jlong value) {
  void *ret;
  memcpy(&ret, &value, sizeof(void *));
  return ret;
}

static jlong fromref(void *value) {
  jlong ret = 0;
  memcpy(&ret, &value, sizeof(void *));
  return ret;
}

/* SqliteDb.throwex(): IOException with errmsg() */
static void throwex(JNIEnv *env, jobject this) {
  (*env)->CallVoidMethod(env, this, mth_throwex);
}

/* SqliteDb.throwex(int): SqliteException for the code, with errmsg() */
static void throwex_errorcode(JNIEnv *env, jobject this, int errorCode) {
  (*env)->CallVoidMethod(env, this, mth_throwexcode, (jint)errorCode);
}

/* NativeDB.throwex(String): IOException with the message */
static void throwex_msg(JNIEnv *env, const char *str) {
  (*env)->CallStaticVoidMethod(env, dbclass, mth_throwexmsg, (*env)->NewStringUTF(env, str));
}

static void throwex_outofmemory(JNIEnv *env) { throwex_msg(env, "Out of memory"); }

static void throwex_stmt_finalized(JNIEnv *env) { throwex_msg(env, "The prepared statement has been finalized"); }

static void throwex_db_closed(JNIEnv *env) { throwex_msg(env, "The database has been closed"); }

static jobject utf8BytesToDirectByteBuffer(JNIEnv *env, const char *bytes, int nbytes) {
  jobject result;
  if (!bytes) return NULL;
  result = (*env)->NewDirectByteBuffer(env, (void *)bytes, nbytes);
  if (!result) {
    throwex_outofmemory(env);
    return NULL;
  }
  return result;
}

/* A NUL-terminated copy of a Java byte[] (UTF-8); *bytes is NULL for a null array or when out of memory */
static void utf8JavaByteArrayToUtf8Bytes(JNIEnv *env, jbyteArray utf8bytes, char **bytes, int *nbytes) {
  jsize length;
  char *buf;

  *bytes = NULL;
  if (nbytes) *nbytes = 0;
  if (!utf8bytes) return;

  length = (*env)->GetArrayLength(env, (jarray)utf8bytes);
  buf = (char *)malloc(length + 1);
  if (!buf) {
    throwex_outofmemory(env);
    return;
  }
  (*env)->GetByteArrayRegion(env, utf8bytes, 0, length, (jbyte *)buf);
  buf[length] = '\0';

  *bytes = buf;
  if (nbytes) *nbytes = (int)length;
}

static void freeUtf8Bytes(char *bytes) {
  if (bytes) free(bytes);
}

static sqlite3 *gethandle(JNIEnv *env, jobject nativeDB) {
  return (sqlite3 *)toref((*env)->GetLongField(env, nativeDB, dbpointer));
}

static void sethandle(JNIEnv *env, jobject nativeDB, sqlite3 *ref) {
  (*env)->SetLongField(env, nativeDB, dbpointer, fromref(ref));
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv *env = 0;
  jclass c;
  (void)reserved;

  if (JNI_OK != (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_2)) return JNI_ERR;

  // built with SQLITE_OMIT_AUTOINIT: the library must be initialized before the first sqlite3_open_v2()
  if (sqlite3_initialize() != SQLITE_OK) return JNI_ERR;

  c = (*env)->FindClass(env, "org/jetbrains/sqlite/NativeDB");
  if (!c) return JNI_ERR;
  dbclass = (*env)->NewWeakGlobalRef(env, c);
  dbpointer = (*env)->GetFieldID(env, c, "pointer", "J");
  db_busyHandler = (*env)->GetFieldID(env, c, "busyHandler", "J");
  mth_throwex = (*env)->GetMethodID(env, c, "throwex", "()V");
  mth_throwexcode = (*env)->GetMethodID(env, c, "throwex", "(I)V");
  mth_throwexmsg = (*env)->GetStaticMethodID(env, c, "throwex", "(Ljava/lang/String;)V");
  if (!dbpointer || !db_busyHandler || !mth_throwex || !mth_throwexcode || !mth_throwexmsg) return JNI_ERR;

  c = (*env)->FindClass(env, "org/jetbrains/sqlite/BusyHandler");
  if (!c) return JNI_ERR;
  bhandleclass = (*env)->NewWeakGlobalRef(env, c);
  bhandle_mth_callback = (*env)->GetMethodID(env, c, "callback", "(I)I");
  if (!bhandle_mth_callback) return JNI_ERR;

  c = (*env)->FindClass(env, "org/jetbrains/sqlite/SqliteDb$ProgressObserver");
  if (!c) return JNI_ERR;
  pobserverclass = (*env)->NewWeakGlobalRef(env, c);
  pobserver_mth_progress = (*env)->GetMethodID(env, c, "progress", "(II)V");
  if (!pobserver_mth_progress) return JNI_ERR;

  c = (*env)->FindClass(env, "java/lang/IllegalStateException");
  if (!c) return JNI_ERR;
  illegalstateclass = (*env)->NewGlobalRef(env, c);

  return JNI_VERSION_1_2;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
  JNIEnv *env = 0;
  (void)reserved;
  if (JNI_OK != (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_2)) return;
  if (dbclass) (*env)->DeleteWeakGlobalRef(env, dbclass);
  if (bhandleclass) (*env)->DeleteWeakGlobalRef(env, bhandleclass);
  if (pobserverclass) (*env)->DeleteWeakGlobalRef(env, pobserverclass);
  if (illegalstateclass) (*env)->DeleteGlobalRef(env, illegalstateclass);
}

// BUSY HANDLER ////////////////////////////////////////////////////

struct BusyHandlerContext {
  JavaVM *vm;
  jobject obj;
};

static int busyHandlerCallBack(void *callback, int nbPrevInvok) {
  JNIEnv *env = 0;
  struct BusyHandlerContext *ctx = (struct BusyHandlerContext *)callback;
  (*ctx->vm)->AttachCurrentThread(ctx->vm, (void **)&env, 0);
  return (*env)->CallIntMethod(env, ctx->obj, bhandle_mth_callback, nbPrevInvok);
}

static void change_busy_handler(JNIEnv *env, jobject nativeDB, sqlite3 *db, jobject busyHandler) {
  struct BusyHandlerContext *ctx = NULL;
  struct BusyHandlerContext *old;

  if (busyHandler) {
    ctx = (struct BusyHandlerContext *)malloc(sizeof(struct BusyHandlerContext));
    if (!ctx) {
      throwex_outofmemory(env);
      return;
    }
    (*env)->GetJavaVM(env, &ctx->vm);
    ctx->obj = (*env)->NewGlobalRef(env, busyHandler);
    sqlite3_busy_handler(db, &busyHandlerCallBack, ctx);
  }
  else {
    sqlite3_busy_handler(db, NULL, NULL);
  }

  old = (struct BusyHandlerContext *)toref((*env)->GetLongField(env, nativeDB, db_busyHandler));
  if (old) {
    (*env)->DeleteGlobalRef(env, old->obj);
    free(old);
  }
  (*env)->SetLongField(env, nativeDB, db_busyHandler, fromref(ctx));
}

// OPEN AND CLOSE //////////////////////////////////////////////////

/* Returns the result code; on failure the connection is closed again and NativeDB.open() throws */
EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_open(JNIEnv *env, jobject this, jbyteArray file, jint flags) {
  sqlite3 *db;
  int ret;
  char *file_bytes;

  db = gethandle(env, this);
  if (db) {
    throwex_msg(env, "DB already open");
    return SQLITE_MISUSE;
  }

  utf8JavaByteArrayToUtf8Bytes(env, file, &file_bytes, NULL);
  if (!file_bytes) return SQLITE_NOMEM;

  db = NULL;
  ret = sqlite3_open_v2(file_bytes, &db, flags, NULL);
  freeUtf8Bytes(file_bytes);

  if (ret != SQLITE_OK) {
    if (db) {
      ret = sqlite3_extended_errcode(db);
      sqlite3_close(db);
    }
    return ret;
  }

  (void)sqlite3_extended_result_codes(db, 1);
  sethandle(env, this, db);
  return SQLITE_OK;
}

EXPORT(void) Java_org_jetbrains_sqlite_NativeDB__1close(JNIEnv *env, jobject nativeDB) {
  sqlite3 *db = gethandle(env, nativeDB);
  if (!db) return;

  change_busy_handler(env, nativeDB, db, NULL);
  if ((*env)->ExceptionCheck(env)) return;

  if (sqlite3_close(db) != SQLITE_OK) {
    throwex(env, nativeDB);
    return;
  }
  sethandle(env, nativeDB, 0);
}

// CONNECTION //////////////////////////////////////////////////////

/* Returns the result code; NativeDB.exec() throws when it is not SQLITE_OK */
EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB__1exec_1utf8(JNIEnv *env, jobject this, jbyteArray sql) {
  sqlite3 *db;
  char *sql_bytes;
  int status;

  db = gethandle(env, this);
  if (!db) {
    throwex_db_closed(env);
    return SQLITE_MISUSE;
  }

  utf8JavaByteArrayToUtf8Bytes(env, sql, &sql_bytes, NULL);
  if (!sql_bytes) return SQLITE_ERROR;

  status = sqlite3_exec(db, sql_bytes, 0, 0, NULL);
  freeUtf8Bytes(sql_bytes);
  return status;
}

EXPORT(void) Java_org_jetbrains_sqlite_NativeDB_interrupt(JNIEnv *env, jobject this) {
  sqlite3 *db = gethandle(env, this);
  if (!db) {
    throwex_db_closed(env);
    return;
  }
  sqlite3_interrupt(db);
}

EXPORT(void) Java_org_jetbrains_sqlite_NativeDB_busy_1timeout(JNIEnv *env, jobject this, jint ms) {
  sqlite3 *db = gethandle(env, this);
  if (!db) {
    throwex_db_closed(env);
    return;
  }
  sqlite3_busy_timeout(db, ms);
}

EXPORT(void) Java_org_jetbrains_sqlite_NativeDB_busy_1handler(JNIEnv *env, jobject this, jobject busyHandler) {
  sqlite3 *db = gethandle(env, this);
  if (!db) {
    throwex_db_closed(env);
    return;
  }
  change_busy_handler(env, this, db, busyHandler);
}

/* Returns the statement; throws (and returns 0) when it cannot be compiled */
EXPORT(jlong) Java_org_jetbrains_sqlite_NativeDB_prepare_1utf8(JNIEnv *env, jobject this, jbyteArray sql) {
  sqlite3 *db;
  sqlite3_stmt *stmt = NULL;
  char *sql_bytes;
  int sql_nbytes;
  int status;

  db = gethandle(env, this);
  if (!db) {
    throwex_db_closed(env);
    return 0;
  }

  utf8JavaByteArrayToUtf8Bytes(env, sql, &sql_bytes, &sql_nbytes);
  if (!sql_bytes) return 0;

  status = sqlite3_prepare_v2(db, sql_bytes, sql_nbytes, &stmt, 0);
  freeUtf8Bytes(sql_bytes);

  if (status != SQLITE_OK) {
    throwex_errorcode(env, this, status);
    return 0;
  }
  return fromref(stmt);
}

EXPORT(jobject) Java_org_jetbrains_sqlite_NativeDB_errmsg_1utf8(JNIEnv *env, jobject this) {
  sqlite3 *db;
  const char *str;

  db = gethandle(env, this);
  if (!db) {
    throwex_db_closed(env);
    return NULL;
  }

  str = (const char *)sqlite3_errmsg(db);
  if (!str) return NULL;
  return utf8BytesToDirectByteBuffer(env, str, (int)strlen(str));
}

EXPORT(jlong) Java_org_jetbrains_sqlite_NativeDB_changes(JNIEnv *env, jobject this) {
  sqlite3 *db = gethandle(env, this);
  if (!db) {
    throwex_db_closed(env);
    return 0;
  }
  return sqlite3_changes64(db);
}

EXPORT(jlong) Java_org_jetbrains_sqlite_NativeDB_total_1changes(JNIEnv *env, jobject this) {
  sqlite3 *db = gethandle(env, this);
  if (!db) {
    throwex_db_closed(env);
    return 0;
  }
  return sqlite3_total_changes64(db);
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_limit(JNIEnv *env, jobject this, jint id, jint value) {
  sqlite3 *db = gethandle(env, this);
  if (!db) {
    throwex_db_closed(env);
    return 0;
  }
  return sqlite3_limit(db, id, value);
}

// STATEMENT ///////////////////////////////////////////////////////

#define CHECK_STMT(ret)            \
  if (!stmt) {                     \
    throwex_stmt_finalized(env);   \
    return ret;                    \
  }

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_finalize(JNIEnv *env, jobject this, jlong stmt) {
  CHECK_STMT(SQLITE_MISUSE)
  return sqlite3_finalize(toref(stmt));
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_step(JNIEnv *env, jobject this, jlong stmt) {
  CHECK_STMT(SQLITE_MISUSE)
  return sqlite3_step(toref(stmt));
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_reset(JNIEnv *env, jobject this, jlong stmt) {
  CHECK_STMT(SQLITE_MISUSE)
  return sqlite3_reset(toref(stmt));
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_clear_1bindings(JNIEnv *env, jobject this, jlong stmt) {
  CHECK_STMT(SQLITE_MISUSE)
  return sqlite3_clear_bindings(toref(stmt));
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_bind_1parameter_1count(JNIEnv *env, jobject this, jlong stmt) {
  CHECK_STMT(SQLITE_MISUSE)
  return sqlite3_bind_parameter_count(toref(stmt));
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_column_1count(JNIEnv *env, jobject this, jlong stmt) {
  CHECK_STMT(SQLITE_MISUSE)
  return sqlite3_column_count(toref(stmt));
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_column_1type(JNIEnv *env, jobject this, jlong stmt, jint col) {
  CHECK_STMT(SQLITE_MISUSE)
  return sqlite3_column_type(toref(stmt), col);
}

EXPORT(jobject) Java_org_jetbrains_sqlite_NativeDB_column_1text_1utf8(JNIEnv *env, jobject this, jlong stmt, jint col) {
  sqlite3 *db;
  const char *bytes;
  int nbytes;

  db = gethandle(env, this);
  if (!db) {
    throwex_db_closed(env);
    return NULL;
  }
  CHECK_STMT(NULL)

  bytes = (const char *)sqlite3_column_text(toref(stmt), col);
  nbytes = sqlite3_column_bytes(toref(stmt), col);
  if (!bytes && sqlite3_errcode(db) == SQLITE_NOMEM) {
    throwex_outofmemory(env);
    return NULL;
  }
  return utf8BytesToDirectByteBuffer(env, bytes, nbytes);
}

EXPORT(jbyteArray) Java_org_jetbrains_sqlite_NativeDB_column_1blob(JNIEnv *env, jobject this, jlong stmt, jint col) {
  sqlite3 *db;
  int type;
  int length;
  jbyteArray jBlob;
  const void *blob;

  db = gethandle(env, this);
  if (!db) {
    throwex_db_closed(env);
    return NULL;
  }
  CHECK_STMT(NULL)

  // sqlite3_column_type() is only meaningful before any type conversion
  type = sqlite3_column_type(toref(stmt), col);
  blob = sqlite3_column_blob(toref(stmt), col);
  if (!blob && sqlite3_errcode(db) == SQLITE_NOMEM) {
    throwex_outofmemory(env);
    return NULL;
  }
  if (!blob) {
    if (type == SQLITE_NULL) return NULL;
    // a zero-length BLOB is a NULL pointer
    jBlob = (*env)->NewByteArray(env, 0);
    if (!jBlob) throwex_outofmemory(env);
    return jBlob;
  }

  length = sqlite3_column_bytes(toref(stmt), col);
  jBlob = (*env)->NewByteArray(env, length);
  if (!jBlob) {
    throwex_outofmemory(env);
    return NULL;
  }
  (*env)->SetByteArrayRegion(env, jBlob, 0, length, (const jbyte *)blob);
  return jBlob;
}

EXPORT(jdouble) Java_org_jetbrains_sqlite_NativeDB_column_1double(JNIEnv *env, jobject this, jlong stmt, jint col) {
  CHECK_STMT(0.0)
  return sqlite3_column_double(toref(stmt), col);
}

EXPORT(jlong) Java_org_jetbrains_sqlite_NativeDB_column_1long(JNIEnv *env, jobject this, jlong stmt, jint col) {
  CHECK_STMT(0)
  return sqlite3_column_int64(toref(stmt), col);
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_column_1int(JNIEnv *env, jobject this, jlong stmt, jint col) {
  CHECK_STMT(0)
  return sqlite3_column_int(toref(stmt), col);
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_bind_1null(JNIEnv *env, jobject this, jlong stmt, jint pos) {
  CHECK_STMT(SQLITE_MISUSE)
  return sqlite3_bind_null(toref(stmt), pos);
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_bind_1int(JNIEnv *env, jobject this, jlong stmt, jint pos, jint v) {
  CHECK_STMT(SQLITE_MISUSE)
  return sqlite3_bind_int(toref(stmt), pos, v);
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_bind_1long(JNIEnv *env, jobject this, jlong stmt, jint pos, jlong v) {
  CHECK_STMT(SQLITE_MISUSE)
  return sqlite3_bind_int64(toref(stmt), pos, v);
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_bind_1double(JNIEnv *env, jobject this, jlong stmt, jint pos, jdouble v) {
  CHECK_STMT(SQLITE_MISUSE)
  return sqlite3_bind_double(toref(stmt), pos, v);
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_bind_1text_1utf8(JNIEnv *env, jobject this, jlong stmt, jint pos, jbyteArray v) {
  int rc;
  char *v_bytes;
  int v_nbytes;

  CHECK_STMT(SQLITE_MISUSE)
  utf8JavaByteArrayToUtf8Bytes(env, v, &v_bytes, &v_nbytes);
  if (!v_bytes) return SQLITE_ERROR;

  rc = sqlite3_bind_text(toref(stmt), pos, v_bytes, v_nbytes, SQLITE_TRANSIENT);
  freeUtf8Bytes(v_bytes);
  return rc;
}

EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_bind_1blob(JNIEnv *env, jobject this, jlong stmt, jint pos, jbyteArray v) {
  jint rc;
  void *a;
  jsize size;

  CHECK_STMT(SQLITE_MISUSE)
  if (!v) return sqlite3_bind_null(toref(stmt), pos);

  size = (*env)->GetArrayLength(env, v);
  a = (*env)->GetPrimitiveArrayCritical(env, v, 0);
  if (!a) {
    throwex_outofmemory(env);
    return SQLITE_NOMEM;
  }
  rc = sqlite3_bind_blob(toref(stmt), pos, a, size, SQLITE_TRANSIENT);
  (*env)->ReleasePrimitiveArrayCritical(env, v, a, JNI_ABORT);
  return rc;
}

/*
 * IntBinder.executeBatch(): data holds queryCount rows of paramCount ints. Each row is bound to the statement as
 * parameters 1..paramCount and run, as LongBinder.executeBatch() and stepInBatch() do in Kotlin: a failed bind or
 * step throws SqliteException, a step that returns a row throws IllegalStateException.
 */
EXPORT(void) Java_org_jetbrains_sqlite_NativeDB_executeBatch(JNIEnv *env, jobject this, jlong stmt, jint queryCount,
                                                             jint paramCount, jintArray data) {
  sqlite3_stmt *s;
  jint *values;
  int batchIndex;
  int index;
  int status;
  char message[64];

  CHECK_STMT()
  if (!data) {
    throwex_msg(env, "executeBatch: no data");
    return;
  }
  if ((*env)->GetArrayLength(env, data) < (jsize)queryCount * paramCount) {
    throwex_msg(env, "executeBatch: data is shorter than queryCount * paramCount");
    return;
  }

  s = (sqlite3_stmt *)toref(stmt);
  values = (*env)->GetIntArrayElements(env, data, NULL);
  if (!values) {
    throwex_outofmemory(env);
    return;
  }

  for (batchIndex = 0; batchIndex < queryCount; batchIndex++) {
    sqlite3_reset(s);
    for (index = 0; index < paramCount; index++) {
      status = sqlite3_bind_int(s, index + 1, values[batchIndex * paramCount + index]) & 0xFF;
      if (status != SQLITE_OK) {
        (*env)->ReleaseIntArrayElements(env, data, values, JNI_ABORT);
        throwex_errorcode(env, this, status);
        return;
      }
    }

    status = sqlite3_step(s);
    if (status != SQLITE_DONE) {
      sqlite3_reset(s);
      (*env)->ReleaseIntArrayElements(env, data, values, JNI_ABORT);
      if (status == SQLITE_ROW) {
        snprintf(message, sizeof(message), "batch entry %d: query returns results", batchIndex);
        (*env)->ThrowNew(env, illegalstateclass, message);
      }
      else {
        throwex_errorcode(env, this, status);
      }
      return;
    }
  }

  (*env)->ReleaseIntArrayElements(env, data, values, JNI_ABORT);
}

// RESTORE /////////////////////////////////////////////////////////

static void updateProgress(JNIEnv *env, sqlite3_backup *pBackup, jobject progress) {
  if (!progress) return;
  (*env)->CallVoidMethod(env, progress, pobserver_mth_progress, sqlite3_backup_remaining(pBackup),
                         sqlite3_backup_pagecount(pBackup));
}

/* Copies the "main" database of file sourceFileName into database dbName of this connection */
EXPORT(jint) Java_org_jetbrains_sqlite_NativeDB_restore(JNIEnv *env, jobject this, jbyteArray zDBName, jbyteArray zFilename,
                                                         jobject observer, jint sleepTimeMillis, jint nTimeoutLimit,
                                                         jint pagesPerStep) {
  int rc;
  sqlite3 *pDb;
  sqlite3 *pFile = NULL;
  sqlite3_backup *pBackup;
  char *dFileName;
  char *dDBName;
  int flags;
  int nTimeout = 0;

  pDb = gethandle(env, this);
  if (!pDb) {
    throwex_db_closed(env);
    return SQLITE_MISUSE;
  }

  utf8JavaByteArrayToUtf8Bytes(env, zFilename, &dFileName, NULL);
  if (!dFileName) return SQLITE_NOMEM;
  utf8JavaByteArrayToUtf8Bytes(env, zDBName, &dDBName, NULL);
  if (!dDBName) {
    freeUtf8Bytes(dFileName);
    return SQLITE_NOMEM;
  }

  flags = SQLITE_OPEN_READONLY;
  if (sqlite3_strnicmp(dFileName, "file:", 5) == 0) flags |= SQLITE_OPEN_URI;
  rc = sqlite3_open_v2(dFileName, &pFile, flags, NULL);

  if (rc == SQLITE_OK) {
    pBackup = sqlite3_backup_init(pDb, dDBName, pFile, "main");
    if (pBackup) {
      do {
        rc = sqlite3_backup_step(pBackup, pagesPerStep);
        if (rc == SQLITE_OK || rc == SQLITE_DONE) {
          updateProgress(env, pBackup, observer);
          if ((*env)->ExceptionCheck(env)) break;
        }
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
          if (nTimeout++ >= nTimeoutLimit) break;
          sqlite3_sleep(sleepTimeMillis);
        }
      } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
      if (rc == SQLITE_DONE) rc = SQLITE_OK;
      if (sqlite3_backup_finish(pBackup) != SQLITE_OK && rc == SQLITE_OK) rc = sqlite3_errcode(pDb);
    }
    else {
      rc = sqlite3_errcode(pDb);
    }
  }

  (void)sqlite3_close(pFile);
  freeUtf8Bytes(dDBName);
  freeUtf8Bytes(dFileName);
  return rc;
}

// LISTENERS ///////////////////////////////////////////////////////

/*
 * NativeDB has no onCommit/onUpdate callbacks (and the platform never enables these listeners), so enabling one is
 * an error instead of a listener that can never be called.
 */
EXPORT(void) Java_org_jetbrains_sqlite_NativeDB_set_1commit_1listener(JNIEnv *env, jobject this, jboolean enabled) {
  (void)this;
  if (enabled) throwex_msg(env, "Commit listeners are not supported");
}

EXPORT(void) Java_org_jetbrains_sqlite_NativeDB_set_1update_1listener(JNIEnv *env, jobject this, jboolean enabled) {
  (void)this;
  if (enabled) throwex_msg(env, "Update listeners are not supported");
}
