/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * This represents a "raw" unswapped, unoptimized DEX file.  We don't open
 * them directly, except to create the optimized version that we tuck in
 * the cache area.
 */
#ifndef DALVIK_RAWDEXFILE_H_
#define DALVIK_RAWDEXFILE_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open a raw ".dex" file, optimize it, and load it.
 *
 * On success, returns 0 and sets "*ppDexFile" to a newly-allocated DexFile.
 * On failure, returns a meaningful error code [currently just -1].
 */
int dvmRawDexFileOpen(const char* fileName, DvmDex** ppDDex);

/*
 * Open a raw ".dex" file based on the given chunk of memory, and load
 * it. The bytes are assumed to be owned by the caller for the
 * purposes of memory management and further assumed to not be touched
 * by the caller while the raw dex file remains open. The bytes *may*
 * be modified as the result of issuing this call.
 *
 * On success, returns 0 and sets "*ppDexFile" to a newly-allocated DexFile.
 * On failure, returns a meaningful error code [currently just -1].
 */
int dvmRawDexFileOpenArray(const u1* pBytes, u4 length, DvmDex** ppDDex);

#ifdef __cplusplus
}
#endif

#endif  // DALVIK_RAWDEXFILE_H_