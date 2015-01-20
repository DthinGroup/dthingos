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
 * Array objects.
 */

#include <std_global.h>
#include <dthing.h>
#include <array.h>
#include <heap.h>
#include <class.h>

/* width of an object reference, for arrays of objects */
static size_t kObjectArrayRefWidth = sizeof(Object*);

static ClassObject* createArrayClass(const char* descriptor);

/*
 * Allocate space for a new array object.  This is the lowest-level array
 * allocation function.
 *
 * Pass in the array class and the width of each element.
 *
 * On failure, returns NULL with an exception raised.
 */
static ArrayObject* allocArray(ClassObject* arrayClass, size_t length, size_t elemWidth, int allocFlags)
{
#if 1
    ArrayObject* newArray;
    size_t size;

    //assert(arrayClass->descriptor[0] == '[');

    if (length > 0x0fffffff) {
        /* too large and (length * elemWidth) will overflow 32 bits */
        DVMTraceErr("Rejecting allocation of %u-element array\n", length);
        //dvmThrowBadAllocException("array size too large");
        //====================================
        /* TODO throw exception here? */
        //====================================
        return NULL;
    }

    size = CRTL_offsetof(ArrayObject, contents);
    size += length * elemWidth;

    /* Note that we assume that the Array class does not
     * override finalize().
     */
    newArray = heapAllocObject(size, allocFlags);
    if (newArray != NULL) {
        DVM_OBJECT_INIT(&newArray->obj, arrayClass);
        newArray->length = length;
        DVMTraceInf("AllocArray: %s [%d] (%d)\n",
            arrayClass->descriptor, (int) length, (int) size);
    }
    /* the caller must call dvmReleaseTrackedAlloc */
    return newArray;
#else
    return NULL;
#endif
}

/*
 * Create a new array, given an array class.  The class may represent an
 * array of references or primitives.
 */
ArrayObject* dvmAllocArrayByClass(ClassObject* arrayClass, size_t length, int allocFlags)
{
    const char* descriptor = arrayClass->descriptor;

    //assert(descriptor[0] == '[');       /* must be array class */
    if (descriptor[1] != '[' && descriptor[1] != 'L') {
        /* primitive array */
        //assert(descriptor[2] == '\0');
        return dvmAllocPrimitiveArray(descriptor[1], length, allocFlags);
    } else {
        return allocArray(arrayClass, length, kObjectArrayRefWidth, allocFlags);
    }
}

/*
 * Find the array class for "elemClassObj", which could itself be an
 * array class.
 */
ClassObject* dvmFindArrayClassForElement(ClassObject* elemClassObj)
{   
    /**
     * This definition should be enough for descriptor size.
     */
    #define MAX_DESCRIPTOR_SIZE (256)
    ClassObject* arrayClass;

    //assert(elemClassObj != NULL);

    /* Simply prepend "[" to the descriptor. */
    int nameLen = (int)CRTL_strlen(elemClassObj->descriptor);
    char className[MAX_DESCRIPTOR_SIZE] = {0x0, };

    className[0] = '[';
    CRTL_memcpy(className+1, elemClassObj->descriptor, nameLen+1);
    arrayClass = dvmFindArrayClass(className);

    return arrayClass;
}

/*
 * Create a new array that holds primitive types.
 *
 * "type" is the primitive type letter, e.g. 'I' for int or 'J' for long.
 */
ArrayObject* dvmAllocPrimitiveArray(char type, size_t length, int allocFlags)
{
    ArrayObject* newArray;
    ClassObject* arrayClass;
    int width;

    switch (type) {
    case 'I':
        arrayClass = gDvm.classArrayInt;
        width = 4;
        break;
    case 'C':
        arrayClass = gDvm.classArrayChar;
        width = 2;
        break;
    case 'B':
        arrayClass = gDvm.classArrayByte;
        width = 1;
        break;
    case 'Z':
        arrayClass = gDvm.classArrayBoolean;
        width = 1; /* special-case this? */
        break;
    case 'F':
        arrayClass = gDvm.classArrayFloat;
        width = 4;
        break;
    case 'D':
        arrayClass = gDvm.classArrayDouble;
        width = 8;
        break;
    case 'S':
        arrayClass = gDvm.classArrayShort;
        width = 2;
        break;
    case 'J':
        arrayClass = gDvm.classArrayLong;
        width = 8;
        break;
    default:
        DVMTraceErr("Unknown primitive type '%c'", type);
        //dvmAbort();
        return NULL; // Keeps the compiler happy.
    }

    newArray = allocArray(arrayClass, length, width, allocFlags);

    /* the caller must dvmReleaseTrackedAlloc if allocFlags==ALLOC_DEFAULT */
    return newArray;
}

/*
 * Recursively create an array with multiple dimensions.  Elements may be
 * Objects or primitive types.
 *
 * The dimension we're creating is in dimensions[0], so when we recurse
 * we advance the pointer.
 */
ArrayObject* dvmAllocMultiArray(ClassObject* arrayClass, int curDim,
    const int* dimensions)
{
#if 1
    ArrayObject* newArray;
    const char* elemName = arrayClass->descriptor + 1; // Advance past one '['.

    DVMTraceInf("dvmAllocMultiArray: class='%s' curDim=%d *dimensions=%d",
        arrayClass->descriptor, curDim, *dimensions);

    if (curDim == 0) {
        if (*elemName == 'L' || *elemName == '[') {
            DVMTraceInf("  end: array class (obj) is '%s'",
                arrayClass->descriptor);
            newArray = allocArray(arrayClass, *dimensions,
                        kObjectArrayRefWidth, 0);
        } else {
            DVMTraceInf("  end: array class (prim) is '%s'",
                arrayClass->descriptor);
            newArray = dvmAllocPrimitiveArray(
                    dexGetPrimitiveTypeDescriptorChar(arrayClass->elementClass->primitiveType),
                    *dimensions, 0);
        }
    }
    else
    {
        ClassObject* subArrayClass;
        int i;

        /* if we have X[][], find X[] */
        subArrayClass = dvmFindArrayClass(elemName);
        if (subArrayClass == NULL) {
            /* not enough '['s on the initial class? */
            //assert(dvmCheckException(dvmThreadSelf()));
            //====================================
            /* TODO: throw exception here? */
            //====================================
            return NULL;
        }
        //assert(dvmIsArrayClass(subArrayClass));

        /* allocate the array that holds the sub-arrays */
        newArray = allocArray(arrayClass, *dimensions, kObjectArrayRefWidth, 0);
        if (newArray == NULL) {
            //assert(dvmCheckException(dvmThreadSelf()));
            //====================================
            /* TODO: throw exception here? */
            //====================================
            return NULL;
        }

        /*
         * Create a new sub-array in every element of the array.
         */
        for (i = 0; i < *dimensions; i++)
        {
            ArrayObject* newSubArray;
            newSubArray = dvmAllocMultiArray(subArrayClass, curDim-1,
                          dimensions+1);
            if (newSubArray == NULL)
            {
                //dvmReleaseTrackedAlloc((Object*) newArray, NULL);
                //assert(dvmCheckException(dvmThreadSelf()));
                //====================================
                /* TODO: throw exception here? */
                //====================================
                return NULL;
            }
            dvmSetObjectArrayElement(newArray, i, (Object *)newSubArray);
            //dvmReleaseTrackedAlloc((Object*) newSubArray, NULL);
        }
    }

    /* caller must call dvmReleaseTrackedAlloc */
    return newArray;
#else
    return NULL;
#endif
}


/*
 * Find an array class, by name (e.g. "[I").
 *
 * If the array class doesn't exist, we generate it.
 *
 * If the element class doesn't exist, we return NULL (no exception raised).
 */
ClassObject* dvmFindArrayClass(const char* descriptor)
{
#if 1
    ClassObject* clazz;

    //assert(descriptor[0] == '[');
    DVMTraceInf("dvmFindArrayClass: '%s'", descriptor);

    clazz = dvmLookupClass(descriptor);
    if (clazz == NULL) {
        DVMTraceInf("Array class '%s' not found; creating", descriptor);
        clazz = createArrayClass(descriptor);
        if (clazz == NULL)
        {
            DVMTraceErr("dvmFindArrayClass - error: create Array class (%s) failure", descriptor);
        }
    }

    return clazz;
#else
    return NULL;
#endif
}

/*
 * Create an array class (i.e. the class object for the array, not the
 * array itself).  "descriptor" looks like "[C" or "[Ljava/lang/String;".
 *
 * If "descriptor" refers to an array of primitives, look up the
 * primitive type's internally-generated class object.
 *
 * "loader" is the class loader of the class that's referring to us.  It's
 * used to ensure that we're looking for the element type in the right
 * context.  It does NOT become the class loader for the array class; that
 * always comes from the base element class.
 *
 * Returns NULL with an exception raised on failure.
 */
static ClassObject* createArrayClass(const char* descriptor)
{
#if 1
    ClassObject* newClass = NULL;
    ClassObject* elementClass = NULL;
    int arrayDim;
    u4 extraFlags;

    //assert(descriptor[0] == '[');
    //assert(gDvm.classJavaLangClass != NULL);
    //assert(gDvm.classJavaLangObject != NULL);

    /*
     * Identify the underlying element class and the array dimension depth.
     */
    extraFlags = CLASS_ISARRAY;
    if (descriptor[1] == '[') {
        /* array of arrays; keep descriptor and grab stuff from parent */
        ClassObject* outer;

        outer = dvmFindClassNoInit(&descriptor[1]);
        if (outer != NULL) {
            /* want the base class, not "outer", in our elementClass */
            elementClass = outer->elementClass;
            arrayDim = outer->arrayDim + 1;
            extraFlags |= CLASS_ISOBJECTARRAY;
        } else {
            //assert(elementClass == NULL);     /* make sure we fail */
        }
    } else {
        arrayDim = 1;
        if (descriptor[1] == 'L') {
            /* array of objects; strip off "[" and look up descriptor. */
            const char* subDescriptor = &descriptor[1];
            DVMTraceInf("searching for element class '%s'", subDescriptor);
            elementClass = dvmFindClassNoInit(subDescriptor);
            extraFlags |= CLASS_ISOBJECTARRAY;
        } else {
            /* array of a primitive type */
            elementClass = dvmFindPrimitiveClass(descriptor[1]);
        }
    }

    if (elementClass == NULL) {
        /* failed */
        //assert(dvmCheckException(dvmThreadSelf()));
        //dvmFreeClassInnards(newClass);
        //dvmReleaseTrackedAlloc((Object*) newClass, NULL);
        /*=================================================
        //what should we do when find element class failure?
        =================================================*/
        return NULL;
    }

    /*
     * See if it's already loaded.  Array classes are always associated
     * with the class loader of their underlying element type -- an array
     * of Strings goes with the loader for java/lang/String -- so we need
     * to look for it there.  (The caller should have checked for the
     * existence of the class before calling here, but they did so with
     * *their* class loader, not the element class' loader.)
     *
     * If we find it, the caller adds "loader" to the class' initiating
     * loader list, which should prevent us from going through this again.
     *
     * This call is unnecessary if "loader" and "elementClass->classLoader"
     * are the same, because our caller (dvmFindArrayClass) just did the
     * lookup.  (Even if we get this wrong we still have correct behavior,
     * because we effectively do this lookup again when we add the new
     * class to the hash table -- necessary because of possible races with
     * other threads.)
     */
    newClass = dvmLookupClass(descriptor);
    if (newClass != NULL) {
        DVMTraceInf("--- we already have %s", descriptor);
        return newClass;
    }


    /*
     * Fill out the fields in the ClassObject.
     *
     * It is possible to execute some methods against arrays, because all
     * arrays are instances of Object, so we need to set up a vtable.  We
     * can just point at the one in Object.
     *
     * Array classes are simple enough that we don't need to do a full
     * link step.
     */
    newClass = (ClassObject*) heapAllocPersistent(sizeof(*newClass));
    if (newClass == NULL)
        return NULL;
    DVM_OBJECT_INIT(&newClass->obj, gDvm.classJavaLangClass);
    //dvmSetClassSerialNumber(newClass);
    newClass->descriptorAlloc = (char *)heapDupStr(descriptor);
    newClass->descriptor = newClass->descriptorAlloc;
    dvmSetFieldObject((Object *)newClass,
                      CRTL_offsetof(ClassObject, super),
                      (Object *)gDvm.classJavaLangObject);
    newClass->vtableCount = gDvm.classJavaLangObject->vtableCount;
    newClass->vtable = gDvm.classJavaLangObject->vtable;
    newClass->primitiveType = PRIM_NOT;
    dvmSetFieldObject((Object *)newClass,
                      CRTL_offsetof(ClassObject, elementClass),
                      (Object *)elementClass);
    //We don't need classloader here.
    //dvmSetFieldObject((Object *)newClass,
    //                  CRTL_offsetof(ClassObject, classLoader),
    //                  (Object *)elementClass->classLoader);
    newClass->arrayDim = arrayDim;
    newClass->status = CLASS_INITIALIZED;

    /* don't need to set newClass->objectSize */

    /*
     * All arrays have java/lang/Cloneable and java/io/Serializable as
     * interfaces.  We need to set that up here, so that stuff like
     * "instanceof" works right.
     *
     * Note: The GC could run during the call to dvmFindSystemClassNoInit(),
     * so we need to make sure the class object is GC-valid while we're in
     * there.  Do this by clearing the interface list so the GC will just
     * think that the entries are null.
     *
     * TODO?
     * We may want to cache these two classes to avoid the lookup, though
     * it's not vital -- we only do it when creating an array class, not
     * every time we create an array.  Better yet, create a single, global
     * copy of "interfaces" and "iftable" somewhere near the start and
     * just point to those (and remember not to free them for arrays).
     */
#if 0
    //TBC: as spec of M2M, we don't really need these interfaces.
    newClass->interfaceCount = 2;
    newClass->interfaces = (ClassObject**)heapAllocPersistent(sizeof(ClassObject*) * 2);
    CRTL_memset(newClass->interfaces, 0, sizeof(ClassObject*) * 2);
    newClass->interfaces[0] =
        dvmFindSystemClassNoInit("Ljava/lang/Cloneable;");
    newClass->interfaces[1] =
        dvmFindSystemClassNoInit("Ljava/io/Serializable;");


    if (newClass->interfaces[0] == NULL || newClass->interfaces[1] == NULL) {
        LOGE("Unable to create array class '%s': missing interfaces",
            descriptor);
        dvmFreeClassInnards(newClass);
        dvmThrowInternalError("missing array ifaces");
        dvmReleaseTrackedAlloc((Object*) newClass, NULL);
        return NULL;
    }
    /*
     * We assume that Cloneable/Serializable don't have superinterfaces --
     * normally we'd have to crawl up and explicitly list all of the
     * supers as well.  These interfaces don't have any methods, so we
     * don't have to worry about the ifviPool either.
     */
    newClass->iftableCount = 2;
    newClass->iftable = (InterfaceEntry*) dvmLinearAlloc(newClass->classLoader,
                                sizeof(InterfaceEntry) * 2);
    memset(newClass->iftable, 0, sizeof(InterfaceEntry) * 2);
    newClass->iftable[0].clazz = newClass->interfaces[0];
    newClass->iftable[1].clazz = newClass->interfaces[1];
    dvmLinearReadOnly(newClass->classLoader, newClass->iftable);
#endif
    /*
     * Inherit access flags from the element.  Arrays can't be used as a
     * superclass or interface, so we want to add "final" and remove
     * "interface".
     *
     * Don't inherit any non-standard flags (e.g., CLASS_FINALIZABLE)
     * from elementClass.  We assume that the array class does not
     * override finalize().
     */
    newClass->accessFlags = ((newClass->elementClass->accessFlags &
                             ~ACC_INTERFACE) | ACC_FINAL) & JAVA_FLAGS_MASK;

    /* Set the flags we determined above.
     * This must happen after accessFlags is set.
     */
    SET_CLASS_FLAG(newClass, extraFlags);

    if (!dvmAddClassToHash(newClass)) {
        /*
         * Another thread must have loaded the class after we
         * started but before we finished.  Discard what we've
         * done and leave some hints for the GC.
         *
         * (Yes, this happens.)
         */

        /* Clean up the class before letting the
         * GC get its hands on it.
         */
        //dvmFreeClassInnards(newClass);

        /* Let the GC free the class.
         */
        //dvmReleaseTrackedAlloc((Object*) newClass, NULL);

        /* Grab the winning class.
         */
        newClass = dvmLookupClass(descriptor);
        //assert(newClass != NULL);
        return newClass;
    }
    //dvmReleaseTrackedAlloc((Object*) newClass, NULL);

    //LOGV("Created array class '%s' %p (access=0x%04x.%04x)",
    //    descriptor, newClass->classLoader,
    //    newClass->accessFlags >> 16,
    //    newClass->accessFlags & JAVA_FLAGS_MASK);

    return newClass;
#else
    return NULL;
#endif
}

/*
 * Copy the entire contents of one array of objects to another.  If the copy
 * is impossible because of a type clash, we fail and return "false".
 */
bool_t dvmCopyObjectArray(ArrayObject* dstArray, const ArrayObject* srcArray,
    ClassObject* dstElemClass)
{
#if 0
    Object** src = (Object**)(void*)srcArray->contents;
    u4 length, count;

    assert(srcArray->length == dstArray->length);
    assert(dstArray->clazz->elementClass == dstElemClass ||
        (dstArray->clazz->elementClass == dstElemClass->elementClass &&
         dstArray->clazz->arrayDim == dstElemClass->arrayDim+1));

    length = dstArray->length;
    for (count = 0; count < length; count++) {
        if (!dvmInstanceof(src[count]->clazz, dstElemClass)) {
            LOGW("dvmCopyObjectArray: can't store %s in %s",
                src[count]->clazz->descriptor, dstElemClass->descriptor);
            return false;
        }
        dvmSetObjectArrayElement(dstArray, count, src[count]);
    }

    return true;
#else
    return FALSE;
#endif
}

/*
 * Copy the entire contents of an array of boxed primitives into an
 * array of primitives.  The boxed value must fit in the primitive (i.e.
 * narrowing conversions are not allowed).
 */
bool_t dvmUnboxObjectArray(ArrayObject* dstArray, const ArrayObject* srcArray, ClassObject* dstElemClass)
{
#if 0
    Object** src = (Object**)(void*)srcArray->contents;
    void* dst = (void*)dstArray->contents;
    u4 count = dstArray->length;
    PrimitiveType typeIndex = dstElemClass->primitiveType;

    assert(typeIndex != PRIM_NOT);
    assert(srcArray->length == dstArray->length);

    while (count--) {
        JValue result;

        /*
         * This will perform widening conversions as appropriate.  It
         * might make sense to be more restrictive and require that the
         * primitive type exactly matches the box class, but it's not
         * necessary for correctness.
         */
        if (!dvmUnboxPrimitive(*src, dstElemClass, &result)) {
            LOGW("dvmCopyObjectArray: can't store %s in %s",
                (*src)->clazz->descriptor, dstElemClass->descriptor);
            return false;
        }

        /* would be faster with 4 loops, but speed not crucial here */
        switch (typeIndex) {
        case PRIM_BOOLEAN:
        case PRIM_BYTE:
            {
                u1* tmp = (u1*)dst;
                *tmp++ = result.b;
                dst = tmp;
            }
            break;
        case PRIM_CHAR:
        case PRIM_SHORT:
            {
                u2* tmp = (u2*)dst;
                *tmp++ = result.s;
                dst = tmp;
            }
            break;
        case PRIM_FLOAT:
        case PRIM_INT:
            {
                u4* tmp = (u4*)dst;
                *tmp++ = result.i;
                dst = tmp;
            }
            break;
        case PRIM_DOUBLE:
        case PRIM_LONG:
            {
                u8* tmp = (u8*)dst;
                *tmp++ = result.j;
                dst = tmp;
            }
            break;
        default:
            /* should not be possible to get here */
            dvmAbort();
        }

        src++;
    }

    return true;
#else
    return FALSE;
#endif
}

/*
 * Returns the width, in bytes, required by elements in instances of
 * the array class.
 */
size_t dvmArrayClassElementWidth(const ClassObject* arrayClass)
{
#if 1
    const char *descriptor;

    //assert(dvmIsArrayClass(arrayClass));

    if (dvmIsObjectArrayClass(arrayClass)) {
        return sizeof(Object *);
    } else {
        descriptor = arrayClass->descriptor;
        switch (descriptor[1]) {
        case 'B': return 1;  /* byte */
        case 'C': return 2;  /* char */
        case 'D': return 8;  /* double */
        case 'F': return 4;  /* float */
        case 'I': return 4;  /* int */
        case 'J': return 8;  /* long */
        case 'S': return 2;  /* short */
        case 'Z': return 1;  /* boolean */
        }
    }
    DVMTraceErr("class %p has an unhandled descriptor '%s'", arrayClass, descriptor);
    //dvmDumpThread(dvmThreadSelf(), false);
    //dvmAbort();
    return 0;  /* Quiet the compiler. */
#else
    return 0;
#endif
}

size_t dvmArrayObjectSize(const ArrayObject *array)
{
    //assert(array != NULL);
    size_t size = CRTL_offsetof(ArrayObject, contents);
    size += array->length * dvmArrayClassElementWidth(array->obj.clazz);
    return size;
}

bool_t dvmIsArrayClass(const ClassObject* clazz)
{
    return (clazz->descriptor[0] == '[');
}

bool_t dvmIsObjectArray(const ArrayObject* arrayObj)
{
    return dvmIsObjectArrayClass(arrayObj->obj.clazz);
}

/*
 * Verify that the array is an object array and not a primitive array.
 *
 * Does not verify that the object is actually a non-NULL object.
 */
bool_t dvmIsObjectArrayClass(const ClassObject* clazz)
{
    const char* descriptor = clazz->descriptor;
    return descriptor[0] == '[' && (descriptor[1] == 'L' ||
                                    descriptor[1] == '[');
}

/*
 * Verify that the object is actually an array.
 *
 * Does not verify that the object is actually a non-NULL object.
 */
bool_t dvmIsArray(const ArrayObject* arrayObj)
{
    return ( ((Object*)arrayObj)->clazz->descriptor[0] == '[' );
}
