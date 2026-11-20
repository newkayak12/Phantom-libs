/* Machine-dependent JNI types for Windows (32- and 64-bit), for cross-compiling NativeDB.c with zig/mingw. */
#ifndef PHANTOM_JNI_MD_H
#define PHANTOM_JNI_MD_H

#define JNIEXPORT __declspec(dllexport)
#define JNIIMPORT __declspec(dllimport)
#define JNICALL __stdcall

typedef long jint;
typedef long long jlong;
typedef signed char jbyte;

#endif
