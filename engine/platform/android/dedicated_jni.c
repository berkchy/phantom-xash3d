#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include "common.h"

static JavaVM *g_vm = NULL;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
	g_vm = vm;
	return JNI_VERSION_1_6;
}

JNIEXPORT jint JNICALL Java_su_xash_engine_DedicatedServerService_nativeInitConsole
  (JNIEnv *env, jobject thiz)
{
	int p[2];
	if (pipe(p) < 0)
		return -1;

	dup2(p[1], STDOUT_FILENO);
	dup2(p[1], STDERR_FILENO);
	close(p[1]);

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	return p[0];
}

JNIEXPORT void JNICALL Java_su_xash_engine_DedicatedServerService_nativeStartServer
  (JNIEnv *env, jobject thiz, jobjectArray jargs, jstring jgamedir, jstring jbasedir, jstring jrodir, jstring jcrashdir)
{
	int argc = (*env)->GetArrayLength(env, jargs);
	char **argv = (char **)calloc(argc + 1, sizeof(char *));
	int i;

	for (i = 0; i < argc; i++)
	{
		jstring js = (jstring)(*env)->GetObjectArrayElement(env, jargs, i);
		const char *str = (*env)->GetStringUTFChars(env, js, NULL);
		argv[i] = strdup(str);
		(*env)->ReleaseStringUTFChars(env, js, str);
	}
	argv[argc] = NULL;

	const char *gamedir = (*env)->GetStringUTFChars(env, jgamedir, NULL);
	const char *basedir = (*env)->GetStringUTFChars(env, jbasedir, NULL);
	const char *rodir = (*env)->GetStringUTFChars(env, jrodir, NULL);
	const char *crashdir = (*env)->GetStringUTFChars(env, jcrashdir, NULL);

	setenv("XASH3D_GAME", gamedir, 1);
	setenv("XASH3D_BASEDIR", basedir, 1);
	setenv("XASH3D_RODIR", rodir, 1);
	setenv("XASH3D_CRASH_DIR", crashdir, 1);

	Host_Main(argc, argv, gamedir, 0, NULL);

	for (i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
	(*env)->ReleaseStringUTFChars(env, jgamedir, gamedir);
	(*env)->ReleaseStringUTFChars(env, jbasedir, basedir);
	(*env)->ReleaseStringUTFChars(env, jrodir, rodir);
	(*env)->ReleaseStringUTFChars(env, jcrashdir, crashdir);
}

JNIEXPORT void JNICALL Java_su_xash_engine_DedicatedServerService_nativeStopServer
  (JNIEnv *env, jobject thiz)
{
	kill(getpid(), SIGTERM);
}
