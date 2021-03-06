/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class com_intel_oap_common_unsafe_PMemMemoryMapper */

#ifndef _Included_com_intel_oap_common_unsafe_PMemMemoryMapper
#define _Included_com_intel_oap_common_unsafe_PMemMemoryMapper
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     com_intel_oap_common_unsafe_PMemMemoryMapper
 * Method:    pmemMapFile
 * Signature: (Ljava/lang/String;J)J
 */
JNIEXPORT jlong JNICALL Java_com_intel_oap_common_unsafe_PMemMemoryMapper_pmemMapFile
  (JNIEnv *, jclass, jstring, jlong);

/*
 * Class:     com_intel_oap_common_unsafe_PMemMemoryMapper
 * Method:    pmemMemcpy
 * Signature: (J[BJ)V
 */
JNIEXPORT void JNICALL Java_com_intel_oap_common_unsafe_PMemMemoryMapper_pmemMemcpy
  (JNIEnv *, jclass, jlong, jbyteArray, jlong);

/*
 * Class:     com_intel_oap_common_unsafe_PMemMemoryMapper
 * Method:    pmemDrain
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_com_intel_oap_common_unsafe_PMemMemoryMapper_pmemDrain
  (JNIEnv *, jclass);

/*
 * Class:     com_intel_oap_common_unsafe_PMemMemoryMapper
 * Method:    pmemUnmap
 * Signature: (JJ)V
 */
JNIEXPORT void JNICALL Java_com_intel_oap_common_unsafe_PMemMemoryMapper_pmemUnmap
  (JNIEnv *, jclass, jlong, jlong);

#ifdef __cplusplus
}
#endif
#endif
