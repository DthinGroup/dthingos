/* DO NOT EDIT THIS FILE - it is machine generated */
#include <dthing.h>
#include <kni.h>

/* Header for class java.lang.DThread */

#ifndef __NATIVE_DTHREAD_H__
#define __NATIVE_DTHREAD_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Class:     java_lang_DThread
 * Method:    start0
 * Signature: ()V
 */
void Java_java_lang_Thread_start(const u4* args, JValue* pResult);

/**
 * Class:     java_lang_DThread
 * Method:    sleep0
 * Signature: (J)V
 */
void Java_java_lang_Thread_sleep(const u4* args, JValue* pResult);

/**
 * Class:     java_lang_DThread
 * Method:    activeCount0
 * Signature: ()I
 */
void Java_java_lang_Thread_activeCount(const u4* args, JValue* pResult);

/**
 * Class:     java_lang_DThread
 * Method:    currentThread0
 * Signature: ()Ljava/lang/DThread;
 */
void Java_java_lang_Thread_currentThread(const u4* args, JValue* pResult);

/**
 * Class:     java_lang_DThread
 * Method:    isAlive0
 * Signature: ()Z
 */
void Java_java_lang_Thread_isAlive(const u4* args, JValue* pResult);

#ifdef __cplusplus
}
#endif
#endif // __NATIVE_DTHREAD_H__
