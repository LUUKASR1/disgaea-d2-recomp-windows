#include "document_probe.h"
#include <jni.h>
extern "C" JNIEXPORT jstring JNICALL
Java_org_disgaead2_bootstrap_BootstrapActivity_inspectDescriptor(JNIEnv *env,jclass,jint fd,jlong base,jlong length) {
    try { return env->NewStringUTF(d2_inspect_document(fd,base,length).c_str()); }
    catch(...) { return env->NewStringUTF("document_status=rejected\ndocument_reason=native-error\ndocument_game_boot=false\n"); }
}
