/*
 * This file was generated automatically by gen-mterp.py for 'portable'.
 *
 * --> DO NOT EDIT <--
 */

/* File: c/header.cpp */
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

/* common includes */
//#include "Dalvik.h"
//#include "interp/InterpDefs.h"
//#include "mterp/Mterp.h"

#include <math.h>                   // needed for fmod, fmodf
#include <stdarg.h>
//#include "mterp/common/FindInterface.h"
#include "vm_common.h"
#include "interpStack.h"
#include "interpOpcode.h"
#include "exception.h"
#include "interpApi.h"
#include "schd.h"
#include "kni.h"
#include "Object.h"
#include "Resolve.h"
/*
 * Configuration defines.  These affect the C implementations, i.e. the
 * portable interpreter(s) and C stubs.
 *
 * Some defines are controlled by the Makefile, e.g.:
 *   WITH_INSTR_CHECKS
 *   WITH_TRACKREF_CHECKS
 *   EASY_GDB
 *   NDEBUG
 */

#ifdef WITH_INSTR_CHECKS            /* instruction-level paranoia (slow!) */
# define CHECK_BRANCH_OFFSETS
# define CHECK_REGISTER_INDICES
#endif

/*
 * Some architectures require 64-bit alignment for access to 64-bit data
 * types.  We can't just use pointers to copy 64-bit values out of our
 * interpreted register set, because gcc may assume the pointer target is
 * aligned and generate invalid code.
 *
 * There are two common approaches:
 *  (1) Use a union that defines a 32-bit pair and a 64-bit value.
 *  (2) Call memcpy().
 *
 * Depending upon what compiler you're using and what options are specified,
 * one may be faster than the other.  For example, the compiler might
 * convert a memcpy() of 8 bytes into a series of instructions and omit
 * the call.  The union version could cause some strange side-effects,
 * e.g. for a while ARM gcc thought it needed separate storage for each
 * inlined instance, and generated instructions to zero out ~700 bytes of
 * stack space at the top of the interpreter.
 *
 * The default is to use memcpy().  The current gcc for ARM seems to do
 * better with the union.
 */
#if defined(__ARM_EABI__)
# define NO_UNALIGN_64__UNION
#endif


//#define LOG_INSTR                   /* verbose debugging */
/* set and adjust ANDROID_LOG_TAGS='*:i jdwp:i dalvikvm:i dalvikvmi:i' */

/*
 * Export another copy of the PC on every instruction; this is largely
 * redundant with EXPORT_PC and the debugger code.  This value can be
 * compared against what we have stored on the stack with EXPORT_PC to
 * help ensure that we aren't missing any export calls.
 */
#if WITH_EXTRA_GC_CHECKS > 1
# define EXPORT_EXTRA_PC() (self->currentPc2 = pc)
#else
# define EXPORT_EXTRA_PC()
#endif

/*
 * Adjust the program counter.  "_offset" is a signed int, in 16-bit units.
 *
 * Assumes the existence of "const u2* pc" and "const u2* curMethod->insns".
 *
 * We don't advance the program counter until we finish an instruction or
 * branch, because we do want to have to unroll the PC if there's an
 * exception.
 */
#ifdef CHECK_BRANCH_OFFSETS
# define ADJUST_PC(_offset) do {                                            \
        int myoff = _offset;        /* deref only once */                   \
        if (pc + myoff < curMethod->insns ||                                \
            pc + myoff >= curMethod->insns + dvmGetMethodInsnsSize(curMethod)) \
        {                                                                   \
            char* desc;                                                     \
            desc = dexProtoCopyMethodDescriptor(&curMethod->prototype);     \
            LOGE("Invalid branch %d at 0x%04x in %s.%s %s",                 \
                myoff, (int) (pc - curMethod->insns),                       \
                curMethod->clazz->descriptor, curMethod->name, desc);       \
            free(desc);                                                     \
            dvmAbort();                                                     \
        }                                                                   \
        pc += myoff;                                                        \
        EXPORT_EXTRA_PC();                                                  \
    } while (false)
#else
# define ADJUST_PC(_offset) do {                                            \
        pc += _offset;                                                      \
        EXPORT_EXTRA_PC();                                                  \
    } while (false)
#endif

/*
 * If enabled, log instructions as we execute them.
 */
#ifdef LOG_INSTR
# define ILOGD(...) ILOG(LOG_DEBUG, __VA_ARGS__)
# define ILOGV(...) ILOG(LOG_VERBOSE, __VA_ARGS__)
# define ILOG(_level, ...) do {                                             \
        char debugStrBuf[128];                                              \
        snprintf(debugStrBuf, sizeof(debugStrBuf), __VA_ARGS__);            \
        if (curMethod != NULL)                                              \
            LOG(_level, LOG_TAG"i", "%-2d|%04x%s",                          \
                self->threadId, (int)(pc - curMethod->insns), debugStrBuf); \
        else                                                                \
            LOG(_level, LOG_TAG"i", "%-2d|####%s",                          \
                self->threadId, debugStrBuf);                               \
    } while(false)
void dvmDumpRegs(const Method* method, const u4* framePtr, bool inOnly);
# define DUMP_REGS(_meth, _frame, _inOnly) dvmDumpRegs(_meth, _frame, _inOnly)
static const char kSpacing[] = "            ";
#else
# define ILOGD ((void)0)
# define ILOGV ((void)0)
# define DUMP_REGS(_meth, _frame, _inOnly) ((void)0)
#endif

/* get a long from an array of u4 */
static /*inline*/ s8 getLongFromArray(const u4* ptr, int idx)
{
#if defined(NO_UNALIGN_64__UNION)
    union { s8 ll; u4 parts[2]; } conv;

    ptr += idx;
    conv.parts[0] = ptr[0];
    conv.parts[1] = ptr[1];
    return conv.ll;
#else
    s8 val;
    memcpy(&val, &ptr[idx], 8);
    return val;
#endif
}

/* store a long into an array of u4 */
static /*inline*/ void putLongToArray(u4* ptr, int idx, s8 val)
{
#if defined(NO_UNALIGN_64__UNION)
    union { s8 ll; u4 parts[2]; } conv;

    ptr += idx;
    conv.ll = val;
    ptr[0] = conv.parts[0];
    ptr[1] = conv.parts[1];
#else
    memcpy(&ptr[idx], &val, 8);
#endif
}

/* get a double from an array of u4 */
static /*inline*/ double getDoubleFromArray(const u4* ptr, int idx)
{
#if defined(NO_UNALIGN_64__UNION)
    union { double d; u4 parts[2]; } conv;

    ptr += idx;
    conv.parts[0] = ptr[0];
    conv.parts[1] = ptr[1];
    return conv.d;
#else
    double dval;
    memcpy(&dval, &ptr[idx], 8);
    return dval;
#endif
}

/* store a double into an array of u4 */
static /*inline*/ void putDoubleToArray(u4* ptr, int idx, double dval)
{
#if defined(NO_UNALIGN_64__UNION)
    union { double d; u4 parts[2]; } conv;

    ptr += idx;
    conv.d = dval;
    ptr[0] = conv.parts[0];
    ptr[1] = conv.parts[1];
#else
    memcpy(&ptr[idx], &dval, 8);
#endif
}

/*
 * If enabled, validate the register number on every access.  Otherwise,
 * just do an array access.
 *
 * Assumes the existence of "u4* fp".
 *
 * "_idx" may be referenced more than once.
 */
#ifdef CHECK_REGISTER_INDICES
# define GET_REGISTER(_idx) \
    ( (_idx) < curMethod->registersSize ? \
        (fp[(_idx)]) : (assert(!"bad reg"),1969) )
# define SET_REGISTER(_idx, _val) \
    ( (_idx) < curMethod->registersSize ? \
        (fp[(_idx)] = (u4)(_val)) : (assert(!"bad reg"),1969) )
# define GET_REGISTER_AS_OBJECT(_idx)       ((Object *)GET_REGISTER(_idx))
# define SET_REGISTER_AS_OBJECT(_idx, _val) SET_REGISTER(_idx, (s4)_val)
# define GET_REGISTER_INT(_idx) ((s4) GET_REGISTER(_idx))
# define SET_REGISTER_INT(_idx, _val) SET_REGISTER(_idx, (s4)_val)
# define GET_REGISTER_WIDE(_idx) \
    ( (_idx) < curMethod->registersSize-1 ? \
        getLongFromArray(fp, (_idx)) : (assert(!"bad reg"),1969) )
# define SET_REGISTER_WIDE(_idx, _val) \
    ( (_idx) < curMethod->registersSize-1 ? \
        (void)putLongToArray(fp, (_idx), (_val)) : assert(!"bad reg") )
# define GET_REGISTER_FLOAT(_idx) \
    ( (_idx) < curMethod->registersSize ? \
        (*((float*) &fp[(_idx)])) : (assert(!"bad reg"),1969.0f) )
# define SET_REGISTER_FLOAT(_idx, _val) \
    ( (_idx) < curMethod->registersSize ? \
        (*((float*) &fp[(_idx)]) = (_val)) : (assert(!"bad reg"),1969.0f) )
# define GET_REGISTER_DOUBLE(_idx) \
    ( (_idx) < curMethod->registersSize-1 ? \
        getDoubleFromArray(fp, (_idx)) : (assert(!"bad reg"),1969.0) )
# define SET_REGISTER_DOUBLE(_idx, _val) \
    ( (_idx) < curMethod->registersSize-1 ? \
        (void)putDoubleToArray(fp, (_idx), (_val)) : assert(!"bad reg") )
#else
# define GET_REGISTER(_idx)                 (fp[(_idx)])
#if 0
# define SET_REGISTER(_idx, _val)           (fp[(_idx)] = (_val))
#else
# define SET_REGISTER(_idx, _val) \
	do{\
	    u4 vall = _val;\
		fp[(_idx)] = (vall); \
		MACRO_LOG("Thread id:%d,set reg idx:%d,val:%d\n",self->threadId,_idx,vall); \
	}while(0)
#endif
# define GET_REGISTER_AS_OBJECT(_idx)       ((Object*) fp[(_idx)])
#if 0
# define SET_REGISTER_AS_OBJECT(_idx, _val) (fp[(_idx)] = (u4)(_val))
#else
# define SET_REGISTER_AS_OBJECT(_idx, _val)\
	do{\
		(fp[(_idx)] = (u4)(_val));	\
		MACRO_LOG("Thread id:%d,set reg as obj idx:%d,val:%d\n",self->threadId,_idx,_val); \
	}while(0)
#endif
# define GET_REGISTER_INT(_idx)             ((s4)GET_REGISTER(_idx))
# define SET_REGISTER_INT(_idx, _val)       SET_REGISTER(_idx, (s4)_val)
# define GET_REGISTER_WIDE(_idx)            getLongFromArray(fp, (_idx))
# define SET_REGISTER_WIDE(_idx, _val)      putLongToArray(fp, (_idx), (_val))
# define GET_REGISTER_FLOAT(_idx)           (*((float*) &fp[(_idx)]))
# define SET_REGISTER_FLOAT(_idx, _val)     (*((float*) &fp[(_idx)]) = (_val))
# define GET_REGISTER_DOUBLE(_idx)          getDoubleFromArray(fp, (_idx))
# define SET_REGISTER_DOUBLE(_idx, _val)    putDoubleToArray(fp, (_idx), (_val))
#endif

#define R_NONE 

/*use to identify data tyep and length*/
//#ifdef ARCH_X86
#define ULL 
#define UL
#define LL
#define LOGV	DVMTraceInf
#define LOGE	DVMTraceInf

#define MACRO_LOG	  DVMTraceInf
#define MACRO_LOG_L   //printf("Thread id:%d,",self->threadId);printf

//#elif defined ARCH_ARM_SPD

//#endif

/*
 * Get 16 bits from the specified offset of the program counter.  We always
 * want to load 16 bits at a time from the instruction stream -- it's more
 * efficient than 8 and won't have the alignment problems that 32 might.
 *
 * Assumes existence of "const u2* pc".
 */
#define FETCH(_offset)     (pc[(_offset)])

/*
 * Extract instruction byte from 16-bit fetch (_inst is a u2).
 */
#define INST_INST(_inst)    ((_inst) & 0xff)

/*
 * Replace the opcode (used when handling breakpoints).  _opcode is a u1.
 */
#define INST_REPLACE_OP(_inst, _opcode) (((_inst) & 0xff00) | _opcode)

/*
 * Extract the "vA, vB" 4-bit registers from the instruction word (_inst is u2).
 */
#define INST_A(_inst)       (((_inst) >> 8) & 0x0f)
#define INST_B(_inst)       ((_inst) >> 12)

/*
 * Get the 8-bit "vAA" 8-bit register index from the instruction word.
 * (_inst is u2)
 */
#define INST_AA(_inst)      ((_inst) >> 8)

/*
 * The current PC must be available to Throwable constructors, e.g.
 * those created by the various exception throw routines, so that the
 * exception stack trace can be generated correctly.  If we don't do this,
 * the offset within the current method won't be shown correctly.  See the
 * notes in Exception.c.
 *
 * This is also used to determine the address for precise GC.
 *
 * Assumes existence of "u4* fp" and "const u2* pc".
 */
#define EXPORT_PC()         (SAVEAREA_FROM_FP(fp)->xtra.currentPc = pc)

/*
 * Check to see if "obj" is NULL.  If so, throw an exception.  Assumes the
 * pc has already been exported to the stack.
 *
 * Perform additional checks on debug builds.
 *
 * Use this to check for NULL when the instruction handler calls into
 * something that could throw an exception (so we have already called
 * EXPORT_PC at the top).
 */
static /*inline*/ vbool checkForNull(Object* obj)
{
    LOGE("checkForNull:0x%x\n",obj);
    if (obj == NULL) {
        dvmThrowNullPointerException(NULL);
        return false;
    }
#ifdef WITH_EXTRA_OBJECT_VALIDATION
    if (!dvmIsHeapAddressObject(obj)) {
        LOGE("Invalid object %p", obj);
        dvmAbort();
    }
#endif
#ifndef NDEBUG
    if (obj->clazz == NULL || ((u4) obj->clazz) <= 65536) {
        /* probable heap corruption */
        LOGE("Invalid object class %p (in %p)", obj->clazz, obj);
        dvmAbort();
    }
#endif
    return true;
}

/*
 * Check to see if "obj" is NULL.  If so, export the PC into the stack
 * frame and throw an exception.
 *
 * Perform additional checks on debug builds.
 *
 * Use this to check for NULL when the instruction handler doesn't do
 * anything else that can throw an exception.
 */
static /*inline*/ vbool checkForNullExportPC(Object* obj, u4* fp, const u2* pc)
{
    if (obj == NULL) {
        EXPORT_PC();
        dvmThrowNullPointerException(NULL);
        return false;
    }
#ifdef WITH_EXTRA_OBJECT_VALIDATION
    if (!dvmIsHeapAddress(obj)) {
        LOGE("Invalid object %p", obj);
        dvmAbort();
    }
#endif
#ifndef NDEBUG
    if (obj->clazz == NULL || ((u4) obj->clazz) <= 65536) {
        /* probable heap corruption */
        LOGE("Invalid object class %p (in %p)", obj->clazz, obj);
        dvmAbort();
    }
#endif
    return true;
}

/* File: portable/stubdefs.cpp */
/*
 * In the C mterp stubs, "goto" is a function call followed immediately
 * by a return.
 */

#define GOTO_TARGET(_target) _target:

#define GOTO_TARGET_END

/* ugh */
#define JIT_STUB_HACK(x)

/*
 * InterpSave's pc and fp must be valid when breaking out to a
 * "Reportxxx" routine.  Because the portable interpreter uses local
 * variables for these, we must flush prior.  Stubs, however, use
 * the interpSave vars directly, so this is a nop for stubs.
 */
#define PC_FP_TO_SELF()                                                    \
    self->interpSave.pc = pc;                                              \
    self->interpSave.curFrame = fp;
#define PC_TO_SELF() self->interpSave.pc = pc;

/*
 * Instruction framing.  For a switch-oriented implementation this is
 * case/break, for a threaded implementation it's a goto label and an
 * instruction fetch/computed goto.
 *
 * Assumes the existence of "const u2* pc" and (for threaded operation)
 * "u2 inst".
 */
#define V(_op) v_##_op
#define H(_op) op_##_op
#define HANDLE_OPCODE(_op) op_##_op:
#if defined(ARCH_X86)
/*help to trace position of labels!*/
static void * label_little = NULL;
static void * label_big    = NULL;
static int  * jmpad        = NULL;


/*Use as:
 *DCLR_LABEL(V(OP_NOP));
 *SAVE_LABEL(OP_NOP);
 *GOTO_LABEL(H(OP_NOP));
 *ADDR_LABEL(OP_NOP);
*/
#define DCLR_LABEL(label)	void * label = NULL
#define SAVE_LABEL(label)	__asm{ mov [V(label)],offset H(label) }
#define GOTO_LABEL(opcod)	jmpad = (int *)label_little+opcod ; jmpad = (int*)(*(int*)jmpad) ; __asm{ jmp jmpad}
#define ADDR_LABEL(label)	&(V(label))
#elif defined(ARCH_ARM_SPD)
    static int g_goto_opcode = -1;
    #define GOTO_LABEL(opcod)   do{g_goto_opcode = opcod; goto OP_GOTO_TABLE;}while(0)
#else
#error "not support for now!"
#endif

#define SAVE_LOCALS()	\
	do{	\
		MACRO_LOG("Thread id:%d,save globals!\n",self->threadId);\
		curMethod->clazz->pDvmDex = methodClassDex;		\
		self->interpSave.method = (Method *)curMethod;			\
		MACRO_LOG_L("self->interpSave.method:0x%x\n",(int)self->interpSave.method);\
		self->interpSave.pc = pc;						\
		MACRO_LOG_L("self->interpSave.pc:0x%x\n",(int)self->interpSave.pc);\
		self->interpSave.curFrame = fp;					\
		MACRO_LOG_L("self->interpSave.fp:0x%x\n",(int)self->interpSave.curFrame);\
		self->interpSave.retval = retval;				\
		MACRO_LOG_L("self->interpSave.retval:0x%x\n",(int)self->interpSave.retval.j);\
		self->itpSchdSave.inst = inst;					\
		MACRO_LOG_L("self->itpSchdSave.inst:0x%x\n",(int)self->itpSchdSave.inst);\
		self->itpSchdSave.vsrc1 = vsrc1;				\
		MACRO_LOG_L("self->itpSchdSave.vsrc1:0x%x\n",(int)self->itpSchdSave.vsrc1);\
		self->itpSchdSave.vsrc2 = vsrc2;				\
		MACRO_LOG_L("self->itpSchdSave.vsrc2:0x%x\n",(int)self->itpSchdSave.vsrc2);\
		self->itpSchdSave.vdst  = vdst;					\
		MACRO_LOG_L("self->itpSchdSave.vdst:0x%x\n",(int)self->itpSchdSave.vdst);\
		self->itpSchdSave.ref = ref;					\
		MACRO_LOG_L("self->itpSchdSave.ref:0x%x\n",(int)self->itpSchdSave.ref);\
		self->itpSchdSave.methodToCall = (Method *)methodToCall;	\
		MACRO_LOG_L("self->itpSchdSave.methodToCall:0x%x\n",(int)self->itpSchdSave.methodToCall);\
		self->itpSchdSave.methodCallRange = methodCallRange;	\
		MACRO_LOG_L("self->itpSchdSave.methodCallRange:0x%x\n",(int)self->itpSchdSave.methodCallRange);\
		self->itpSchdSave.jumboFormat = jumboFormat;	\
		MACRO_LOG_L("self->itpSchdSave.jumboFormat:0x%x\n",(int)self->itpSchdSave.jumboFormat);\
	}while(0)
static int fuck_ret = 1;
int fuck()
{	
	DVMTraceInf("give a schduler\n");
	return fuck_ret;
}

# define RESCHDULE()			\
		if(CAN_SCHEDULE() && self->beBroken)		\
		{						\
			if(fuck()) {\
				SAVE_LOCALS();		\
				GOTO_bail();		}\
		}

# define FINISH(_offset) {                                                  \
        ADJUST_PC(_offset);                                                 \
		RESCHDULE();														\
        inst = FETCH(0);                                                    \
		MACRO_LOG("Thread id:%d,fetch:%d\n",self->threadId,inst);	\
        if (self->interpBreak.ctl.subMode) {                                \
            dvmCheckBefore(pc, fp, self);                                   \
        }                                                                   \
        GOTO_LABEL(INST_INST(inst));                                        \
    }
# define FINISH_BKPT(_opcode) {                                             \
        GOTO_LABEL(_opcode);                                                \
    }

# define DISPATCH_EXTENDED(_opcode) {                                       \
        GOTO_LABEL(0x100 + _opcode);                                        \
    }

#define OP_END

/*
 * The "goto" targets just turn into goto statements.  The "arguments" are
 * passed through local variables.
 */

#define GOTO_exceptionThrown() goto exceptionThrown;

#define GOTO_returnFromMethod() goto returnFromMethod;

#define GOTO_invoke(_target, _methodCallRange, _jumboFormat)                \
    do {                                                                    \
        methodCallRange = _methodCallRange;                                 \
        jumboFormat = _jumboFormat;                                         \
        goto _target;                                                       \
    } while(false)

/* for this, the "args" are already in the locals */
#define GOTO_invokeMethod(_methodCallRange, _methodToCall, _vsrc1, _vdst) goto invokeMethod;

#define GOTO_bail() goto bail;

/*
 * Periodically check for thread suspension.
 *
 * While we're at it, see if a debugger has attached or the profiler has
 * started.  If so, switch to a different "goto" table.
 */
#define PERIODIC_CHECKS(_pcadj) {                              \
        if (dvmCheckSuspendQuick(self)) {                                   \
            EXPORT_PC();  /* need for precise GC */                         \
            dvmCheckSuspendPending(self);                                   \
        }                                                                   \
    }


/*
 * ===========================================================================
 *
 * What follows are opcode definitions shared between multiple opcodes with
 * minor substitutions handled by the C pre-processor.  These should probably
 * use the mterp substitution mechanism instead, with the code here moved
 * into common fragment files (like the asm "binop.S"), although it's hard
 * to give up the C preprocessor in favor of the much simpler text subst.
 *
 * ===========================================================================
 */

#define HANDLE_NUMCONV(_opcode, _opname, _fromtype, _totype)                \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        SET_REGISTER##_totype(vdst,                                         \
            GET_REGISTER##_fromtype(vsrc1));                                \
        FINISH(1);

#define HANDLE_FLOAT_TO_INT(_opcode, _opname, _fromvtype, _fromrtype,       \
        _tovtype, _tortype)                                                 \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
    {                                                                       \
        /* spec defines specific handling for +/- inf and NaN values */     \
        _fromvtype val;                                                     \
        _tovtype intMin, intMax, result;                                    \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        val = GET_REGISTER##_fromrtype(vsrc1);                              \
        intMin = (_tovtype) 1 << (sizeof(_tovtype) * 8 -1);                 \
        intMax = ~intMin;                                                   \
        result = (_tovtype) val;                                            \
        if (val >= intMax)          /* +inf */                              \
            result = intMax;                                                \
        else if (val <= intMin)     /* -inf */                              \
            result = intMin;                                                \
        else if (val != val)        /* NaN */                               \
            result = 0;                                                     \
        else                                                                \
            result = (_tovtype) val;                                        \
        SET_REGISTER##_tortype(vdst, result);                               \
    }                                                                       \
    FINISH(1);

#define HANDLE_INT_TO_SMALL(_opcode, _opname, _type)                        \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        SET_REGISTER(vdst, (_type) GET_REGISTER(vsrc1));                    \
        FINISH(1);

/* NOTE: the comparison result is always a signed 4-byte integer */
#define HANDLE_OP_CMPX(_opcode, _opname, _varType, _type, _nanVal)          \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        int result;                                                         \
        u2 regs;                                                            \
        _varType val1, val2;                                                \
        vdst = INST_AA(inst);                                               \
        regs = FETCH(1);                                                    \
        vsrc1 = regs & 0xff;                                                \
        vsrc2 = regs >> 8;                                                  \
        val1 = GET_REGISTER##_type(vsrc1);                                  \
        val2 = GET_REGISTER##_type(vsrc2);                                  \
        if (val1 == val2)                                                   \
            result = 0;                                                     \
        else if (val1 < val2)                                               \
            result = -1;                                                    \
        else if (val1 > val2)                                               \
            result = 1;                                                     \
        else                                                                \
            result = (_nanVal);                                             \
        SET_REGISTER(vdst, result);                                         \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_IF_XX(_opcode, _opname, _cmp)                             \
    HANDLE_OPCODE(_opcode /*vA, vB, +CCCC*/)                                \
	{																		\
        vsrc1 = INST_A(inst);                                               \
        vsrc2 = INST_B(inst);                                               \
        if ((s4) GET_REGISTER(vsrc1) _cmp (s4) GET_REGISTER(vsrc2)) {       \
            int branchOffset = (s2)FETCH(1);    /* sign-extended */         \
            if (branchOffset < 0)                                           \
                PERIODIC_CHECKS(branchOffset);                              \
            FINISH(branchOffset);                                           \
        } else {                                                            \
            FINISH(2);                                                      \
        }																	\
	}

#define HANDLE_OP_IF_XXZ(_opcode, _opname, _cmp)                            \
    HANDLE_OPCODE(_opcode /*vAA, +BBBB*/)                                   \
        vsrc1 = INST_AA(inst);                                              \
		MACRO_LOG("Thread id:%d,HANDLE_OP_IF_XXZ,vsrc1=%d\n",self->threadId,vsrc1);\
        if ((s4) GET_REGISTER(vsrc1) _cmp 0) {                              \
            int branchOffset = (s2)FETCH(1);    /* sign-extended */         \
			MACRO_LOG("Thread id:%d,HANDLE_OP_IF_XXZ,branchOffset=%d\n",self->threadId,branchOffset);\
            if (branchOffset < 0)                                           \
                PERIODIC_CHECKS(branchOffset);                              \
            FINISH(branchOffset);                                           \
        } else {                                                            \
            FINISH(2);                                                      \
        }

#define HANDLE_UNOP(_opcode, _opname, _pfx, _sfx, _type)                    \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        SET_REGISTER##_type(vdst, _pfx GET_REGISTER##_type(vsrc1) _sfx);    \
        FINISH(1);

#define HANDLE_OP_X_INT(_opcode, _opname, _op, _chkdiv)                     \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        u2 srcRegs;                                                         \
        vdst = INST_AA(inst);                                               \
        srcRegs = FETCH(1);                                                 \
        vsrc1 = srcRegs & 0xff;                                             \
        vsrc2 = srcRegs >> 8;                                               \
        if (_chkdiv != 0) {                                                 \
            s4 firstVal, secondVal, result;                                 \
            firstVal = GET_REGISTER(vsrc1);                                 \
            secondVal = GET_REGISTER(vsrc2);                                \
            if (secondVal == 0) {                                           \
                EXPORT_PC();                                                \
                dvmThrowArithmeticException("divide by zero");              \
                GOTO_exceptionThrown();                                     \
            }                                                               \
            if ((u4)firstVal == 0x80000000 && secondVal == -1) {            \
                if (_chkdiv == 1)                                           \
                    result = firstVal;  /* division */                      \
                else                                                        \
                    result = 0;         /* remainder */                     \
            } else {                                                        \
                result = firstVal _op secondVal;                            \
            }                                                               \
            SET_REGISTER(vdst, result);                                     \
        } else {                                                            \
            /* non-div/rem case */                                          \
            SET_REGISTER(vdst,                                              \
                (s4) GET_REGISTER(vsrc1) _op (s4) GET_REGISTER(vsrc2));     \
        }                                                                   \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_SHX_INT(_opcode, _opname, _cast, _op)                     \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        u2 srcRegs;                                                         \
        vdst = INST_AA(inst);                                               \
        srcRegs = FETCH(1);                                                 \
        vsrc1 = srcRegs & 0xff;                                             \
        vsrc2 = srcRegs >> 8;                                               \
        SET_REGISTER(vdst,                                                  \
            _cast GET_REGISTER(vsrc1) _op (GET_REGISTER(vsrc2) & 0x1f));    \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_X_INT_LIT16(_opcode, _opname, _op, _chkdiv)               \
    HANDLE_OPCODE(_opcode /*vA, vB, #+CCCC*/)                               \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        vsrc2 = FETCH(1);                                                   \
        if (_chkdiv != 0) {                                                 \
            s4 firstVal, result;                                            \
            firstVal = GET_REGISTER(vsrc1);                                 \
            if ((s2) vsrc2 == 0) {                                          \
                EXPORT_PC();                                                \
                dvmThrowArithmeticException("divide by zero");              \
                GOTO_exceptionThrown();                                     \
            }                                                               \
            if ((u4)firstVal == 0x80000000 && ((s2) vsrc2) == -1) {         \
                /* won't generate /lit16 instr for this; check anyway */    \
                if (_chkdiv == 1)                                           \
                    result = firstVal;  /* division */                      \
                else                                                        \
                    result = 0;         /* remainder */                     \
            } else {                                                        \
                result = firstVal _op (s2) vsrc2;                           \
            }                                                               \
            SET_REGISTER(vdst, result);                                     \
        } else {                                                            \
            /* non-div/rem case */                                          \
            SET_REGISTER(vdst, GET_REGISTER(vsrc1) _op (s2) vsrc2);         \
        }                                                                   \
        FINISH(2);

#define HANDLE_OP_X_INT_LIT8(_opcode, _opname, _op, _chkdiv)                \
    HANDLE_OPCODE(_opcode /*vAA, vBB, #+CC*/)                               \
    {                                                                       \
        u2 litInfo;                                                         \
        vdst = INST_AA(inst);                                               \
        litInfo = FETCH(1);                                                 \
        vsrc1 = litInfo & 0xff;                                             \
        vsrc2 = litInfo >> 8;       /* constant */                          \
		MACRO_LOG("Thread id:%d,HANDLE_OP_X_INT_LIT8,vsrc1=%d,vsrc2=%d\n",self->threadId,vsrc1,vsrc2);\
        if (_chkdiv != 0) {                                                 \
            s4 firstVal, result;                                            \
            firstVal = GET_REGISTER(vsrc1);                                 \
            if ((s1) vsrc2 == 0) {                                          \
                EXPORT_PC();                                                \
                dvmThrowArithmeticException("divide by zero");              \
                GOTO_exceptionThrown();                                     \
            }                                                               \
            if ((u4)firstVal == 0x80000000 && ((s1) vsrc2) == -1) {         \
                if (_chkdiv == 1)                                           \
                    result = firstVal;  /* division */                      \
                else                                                        \
                    result = 0;         /* remainder */                     \
            } else {                                                        \
                result = firstVal _op ((s1) vsrc2);                         \
            }                                                               \
            SET_REGISTER(vdst, result);                                     \
        } else {                                                            \
		    MACRO_LOG("Thread id:%d,HANDLE_OP_X_INT_LIT8,reg[vsrc1]=%d,vsrc2=%d\n",self->threadId,(s4)GET_REGISTER(vsrc1),(s1)vsrc2);\
            SET_REGISTER(vdst,                                              \
                (s4) GET_REGISTER(vsrc1) _op (s1) vsrc2);                   \
        }                                                                   \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_SHX_INT_LIT8(_opcode, _opname, _cast, _op)                \
    HANDLE_OPCODE(_opcode /*vAA, vBB, #+CC*/)                               \
    {                                                                       \
        u2 litInfo;                                                         \
        vdst = INST_AA(inst);                                               \
        litInfo = FETCH(1);                                                 \
        vsrc1 = litInfo & 0xff;                                             \
        vsrc2 = litInfo >> 8;       /* constant */                          \
        SET_REGISTER(vdst,                                                  \
            _cast GET_REGISTER(vsrc1) _op (vsrc2 & 0x1f));                  \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_X_INT_2ADDR(_opcode, _opname, _op, _chkdiv)               \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        if (_chkdiv != 0) {                                                 \
            s4 firstVal, secondVal, result;                                 \
            firstVal = GET_REGISTER(vdst);                                  \
			MACRO_LOG("Thread id:%d,first:%d\n",self->threadId,firstVal);\
            secondVal = GET_REGISTER(vsrc1);                                \
			MACRO_LOG("Thread id:%d,second:%d\n",self->threadId,secondVal);\
            if (secondVal == 0) {                                           \
                EXPORT_PC();                                                \
                dvmThrowArithmeticException("divide by zero");              \
                GOTO_exceptionThrown();                                     \
            }                                                               \
            if ((u4)firstVal == 0x80000000 && secondVal == -1) {            \
                if (_chkdiv == 1)                                           \
                    result = firstVal;  /* division */                      \
                else                                                        \
                    result = 0;         /* remainder */                     \
            } else {                                                        \
                result = firstVal _op secondVal;                            \
            }                                                               \
			MACRO_LOG("Thread id:%d,result:%d\n",self->threadId,result);\
            SET_REGISTER(vdst, result);                                     \
        } else {                                                            \
            SET_REGISTER(vdst,                                              \
                (s4) GET_REGISTER(vdst) _op (s4) GET_REGISTER(vsrc1));      \
        }                                                                   \
        FINISH(1);

#define HANDLE_OP_SHX_INT_2ADDR(_opcode, _opname, _cast, _op)               \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        SET_REGISTER(vdst,                                                  \
            _cast GET_REGISTER(vdst) _op (GET_REGISTER(vsrc1) & 0x1f));     \
        FINISH(1);

#define HANDLE_OP_X_LONG(_opcode, _opname, _op, _chkdiv)                    \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        u2 srcRegs;                                                         \
        vdst = INST_AA(inst);                                               \
        srcRegs = FETCH(1);                                                 \
        vsrc1 = srcRegs & 0xff;                                             \
        vsrc2 = srcRegs >> 8;                                               \
        if (_chkdiv != 0) {                                                 \
            s8 firstVal, secondVal, result;                                 \
            firstVal = GET_REGISTER_WIDE(vsrc1);                            \
            secondVal = GET_REGISTER_WIDE(vsrc2);                           \
            if (secondVal == 0 LL) {                                         \
                EXPORT_PC();                                                \
                dvmThrowArithmeticException("divide by zero");              \
                GOTO_exceptionThrown();                                     \
            }                                                               \
            if ((u8)firstVal == 0x8000000000000000 ULL &&                    \
                secondVal == -1 LL)                                          \
            {                                                               \
                if (_chkdiv == 1)                                           \
                    result = firstVal;  /* division */                      \
                else                                                        \
                    result = 0;         /* remainder */                     \
            } else {                                                        \
                result = firstVal _op secondVal;                            \
            }                                                               \
            SET_REGISTER_WIDE(vdst, result);                                \
        } else {                                                            \
            SET_REGISTER_WIDE(vdst,                                         \
                (s8) GET_REGISTER_WIDE(vsrc1) _op (s8) GET_REGISTER_WIDE(vsrc2)); \
        }                                                                   \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_SHX_LONG(_opcode, _opname, _cast, _op)                    \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        u2 srcRegs;                                                         \
        vdst = INST_AA(inst);                                               \
        srcRegs = FETCH(1);                                                 \
        vsrc1 = srcRegs & 0xff;                                             \
        vsrc2 = srcRegs >> 8;                                               \
        SET_REGISTER_WIDE(vdst,                                             \
            _cast GET_REGISTER_WIDE(vsrc1) _op (GET_REGISTER(vsrc2) & 0x3f)); \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_X_LONG_2ADDR(_opcode, _opname, _op, _chkdiv)              \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        if (_chkdiv != 0) {                                                 \
            s8 firstVal, secondVal, result;                                 \
            firstVal = GET_REGISTER_WIDE(vdst);                             \
            secondVal = GET_REGISTER_WIDE(vsrc1);                           \
            if (secondVal == 0 LL) {                                         \
                EXPORT_PC();                                                \
                dvmThrowArithmeticException("divide by zero");              \
                GOTO_exceptionThrown();                                     \
            }                                                               \
            if ((u8)firstVal == 0x8000000000000000 ULL &&                    \
                secondVal == -1 LL)                                          \
            {                                                               \
                if (_chkdiv == 1)                                           \
                    result = firstVal;  /* division */                      \
                else                                                        \
                    result = 0;         /* remainder */                     \
            } else {                                                        \
                result = firstVal _op secondVal;                            \
            }                                                               \
            SET_REGISTER_WIDE(vdst, result);                                \
        } else {                                                            \
            SET_REGISTER_WIDE(vdst,                                         \
                (s8) GET_REGISTER_WIDE(vdst) _op (s8)GET_REGISTER_WIDE(vsrc1));\
        }                                                                   \
        FINISH(1);

#define HANDLE_OP_SHX_LONG_2ADDR(_opcode, _opname, _cast, _op)              \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        SET_REGISTER_WIDE(vdst,                                             \
            _cast GET_REGISTER_WIDE(vdst) _op (GET_REGISTER(vsrc1) & 0x3f)); \
        FINISH(1);

#define HANDLE_OP_X_FLOAT(_opcode, _opname, _op)                            \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        u2 srcRegs;                                                         \
        vdst = INST_AA(inst);                                               \
        srcRegs = FETCH(1);                                                 \
        vsrc1 = srcRegs & 0xff;                                             \
        vsrc2 = srcRegs >> 8;                                               \
        SET_REGISTER_FLOAT(vdst,                                            \
            GET_REGISTER_FLOAT(vsrc1) _op GET_REGISTER_FLOAT(vsrc2));       \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_X_DOUBLE(_opcode, _opname, _op)                           \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        u2 srcRegs;                                                         \
        vdst = INST_AA(inst);                                               \
        srcRegs = FETCH(1);                                                 \
        vsrc1 = srcRegs & 0xff;                                             \
        vsrc2 = srcRegs >> 8;                                               \
        SET_REGISTER_DOUBLE(vdst,                                           \
            GET_REGISTER_DOUBLE(vsrc1) _op GET_REGISTER_DOUBLE(vsrc2));     \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_X_FLOAT_2ADDR(_opcode, _opname, _op)                      \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        SET_REGISTER_FLOAT(vdst,                                            \
            GET_REGISTER_FLOAT(vdst) _op GET_REGISTER_FLOAT(vsrc1));        \
        FINISH(1);

#define HANDLE_OP_X_DOUBLE_2ADDR(_opcode, _opname, _op)                     \
    HANDLE_OPCODE(_opcode /*vA, vB*/)                                       \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);                                               \
        SET_REGISTER_DOUBLE(vdst,                                           \
            GET_REGISTER_DOUBLE(vdst) _op GET_REGISTER_DOUBLE(vsrc1));      \
        FINISH(1);

#define HANDLE_OP_AGET(_opcode, _opname, _type, _regsize)                   \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        ArrayObject* arrayObj;                                              \
        u2 arrayInfo;                                                       \
        EXPORT_PC();                                                        \
        vdst = INST_AA(inst);                                               \
        arrayInfo = FETCH(1);                                               \
        vsrc1 = arrayInfo & 0xff;    /* array ptr */                        \
        vsrc2 = arrayInfo >> 8;      /* index */                            \
        arrayObj = (ArrayObject*) GET_REGISTER(vsrc1);                      \
        if (!checkForNull((Object*) arrayObj))                              \
            GOTO_exceptionThrown();                                         \
        if (GET_REGISTER(vsrc2) >= arrayObj->length) {                      \
            dvmThrowArrayIndexOutOfBoundsException(                         \
                arrayObj->length, GET_REGISTER(vsrc2));                     \
            GOTO_exceptionThrown();                                         \
        }                                                                   \
        SET_REGISTER##_regsize(vdst,                                        \
            ((_type*)(void*)arrayObj->contents)[GET_REGISTER(vsrc2)]);      \
    }                                                                       \
    FINISH(2);

#define HANDLE_OP_APUT(_opcode, _opname, _type, _regsize)                   \
    HANDLE_OPCODE(_opcode /*vAA, vBB, vCC*/)                                \
    {                                                                       \
        ArrayObject* arrayObj;                                              \
        u2 arrayInfo;                                                       \
        EXPORT_PC();                                                        \
        vdst = INST_AA(inst);       /* AA: source value */                  \
        arrayInfo = FETCH(1);                                               \
        vsrc1 = arrayInfo & 0xff;   /* BB: array ptr */                     \
        vsrc2 = arrayInfo >> 8;     /* CC: index */                         \
        arrayObj = (ArrayObject*) GET_REGISTER(vsrc1);                      \
        if (!checkForNull((Object*) arrayObj))                              \
            GOTO_exceptionThrown();                                         \
        if (GET_REGISTER(vsrc2) >= arrayObj->length) {                      \
            dvmThrowArrayIndexOutOfBoundsException(                         \
                arrayObj->length, GET_REGISTER(vsrc2));                     \
            GOTO_exceptionThrown();                                         \
        }                                                                   \
        ((_type*)(void*)arrayObj->contents)[GET_REGISTER(vsrc2)] =          \
            GET_REGISTER##_regsize(vdst);                                   \
    }                                                                       \
    FINISH(2);

/*
 * It's possible to get a bad value out of a field with sub-32-bit stores
 * because the -quick versions always operate on 32 bits.  Consider:
 *   short foo = -1  (sets a 32-bit register to 0xffffffff)
 *   iput-quick foo  (writes all 32 bits to the field)
 *   short bar = 1   (sets a 32-bit register to 0x00000001)
 *   iput-short      (writes the low 16 bits to the field)
 *   iget-quick foo  (reads all 32 bits from the field, yielding 0xffff0001)
 * This can only happen when optimized and non-optimized code has interleaved
 * access to the same field.  This is unlikely but possible.
 *
 * The easiest way to fix this is to always read/write 32 bits at a time.  On
 * a device with a 16-bit data bus this is sub-optimal.  (The alternative
 * approach is to have sub-int versions of iget-quick, but now we're wasting
 * Dalvik instruction space and making it less likely that handler code will
 * already be in the CPU i-cache.)
 */
#define HANDLE_IGET_X(_opcode, _opname, _ftype, _regsize)                   \
    HANDLE_OPCODE(_opcode /*vA, vB, field@CCCC*/)                           \
    {                                                                       \
        InstField* ifield;                                                  \
        Object* obj;                                                        \
        EXPORT_PC();                                                        \
        vdst = INST_A(inst);                                                \
		MACRO_LOG("Thread id:%d,HANDLE_IGET_X,vdst=0x%x\n",self->threadId,vdst);\
        vsrc1 = INST_B(inst);   /* object ptr */                            \
		MACRO_LOG("Thread id:%d,HANDLE_IGET_X,vsrc1=0x%x\n",self->threadId,vsrc1);\
        ref = FETCH(1);         /* field ref */                             \
        obj = (Object*) GET_REGISTER(vsrc1);                                \
		MACRO_LOG("Thread id:%d,HANDLE_IGET_X,obj=0x%x\n",self->threadId,obj);\
        if (!checkForNull(obj))  {                                           \
		GOTO_exceptionThrown(); MACRO_LOG("Thread id:%d,HANDLE_IGET_X,exceptionn",self->threadId); }                  \
        ifield = (InstField*) dvmDexGetResolvedField(methodClassDex, ref);  \
		MACRO_LOG("Thread id:%d,HANDLE_IGET_X,ifield=0x%x\n",self->threadId,ifield);\
        if (ifield == NULL) {                                               \
            ifield = dvmResolveInstField(curMethod->clazz, ref);            \
            if (ifield == NULL)                                             \
                GOTO_exceptionThrown();                                     \
        }                                                                   \
        SET_REGISTER##_regsize(vdst,                                        \
            dvmGetField##_ftype(obj, ifield->byteOffset));                  \
    }                                                                       \
    FINISH(2);

#define HANDLE_IGET_X_JUMBO(_opcode, _opname, _ftype, _regsize)             \
    HANDLE_OPCODE(_opcode /*vBBBB, vCCCC, class@AAAAAAAA*/)                 \
    {                                                                       \
        InstField* ifield;                                                  \
        Object* obj;                                                        \
        EXPORT_PC();                                                        \
        ref = FETCH(1) | (u4)FETCH(2) << 16;   /* field ref */              \
        vdst = FETCH(3);                                                    \
        vsrc1 = FETCH(4);                      /* object ptr */             \
        obj = (Object*) GET_REGISTER(vsrc1);                                \
        if (!checkForNull(obj))                                             \
            GOTO_exceptionThrown();                                         \
        ifield = (InstField*) dvmDexGetResolvedField(methodClassDex, ref);  \
        if (ifield == NULL) {                                               \
            ifield = dvmResolveInstField(curMethod->clazz, ref);            \
            if (ifield == NULL)                                             \
                GOTO_exceptionThrown();                                     \
        }                                                                   \
        SET_REGISTER##_regsize(vdst,                                        \
            dvmGetField##_ftype(obj, ifield->byteOffset));                  \
    }                                                                       \
    FINISH(5);

#define HANDLE_IGET_X_QUICK(_opcode, _opname, _ftype, _regsize)             \
    HANDLE_OPCODE(_opcode /*vA, vB, field@CCCC*/)                           \
    {                                                                       \
        Object* obj;                                                        \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);   /* object ptr */                            \
        ref = FETCH(1);         /* field offset */                          \
        obj = (Object*) GET_REGISTER(vsrc1);                                \
        if (!checkForNullExportPC(obj, fp, pc))                             \
            GOTO_exceptionThrown();                                         \
        SET_REGISTER##_regsize(vdst, dvmGetField##_ftype(obj, ref));        \
    }                                                                       \
    FINISH(2);

#define HANDLE_IPUT_X(_opcode, _opname, _ftype, _regsize)                   \
    HANDLE_OPCODE(_opcode /*vA, vB, field@CCCC*/)                           \
    {                                                                       \
        InstField* ifield;                                                  \
        Object* obj;                                                        \
        EXPORT_PC();                                                        \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);   /* object ptr */                            \
        ref = FETCH(1);         /* field ref */                             \
        obj = (Object*) GET_REGISTER(vsrc1);                                \
		MACRO_LOG("Thread id:%d,HANDLE_IPUT_X,obj=0x%x",self->threadId,obj);\
        if (!checkForNull(obj))                                             \
            GOTO_exceptionThrown();                                         \
        ifield = (InstField*) dvmDexGetResolvedField(methodClassDex, ref);  \
		MACRO_LOG("Thread id:%d,HANDLE_IPUT_X,ifield=0x%x",self->threadId,ifield);\
        if (ifield == NULL) {                                               \
            ifield = dvmResolveInstField(curMethod->clazz, ref);            \
            if (ifield == NULL)                                             \
                GOTO_exceptionThrown();                                     \
        }                                                                   \
        dvmSetField##_ftype(obj, ifield->byteOffset,                        \
            GET_REGISTER##_regsize(vdst));                                  \
    }                                                                       \
    FINISH(2);

#define HANDLE_IPUT_X_JUMBO(_opcode, _opname, _ftype, _regsize)             \
    HANDLE_OPCODE(_opcode /*vBBBB, vCCCC, class@AAAAAAAA*/)                 \
    {                                                                       \
        InstField* ifield;                                                  \
        Object* obj;                                                        \
        EXPORT_PC();                                                        \
        ref = FETCH(1) | (u4)FETCH(2) << 16;   /* field ref */              \
        vdst = FETCH(3);                                                    \
        vsrc1 = FETCH(4);                      /* object ptr */             \
        obj = (Object*) GET_REGISTER(vsrc1);                                \
        if (!checkForNull(obj))                                             \
            GOTO_exceptionThrown();                                         \
        ifield = (InstField*) dvmDexGetResolvedField(methodClassDex, ref);  \
        if (ifield == NULL) {                                               \
            ifield = dvmResolveInstField(curMethod->clazz, ref);            \
            if (ifield == NULL)                                             \
                GOTO_exceptionThrown();                                     \
        }                                                                   \
        dvmSetField##_ftype(obj, ifield->byteOffset,                        \
            GET_REGISTER##_regsize(vdst));                                  \
    }                                                                       \
    FINISH(5);

#define HANDLE_IPUT_X_QUICK(_opcode, _opname, _ftype, _regsize)             \
    HANDLE_OPCODE(_opcode /*vA, vB, field@CCCC*/)                           \
    {                                                                       \
        Object* obj;                                                        \
        vdst = INST_A(inst);                                                \
        vsrc1 = INST_B(inst);   /* object ptr */                            \
        ref = FETCH(1);         /* field offset */                          \
        obj = (Object*) GET_REGISTER(vsrc1);                                \
        if (!checkForNullExportPC(obj, fp, pc))                             \
            GOTO_exceptionThrown();                                         \
        dvmSetField##_ftype(obj, ref, GET_REGISTER##_regsize(vdst));        \
    }                                                                       \
    FINISH(2);

/*
 * The JIT needs dvmDexGetResolvedField() to return non-null.
 * Because the portable interpreter is not involved with the JIT
 * and trace building, we only need the extra check here when this
 * code is massaged into a stub called from an assembly interpreter.
 * This is controlled by the JIT_STUB_HACK maco.
 */

#define HANDLE_SGET_X(_opcode, _opname, _ftype, _regsize)                   \
    HANDLE_OPCODE(_opcode /*vAA, field@BBBB*/)                              \
    {                                                                       \
        StaticField* sfield;                                                \
        vdst = INST_AA(inst);                                               \
        ref = FETCH(1);         /* field ref */                             \
        sfield = (StaticField*)dvmDexGetResolvedField(methodClassDex, ref); \
        if (sfield == NULL) {                                               \
            EXPORT_PC();                                                    \
            sfield = dvmResolveStaticField(curMethod->clazz, ref);          \
            if (sfield == NULL)                                             \
                GOTO_exceptionThrown();                                     \
            if (dvmDexGetResolvedField(methodClassDex, ref) == NULL) {      \
                JIT_STUB_HACK(dvmJitEndTraceSelect(self,pc));               \
            }                                                               \
        }                                                                   \
        SET_REGISTER##_regsize(vdst, dvmGetStaticField##_ftype(sfield));    \
    }                                                                       \
    FINISH(2);

#define HANDLE_SGET_X_JUMBO(_opcode, _opname, _ftype, _regsize)             \
    HANDLE_OPCODE(_opcode /*vBBBB, class@AAAAAAAA*/)                        \
    {                                                                       \
        StaticField* sfield;                                                \
        ref = FETCH(1) | (u4)FETCH(2) << 16;   /* field ref */              \
        vdst = FETCH(3);                                                    \
        sfield = (StaticField*)dvmDexGetResolvedField(methodClassDex, ref); \
        if (sfield == NULL) {                                               \
            EXPORT_PC();                                                    \
            sfield = dvmResolveStaticField(curMethod->clazz, ref);          \
            if (sfield == NULL)                                             \
                GOTO_exceptionThrown();                                     \
            if (dvmDexGetResolvedField(methodClassDex, ref) == NULL) {      \
                JIT_STUB_HACK(dvmJitEndTraceSelect(self,pc));               \
            }                                                               \
        }                                                                   \
        SET_REGISTER##_regsize(vdst, dvmGetStaticField##_ftype(sfield));    \
    }                                                                       \
    FINISH(4);

#define HANDLE_SPUT_X(_opcode, _opname, _ftype, _regsize)                   \
    HANDLE_OPCODE(_opcode /*vAA, field@BBBB*/)                              \
    {                                                                       \
        StaticField* sfield;                                                \
        vdst = INST_AA(inst);                                               \
        ref = FETCH(1);         /* field ref */                             \
        sfield = (StaticField*)dvmDexGetResolvedField(methodClassDex, ref); \
        if (sfield == NULL) {                                               \
            EXPORT_PC();                                                    \
            sfield = dvmResolveStaticField(curMethod->clazz, ref);          \
            if (sfield == NULL)                                             \
                GOTO_exceptionThrown();                                     \
            if (dvmDexGetResolvedField(methodClassDex, ref) == NULL) {      \
                JIT_STUB_HACK(dvmJitEndTraceSelect(self,pc));               \
            }                                                               \
        }                                                                   \
        dvmSetStaticField##_ftype(sfield, GET_REGISTER##_regsize(vdst));    \
    }                                                                       \
    FINISH(2);

#define HANDLE_SPUT_X_JUMBO(_opcode, _opname, _ftype, _regsize)             \
    HANDLE_OPCODE(_opcode /*vBBBB, class@AAAAAAAA*/)                        \
    {                                                                       \
        StaticField* sfield;                                                \
        ref = FETCH(1) | (u4)FETCH(2) << 16;   /* field ref */              \
        vdst = FETCH(3);                                                    \
        sfield = (StaticField*)dvmDexGetResolvedField(methodClassDex, ref); \
        if (sfield == NULL) {                                               \
            EXPORT_PC();                                                    \
            sfield = dvmResolveStaticField(curMethod->clazz, ref);          \
            if (sfield == NULL)                                             \
                GOTO_exceptionThrown();                                     \
            if (dvmDexGetResolvedField(methodClassDex, ref) == NULL) {      \
                JIT_STUB_HACK(dvmJitEndTraceSelect(self,pc));               \
            }                                                               \
        }                                                                   \
        dvmSetStaticField##_ftype(sfield, GET_REGISTER##_regsize(vdst));    \
    }                                                                       \
    FINISH(4);


#if defined(ARCH_X86)
/*Re-declare all labels as vars!*/

/*declare op labels*/
DCLR_LABEL(V(OP_NOP));
DCLR_LABEL(V(OP_MOVE));
DCLR_LABEL(V(OP_MOVE_FROM16));
DCLR_LABEL(V(OP_MOVE_16));
DCLR_LABEL(V(OP_MOVE_WIDE));
DCLR_LABEL(V(OP_MOVE_WIDE_FROM16));
DCLR_LABEL(V(OP_MOVE_WIDE_16));
DCLR_LABEL(V(OP_MOVE_OBJECT));
DCLR_LABEL(V(OP_MOVE_OBJECT_FROM16));
DCLR_LABEL(V(OP_MOVE_OBJECT_16));
DCLR_LABEL(V(OP_MOVE_RESULT));
DCLR_LABEL(V(OP_MOVE_RESULT_WIDE));
DCLR_LABEL(V(OP_MOVE_RESULT_OBJECT));
DCLR_LABEL(V(OP_MOVE_EXCEPTION));
DCLR_LABEL(V(OP_RETURN_VOID));
DCLR_LABEL(V(OP_RETURN));
DCLR_LABEL(V(OP_RETURN_WIDE));
DCLR_LABEL(V(OP_RETURN_OBJECT));
DCLR_LABEL(V(OP_CONST_4));
DCLR_LABEL(V(OP_CONST_16));
DCLR_LABEL(V(OP_CONST));
DCLR_LABEL(V(OP_CONST_HIGH16));
DCLR_LABEL(V(OP_CONST_WIDE_16));
DCLR_LABEL(V(OP_CONST_WIDE_32));
DCLR_LABEL(V(OP_CONST_WIDE));
DCLR_LABEL(V(OP_CONST_WIDE_HIGH16));
DCLR_LABEL(V(OP_CONST_STRING));
DCLR_LABEL(V(OP_CONST_STRING_JUMBO));
DCLR_LABEL(V(OP_CONST_CLASS));
DCLR_LABEL(V(OP_MONITOR_ENTER));
DCLR_LABEL(V(OP_MONITOR_EXIT));
DCLR_LABEL(V(OP_CHECK_CAST));
DCLR_LABEL(V(OP_INSTANCE_OF));
DCLR_LABEL(V(OP_ARRAY_LENGTH));
DCLR_LABEL(V(OP_NEW_INSTANCE));
DCLR_LABEL(V(OP_NEW_ARRAY));
DCLR_LABEL(V(OP_FILLED_NEW_ARRAY));
DCLR_LABEL(V(OP_FILLED_NEW_ARRAY_RANGE));
DCLR_LABEL(V(OP_FILL_ARRAY_DATA));
DCLR_LABEL(V(OP_THROW));
DCLR_LABEL(V(OP_GOTO));
DCLR_LABEL(V(OP_GOTO_16));
DCLR_LABEL(V(OP_GOTO_32));
DCLR_LABEL(V(OP_PACKED_SWITCH));
DCLR_LABEL(V(OP_SPARSE_SWITCH));
DCLR_LABEL(V(OP_CMPL_FLOAT));
DCLR_LABEL(V(OP_CMPG_FLOAT));
DCLR_LABEL(V(OP_CMPL_DOUBLE));
DCLR_LABEL(V(OP_CMPG_DOUBLE));
DCLR_LABEL(V(OP_CMP_LONG));
DCLR_LABEL(V(OP_IF_EQ));
DCLR_LABEL(V(OP_IF_NE));
DCLR_LABEL(V(OP_IF_LT));
DCLR_LABEL(V(OP_IF_GE));
DCLR_LABEL(V(OP_IF_GT));
DCLR_LABEL(V(OP_IF_LE));
DCLR_LABEL(V(OP_IF_EQZ));
DCLR_LABEL(V(OP_IF_NEZ));
DCLR_LABEL(V(OP_IF_LTZ));
DCLR_LABEL(V(OP_IF_GEZ));
DCLR_LABEL(V(OP_IF_GTZ));
DCLR_LABEL(V(OP_IF_LEZ));
DCLR_LABEL(V(OP_UNUSED_3E));
DCLR_LABEL(V(OP_UNUSED_3F));
DCLR_LABEL(V(OP_UNUSED_40));
DCLR_LABEL(V(OP_UNUSED_41));
DCLR_LABEL(V(OP_UNUSED_42));
DCLR_LABEL(V(OP_UNUSED_43));
DCLR_LABEL(V(OP_AGET));
DCLR_LABEL(V(OP_AGET_WIDE));
DCLR_LABEL(V(OP_AGET_OBJECT));
DCLR_LABEL(V(OP_AGET_BOOLEAN));
DCLR_LABEL(V(OP_AGET_BYTE));
DCLR_LABEL(V(OP_AGET_CHAR));
DCLR_LABEL(V(OP_AGET_SHORT));
DCLR_LABEL(V(OP_APUT));
DCLR_LABEL(V(OP_APUT_WIDE));
DCLR_LABEL(V(OP_APUT_OBJECT));
DCLR_LABEL(V(OP_APUT_BOOLEAN));
DCLR_LABEL(V(OP_APUT_BYTE));
DCLR_LABEL(V(OP_APUT_CHAR));
DCLR_LABEL(V(OP_APUT_SHORT));
DCLR_LABEL(V(OP_IGET));
DCLR_LABEL(V(OP_IGET_WIDE));
DCLR_LABEL(V(OP_IGET_OBJECT));
DCLR_LABEL(V(OP_IGET_BOOLEAN));
DCLR_LABEL(V(OP_IGET_BYTE));
DCLR_LABEL(V(OP_IGET_CHAR));
DCLR_LABEL(V(OP_IGET_SHORT));
DCLR_LABEL(V(OP_IPUT));
DCLR_LABEL(V(OP_IPUT_WIDE));
DCLR_LABEL(V(OP_IPUT_OBJECT));
DCLR_LABEL(V(OP_IPUT_BOOLEAN));
DCLR_LABEL(V(OP_IPUT_BYTE));
DCLR_LABEL(V(OP_IPUT_CHAR));
DCLR_LABEL(V(OP_IPUT_SHORT));
DCLR_LABEL(V(OP_SGET));
DCLR_LABEL(V(OP_SGET_WIDE));
DCLR_LABEL(V(OP_SGET_OBJECT));
DCLR_LABEL(V(OP_SGET_BOOLEAN));
DCLR_LABEL(V(OP_SGET_BYTE));
DCLR_LABEL(V(OP_SGET_CHAR));
DCLR_LABEL(V(OP_SGET_SHORT));
DCLR_LABEL(V(OP_SPUT));
DCLR_LABEL(V(OP_SPUT_WIDE));
DCLR_LABEL(V(OP_SPUT_OBJECT));
DCLR_LABEL(V(OP_SPUT_BOOLEAN));
DCLR_LABEL(V(OP_SPUT_BYTE));
DCLR_LABEL(V(OP_SPUT_CHAR));
DCLR_LABEL(V(OP_SPUT_SHORT));
DCLR_LABEL(V(OP_INVOKE_VIRTUAL));
DCLR_LABEL(V(OP_INVOKE_SUPER));
DCLR_LABEL(V(OP_INVOKE_DIRECT));
DCLR_LABEL(V(OP_INVOKE_STATIC));
DCLR_LABEL(V(OP_INVOKE_INTERFACE));
DCLR_LABEL(V(OP_UNUSED_73));
DCLR_LABEL(V(OP_INVOKE_VIRTUAL_RANGE));
DCLR_LABEL(V(OP_INVOKE_SUPER_RANGE));
DCLR_LABEL(V(OP_INVOKE_DIRECT_RANGE));
DCLR_LABEL(V(OP_INVOKE_STATIC_RANGE));
DCLR_LABEL(V(OP_INVOKE_INTERFACE_RANGE));
DCLR_LABEL(V(OP_UNUSED_79));
DCLR_LABEL(V(OP_UNUSED_7A));
DCLR_LABEL(V(OP_NEG_INT));
DCLR_LABEL(V(OP_NOT_INT));
DCLR_LABEL(V(OP_NEG_LONG));
DCLR_LABEL(V(OP_NOT_LONG));
DCLR_LABEL(V(OP_NEG_FLOAT));
DCLR_LABEL(V(OP_NEG_DOUBLE));
DCLR_LABEL(V(OP_INT_TO_LONG));
DCLR_LABEL(V(OP_INT_TO_FLOAT));
DCLR_LABEL(V(OP_INT_TO_DOUBLE));
DCLR_LABEL(V(OP_LONG_TO_INT));
DCLR_LABEL(V(OP_LONG_TO_FLOAT));
DCLR_LABEL(V(OP_LONG_TO_DOUBLE));
DCLR_LABEL(V(OP_FLOAT_TO_INT));
DCLR_LABEL(V(OP_FLOAT_TO_LONG));
DCLR_LABEL(V(OP_FLOAT_TO_DOUBLE));
DCLR_LABEL(V(OP_DOUBLE_TO_INT));
DCLR_LABEL(V(OP_DOUBLE_TO_LONG));
DCLR_LABEL(V(OP_DOUBLE_TO_FLOAT));
DCLR_LABEL(V(OP_INT_TO_BYTE));
DCLR_LABEL(V(OP_INT_TO_CHAR));
DCLR_LABEL(V(OP_INT_TO_SHORT));
DCLR_LABEL(V(OP_ADD_INT));
DCLR_LABEL(V(OP_SUB_INT));
DCLR_LABEL(V(OP_MUL_INT));
DCLR_LABEL(V(OP_DIV_INT));
DCLR_LABEL(V(OP_REM_INT));
DCLR_LABEL(V(OP_AND_INT));
DCLR_LABEL(V(OP_OR_INT));
DCLR_LABEL(V(OP_XOR_INT));
DCLR_LABEL(V(OP_SHL_INT));
DCLR_LABEL(V(OP_SHR_INT));
DCLR_LABEL(V(OP_USHR_INT));
DCLR_LABEL(V(OP_ADD_LONG));
DCLR_LABEL(V(OP_SUB_LONG));
DCLR_LABEL(V(OP_MUL_LONG));
DCLR_LABEL(V(OP_DIV_LONG));
DCLR_LABEL(V(OP_REM_LONG));
DCLR_LABEL(V(OP_AND_LONG));
DCLR_LABEL(V(OP_OR_LONG));
DCLR_LABEL(V(OP_XOR_LONG));
DCLR_LABEL(V(OP_SHL_LONG));
DCLR_LABEL(V(OP_SHR_LONG));
DCLR_LABEL(V(OP_USHR_LONG));
DCLR_LABEL(V(OP_ADD_FLOAT));
DCLR_LABEL(V(OP_SUB_FLOAT));
DCLR_LABEL(V(OP_MUL_FLOAT));
DCLR_LABEL(V(OP_DIV_FLOAT));
DCLR_LABEL(V(OP_REM_FLOAT));
DCLR_LABEL(V(OP_ADD_DOUBLE));
DCLR_LABEL(V(OP_SUB_DOUBLE));
DCLR_LABEL(V(OP_MUL_DOUBLE));
DCLR_LABEL(V(OP_DIV_DOUBLE));
DCLR_LABEL(V(OP_REM_DOUBLE));
DCLR_LABEL(V(OP_ADD_INT_2ADDR));
DCLR_LABEL(V(OP_SUB_INT_2ADDR));
DCLR_LABEL(V(OP_MUL_INT_2ADDR));
DCLR_LABEL(V(OP_DIV_INT_2ADDR));
DCLR_LABEL(V(OP_REM_INT_2ADDR));
DCLR_LABEL(V(OP_AND_INT_2ADDR));
DCLR_LABEL(V(OP_OR_INT_2ADDR));
DCLR_LABEL(V(OP_XOR_INT_2ADDR));
DCLR_LABEL(V(OP_SHL_INT_2ADDR));
DCLR_LABEL(V(OP_SHR_INT_2ADDR));
DCLR_LABEL(V(OP_USHR_INT_2ADDR));
DCLR_LABEL(V(OP_ADD_LONG_2ADDR));
DCLR_LABEL(V(OP_SUB_LONG_2ADDR));
DCLR_LABEL(V(OP_MUL_LONG_2ADDR));
DCLR_LABEL(V(OP_DIV_LONG_2ADDR));
DCLR_LABEL(V(OP_REM_LONG_2ADDR));
DCLR_LABEL(V(OP_AND_LONG_2ADDR));
DCLR_LABEL(V(OP_OR_LONG_2ADDR));
DCLR_LABEL(V(OP_XOR_LONG_2ADDR));
DCLR_LABEL(V(OP_SHL_LONG_2ADDR));
DCLR_LABEL(V(OP_SHR_LONG_2ADDR));
DCLR_LABEL(V(OP_USHR_LONG_2ADDR));
DCLR_LABEL(V(OP_ADD_FLOAT_2ADDR));
DCLR_LABEL(V(OP_SUB_FLOAT_2ADDR));
DCLR_LABEL(V(OP_MUL_FLOAT_2ADDR));
DCLR_LABEL(V(OP_DIV_FLOAT_2ADDR));
DCLR_LABEL(V(OP_REM_FLOAT_2ADDR));
DCLR_LABEL(V(OP_ADD_DOUBLE_2ADDR));
DCLR_LABEL(V(OP_SUB_DOUBLE_2ADDR));
DCLR_LABEL(V(OP_MUL_DOUBLE_2ADDR));
DCLR_LABEL(V(OP_DIV_DOUBLE_2ADDR));
DCLR_LABEL(V(OP_REM_DOUBLE_2ADDR));
DCLR_LABEL(V(OP_ADD_INT_LIT16));
DCLR_LABEL(V(OP_RSUB_INT));
DCLR_LABEL(V(OP_MUL_INT_LIT16));
DCLR_LABEL(V(OP_DIV_INT_LIT16));
DCLR_LABEL(V(OP_REM_INT_LIT16));
DCLR_LABEL(V(OP_AND_INT_LIT16));
DCLR_LABEL(V(OP_OR_INT_LIT16));
DCLR_LABEL(V(OP_XOR_INT_LIT16));
DCLR_LABEL(V(OP_ADD_INT_LIT8));
DCLR_LABEL(V(OP_RSUB_INT_LIT8));
DCLR_LABEL(V(OP_MUL_INT_LIT8));
DCLR_LABEL(V(OP_DIV_INT_LIT8));
DCLR_LABEL(V(OP_REM_INT_LIT8));
DCLR_LABEL(V(OP_AND_INT_LIT8));
DCLR_LABEL(V(OP_OR_INT_LIT8));
DCLR_LABEL(V(OP_XOR_INT_LIT8));
DCLR_LABEL(V(OP_SHL_INT_LIT8));
DCLR_LABEL(V(OP_SHR_INT_LIT8));
DCLR_LABEL(V(OP_USHR_INT_LIT8));
DCLR_LABEL(V(OP_IGET_VOLATILE));
DCLR_LABEL(V(OP_IPUT_VOLATILE));
DCLR_LABEL(V(OP_SGET_VOLATILE));
DCLR_LABEL(V(OP_SPUT_VOLATILE));
DCLR_LABEL(V(OP_IGET_OBJECT_VOLATILE));
DCLR_LABEL(V(OP_IGET_WIDE_VOLATILE));
DCLR_LABEL(V(OP_IPUT_WIDE_VOLATILE));
DCLR_LABEL(V(OP_SGET_WIDE_VOLATILE));
DCLR_LABEL(V(OP_SPUT_WIDE_VOLATILE));
DCLR_LABEL(V(OP_BREAKPOINT));
DCLR_LABEL(V(OP_THROW_VERIFICATION_ERROR));
DCLR_LABEL(V(OP_EXECUTE_INLINE));
DCLR_LABEL(V(OP_EXECUTE_INLINE_RANGE));
DCLR_LABEL(V(OP_INVOKE_OBJECT_INIT_RANGE));
DCLR_LABEL(V(OP_RETURN_VOID_BARRIER));
DCLR_LABEL(V(OP_IGET_QUICK));
DCLR_LABEL(V(OP_IGET_WIDE_QUICK));
DCLR_LABEL(V(OP_IGET_OBJECT_QUICK));
DCLR_LABEL(V(OP_IPUT_QUICK));
DCLR_LABEL(V(OP_IPUT_WIDE_QUICK));
DCLR_LABEL(V(OP_IPUT_OBJECT_QUICK));
DCLR_LABEL(V(OP_INVOKE_VIRTUAL_QUICK));
DCLR_LABEL(V(OP_INVOKE_VIRTUAL_QUICK_RANGE));
DCLR_LABEL(V(OP_INVOKE_SUPER_QUICK));
DCLR_LABEL(V(OP_INVOKE_SUPER_QUICK_RANGE));
DCLR_LABEL(V(OP_IPUT_OBJECT_VOLATILE));
DCLR_LABEL(V(OP_SGET_OBJECT_VOLATILE));
DCLR_LABEL(V(OP_SPUT_OBJECT_VOLATILE));
DCLR_LABEL(V(OP_DISPATCH_FF));
DCLR_LABEL(V(OP_CONST_CLASS_JUMBO));
DCLR_LABEL(V(OP_CHECK_CAST_JUMBO));
DCLR_LABEL(V(OP_INSTANCE_OF_JUMBO));
DCLR_LABEL(V(OP_NEW_INSTANCE_JUMBO));
DCLR_LABEL(V(OP_NEW_ARRAY_JUMBO));
DCLR_LABEL(V(OP_FILLED_NEW_ARRAY_JUMBO));
DCLR_LABEL(V(OP_IGET_JUMBO));
DCLR_LABEL(V(OP_IGET_WIDE_JUMBO));
DCLR_LABEL(V(OP_IGET_OBJECT_JUMBO));
DCLR_LABEL(V(OP_IGET_BOOLEAN_JUMBO));
DCLR_LABEL(V(OP_IGET_BYTE_JUMBO));
DCLR_LABEL(V(OP_IGET_CHAR_JUMBO));
DCLR_LABEL(V(OP_IGET_SHORT_JUMBO));
DCLR_LABEL(V(OP_IPUT_JUMBO));
DCLR_LABEL(V(OP_IPUT_WIDE_JUMBO));
DCLR_LABEL(V(OP_IPUT_OBJECT_JUMBO));
DCLR_LABEL(V(OP_IPUT_BOOLEAN_JUMBO));
DCLR_LABEL(V(OP_IPUT_BYTE_JUMBO));
DCLR_LABEL(V(OP_IPUT_CHAR_JUMBO));
DCLR_LABEL(V(OP_IPUT_SHORT_JUMBO));
DCLR_LABEL(V(OP_SGET_JUMBO));
DCLR_LABEL(V(OP_SGET_WIDE_JUMBO));
DCLR_LABEL(V(OP_SGET_OBJECT_JUMBO));
DCLR_LABEL(V(OP_SGET_BOOLEAN_JUMBO));
DCLR_LABEL(V(OP_SGET_BYTE_JUMBO));
DCLR_LABEL(V(OP_SGET_CHAR_JUMBO));
DCLR_LABEL(V(OP_SGET_SHORT_JUMBO));
DCLR_LABEL(V(OP_SPUT_JUMBO));
DCLR_LABEL(V(OP_SPUT_WIDE_JUMBO));
DCLR_LABEL(V(OP_SPUT_OBJECT_JUMBO));
DCLR_LABEL(V(OP_SPUT_BOOLEAN_JUMBO));
DCLR_LABEL(V(OP_SPUT_BYTE_JUMBO));
DCLR_LABEL(V(OP_SPUT_CHAR_JUMBO));
DCLR_LABEL(V(OP_SPUT_SHORT_JUMBO));
DCLR_LABEL(V(OP_INVOKE_VIRTUAL_JUMBO));
DCLR_LABEL(V(OP_INVOKE_SUPER_JUMBO));
DCLR_LABEL(V(OP_INVOKE_DIRECT_JUMBO));
DCLR_LABEL(V(OP_INVOKE_STATIC_JUMBO));
DCLR_LABEL(V(OP_INVOKE_INTERFACE_JUMBO));
DCLR_LABEL(V(OP_UNUSED_27FF));
DCLR_LABEL(V(OP_UNUSED_28FF));
DCLR_LABEL(V(OP_UNUSED_29FF));
DCLR_LABEL(V(OP_UNUSED_2AFF));
DCLR_LABEL(V(OP_UNUSED_2BFF));
DCLR_LABEL(V(OP_UNUSED_2CFF));
DCLR_LABEL(V(OP_UNUSED_2DFF));
DCLR_LABEL(V(OP_UNUSED_2EFF));
DCLR_LABEL(V(OP_UNUSED_2FFF));
DCLR_LABEL(V(OP_UNUSED_30FF));
DCLR_LABEL(V(OP_UNUSED_31FF));
DCLR_LABEL(V(OP_UNUSED_32FF));
DCLR_LABEL(V(OP_UNUSED_33FF));
DCLR_LABEL(V(OP_UNUSED_34FF));
DCLR_LABEL(V(OP_UNUSED_35FF));
DCLR_LABEL(V(OP_UNUSED_36FF));
DCLR_LABEL(V(OP_UNUSED_37FF));
DCLR_LABEL(V(OP_UNUSED_38FF));
DCLR_LABEL(V(OP_UNUSED_39FF));
DCLR_LABEL(V(OP_UNUSED_3AFF));
DCLR_LABEL(V(OP_UNUSED_3BFF));
DCLR_LABEL(V(OP_UNUSED_3CFF));
DCLR_LABEL(V(OP_UNUSED_3DFF));
DCLR_LABEL(V(OP_UNUSED_3EFF));
DCLR_LABEL(V(OP_UNUSED_3FFF));
DCLR_LABEL(V(OP_UNUSED_40FF));
DCLR_LABEL(V(OP_UNUSED_41FF));
DCLR_LABEL(V(OP_UNUSED_42FF));
DCLR_LABEL(V(OP_UNUSED_43FF));
DCLR_LABEL(V(OP_UNUSED_44FF));
DCLR_LABEL(V(OP_UNUSED_45FF));
DCLR_LABEL(V(OP_UNUSED_46FF));
DCLR_LABEL(V(OP_UNUSED_47FF));
DCLR_LABEL(V(OP_UNUSED_48FF));
DCLR_LABEL(V(OP_UNUSED_49FF));
DCLR_LABEL(V(OP_UNUSED_4AFF));
DCLR_LABEL(V(OP_UNUSED_4BFF));
DCLR_LABEL(V(OP_UNUSED_4CFF));
DCLR_LABEL(V(OP_UNUSED_4DFF));
DCLR_LABEL(V(OP_UNUSED_4EFF));
DCLR_LABEL(V(OP_UNUSED_4FFF));
DCLR_LABEL(V(OP_UNUSED_50FF));
DCLR_LABEL(V(OP_UNUSED_51FF));
DCLR_LABEL(V(OP_UNUSED_52FF));
DCLR_LABEL(V(OP_UNUSED_53FF));
DCLR_LABEL(V(OP_UNUSED_54FF));
DCLR_LABEL(V(OP_UNUSED_55FF));
DCLR_LABEL(V(OP_UNUSED_56FF));
DCLR_LABEL(V(OP_UNUSED_57FF));
DCLR_LABEL(V(OP_UNUSED_58FF));
DCLR_LABEL(V(OP_UNUSED_59FF));
DCLR_LABEL(V(OP_UNUSED_5AFF));
DCLR_LABEL(V(OP_UNUSED_5BFF));
DCLR_LABEL(V(OP_UNUSED_5CFF));
DCLR_LABEL(V(OP_UNUSED_5DFF));
DCLR_LABEL(V(OP_UNUSED_5EFF));
DCLR_LABEL(V(OP_UNUSED_5FFF));
DCLR_LABEL(V(OP_UNUSED_60FF));
DCLR_LABEL(V(OP_UNUSED_61FF));
DCLR_LABEL(V(OP_UNUSED_62FF));
DCLR_LABEL(V(OP_UNUSED_63FF));
DCLR_LABEL(V(OP_UNUSED_64FF));
DCLR_LABEL(V(OP_UNUSED_65FF));
DCLR_LABEL(V(OP_UNUSED_66FF));
DCLR_LABEL(V(OP_UNUSED_67FF));
DCLR_LABEL(V(OP_UNUSED_68FF));
DCLR_LABEL(V(OP_UNUSED_69FF));
DCLR_LABEL(V(OP_UNUSED_6AFF));
DCLR_LABEL(V(OP_UNUSED_6BFF));
DCLR_LABEL(V(OP_UNUSED_6CFF));
DCLR_LABEL(V(OP_UNUSED_6DFF));
DCLR_LABEL(V(OP_UNUSED_6EFF));
DCLR_LABEL(V(OP_UNUSED_6FFF));
DCLR_LABEL(V(OP_UNUSED_70FF));
DCLR_LABEL(V(OP_UNUSED_71FF));
DCLR_LABEL(V(OP_UNUSED_72FF));
DCLR_LABEL(V(OP_UNUSED_73FF));
DCLR_LABEL(V(OP_UNUSED_74FF));
DCLR_LABEL(V(OP_UNUSED_75FF));
DCLR_LABEL(V(OP_UNUSED_76FF));
DCLR_LABEL(V(OP_UNUSED_77FF));
DCLR_LABEL(V(OP_UNUSED_78FF));
DCLR_LABEL(V(OP_UNUSED_79FF));
DCLR_LABEL(V(OP_UNUSED_7AFF));
DCLR_LABEL(V(OP_UNUSED_7BFF));
DCLR_LABEL(V(OP_UNUSED_7CFF));
DCLR_LABEL(V(OP_UNUSED_7DFF));
DCLR_LABEL(V(OP_UNUSED_7EFF));
DCLR_LABEL(V(OP_UNUSED_7FFF));
DCLR_LABEL(V(OP_UNUSED_80FF));
DCLR_LABEL(V(OP_UNUSED_81FF));
DCLR_LABEL(V(OP_UNUSED_82FF));
DCLR_LABEL(V(OP_UNUSED_83FF));
DCLR_LABEL(V(OP_UNUSED_84FF));
DCLR_LABEL(V(OP_UNUSED_85FF));
DCLR_LABEL(V(OP_UNUSED_86FF));
DCLR_LABEL(V(OP_UNUSED_87FF));
DCLR_LABEL(V(OP_UNUSED_88FF));
DCLR_LABEL(V(OP_UNUSED_89FF));
DCLR_LABEL(V(OP_UNUSED_8AFF));
DCLR_LABEL(V(OP_UNUSED_8BFF));
DCLR_LABEL(V(OP_UNUSED_8CFF));
DCLR_LABEL(V(OP_UNUSED_8DFF));
DCLR_LABEL(V(OP_UNUSED_8EFF));
DCLR_LABEL(V(OP_UNUSED_8FFF));
DCLR_LABEL(V(OP_UNUSED_90FF));
DCLR_LABEL(V(OP_UNUSED_91FF));
DCLR_LABEL(V(OP_UNUSED_92FF));
DCLR_LABEL(V(OP_UNUSED_93FF));
DCLR_LABEL(V(OP_UNUSED_94FF));
DCLR_LABEL(V(OP_UNUSED_95FF));
DCLR_LABEL(V(OP_UNUSED_96FF));
DCLR_LABEL(V(OP_UNUSED_97FF));
DCLR_LABEL(V(OP_UNUSED_98FF));
DCLR_LABEL(V(OP_UNUSED_99FF));
DCLR_LABEL(V(OP_UNUSED_9AFF));
DCLR_LABEL(V(OP_UNUSED_9BFF));
DCLR_LABEL(V(OP_UNUSED_9CFF));
DCLR_LABEL(V(OP_UNUSED_9DFF));
DCLR_LABEL(V(OP_UNUSED_9EFF));
DCLR_LABEL(V(OP_UNUSED_9FFF));
DCLR_LABEL(V(OP_UNUSED_A0FF));
DCLR_LABEL(V(OP_UNUSED_A1FF));
DCLR_LABEL(V(OP_UNUSED_A2FF));
DCLR_LABEL(V(OP_UNUSED_A3FF));
DCLR_LABEL(V(OP_UNUSED_A4FF));
DCLR_LABEL(V(OP_UNUSED_A5FF));
DCLR_LABEL(V(OP_UNUSED_A6FF));
DCLR_LABEL(V(OP_UNUSED_A7FF));
DCLR_LABEL(V(OP_UNUSED_A8FF));
DCLR_LABEL(V(OP_UNUSED_A9FF));
DCLR_LABEL(V(OP_UNUSED_AAFF));
DCLR_LABEL(V(OP_UNUSED_ABFF));
DCLR_LABEL(V(OP_UNUSED_ACFF));
DCLR_LABEL(V(OP_UNUSED_ADFF));
DCLR_LABEL(V(OP_UNUSED_AEFF));
DCLR_LABEL(V(OP_UNUSED_AFFF));
DCLR_LABEL(V(OP_UNUSED_B0FF));
DCLR_LABEL(V(OP_UNUSED_B1FF));
DCLR_LABEL(V(OP_UNUSED_B2FF));
DCLR_LABEL(V(OP_UNUSED_B3FF));
DCLR_LABEL(V(OP_UNUSED_B4FF));
DCLR_LABEL(V(OP_UNUSED_B5FF));
DCLR_LABEL(V(OP_UNUSED_B6FF));
DCLR_LABEL(V(OP_UNUSED_B7FF));
DCLR_LABEL(V(OP_UNUSED_B8FF));
DCLR_LABEL(V(OP_UNUSED_B9FF));
DCLR_LABEL(V(OP_UNUSED_BAFF));
DCLR_LABEL(V(OP_UNUSED_BBFF));
DCLR_LABEL(V(OP_UNUSED_BCFF));
DCLR_LABEL(V(OP_UNUSED_BDFF));
DCLR_LABEL(V(OP_UNUSED_BEFF));
DCLR_LABEL(V(OP_UNUSED_BFFF));
DCLR_LABEL(V(OP_UNUSED_C0FF));
DCLR_LABEL(V(OP_UNUSED_C1FF));
DCLR_LABEL(V(OP_UNUSED_C2FF));
DCLR_LABEL(V(OP_UNUSED_C3FF));
DCLR_LABEL(V(OP_UNUSED_C4FF));
DCLR_LABEL(V(OP_UNUSED_C5FF));
DCLR_LABEL(V(OP_UNUSED_C6FF));
DCLR_LABEL(V(OP_UNUSED_C7FF));
DCLR_LABEL(V(OP_UNUSED_C8FF));
DCLR_LABEL(V(OP_UNUSED_C9FF));
DCLR_LABEL(V(OP_UNUSED_CAFF));
DCLR_LABEL(V(OP_UNUSED_CBFF));
DCLR_LABEL(V(OP_UNUSED_CCFF));
DCLR_LABEL(V(OP_UNUSED_CDFF));
DCLR_LABEL(V(OP_UNUSED_CEFF));
DCLR_LABEL(V(OP_UNUSED_CFFF));
DCLR_LABEL(V(OP_UNUSED_D0FF));
DCLR_LABEL(V(OP_UNUSED_D1FF));
DCLR_LABEL(V(OP_UNUSED_D2FF));
DCLR_LABEL(V(OP_UNUSED_D3FF));
DCLR_LABEL(V(OP_UNUSED_D4FF));
DCLR_LABEL(V(OP_UNUSED_D5FF));
DCLR_LABEL(V(OP_UNUSED_D6FF));
DCLR_LABEL(V(OP_UNUSED_D7FF));
DCLR_LABEL(V(OP_UNUSED_D8FF));
DCLR_LABEL(V(OP_UNUSED_D9FF));
DCLR_LABEL(V(OP_UNUSED_DAFF));
DCLR_LABEL(V(OP_UNUSED_DBFF));
DCLR_LABEL(V(OP_UNUSED_DCFF));
DCLR_LABEL(V(OP_UNUSED_DDFF));
DCLR_LABEL(V(OP_UNUSED_DEFF));
DCLR_LABEL(V(OP_UNUSED_DFFF));
DCLR_LABEL(V(OP_UNUSED_E0FF));
DCLR_LABEL(V(OP_UNUSED_E1FF));
DCLR_LABEL(V(OP_UNUSED_E2FF));
DCLR_LABEL(V(OP_UNUSED_E3FF));
DCLR_LABEL(V(OP_UNUSED_E4FF));
DCLR_LABEL(V(OP_UNUSED_E5FF));
DCLR_LABEL(V(OP_UNUSED_E6FF));
DCLR_LABEL(V(OP_UNUSED_E7FF));
DCLR_LABEL(V(OP_UNUSED_E8FF));
DCLR_LABEL(V(OP_UNUSED_E9FF));
DCLR_LABEL(V(OP_UNUSED_EAFF));
DCLR_LABEL(V(OP_UNUSED_EBFF));
DCLR_LABEL(V(OP_UNUSED_ECFF));
DCLR_LABEL(V(OP_UNUSED_EDFF));
DCLR_LABEL(V(OP_UNUSED_EEFF));
DCLR_LABEL(V(OP_UNUSED_EFFF));
DCLR_LABEL(V(OP_UNUSED_F0FF));
DCLR_LABEL(V(OP_UNUSED_F1FF));
DCLR_LABEL(V(OP_INVOKE_OBJECT_INIT_JUMBO));
DCLR_LABEL(V(OP_IGET_VOLATILE_JUMBO));
DCLR_LABEL(V(OP_IGET_WIDE_VOLATILE_JUMBO));
DCLR_LABEL(V(OP_IGET_OBJECT_VOLATILE_JUMBO));
DCLR_LABEL(V(OP_IPUT_VOLATILE_JUMBO));
DCLR_LABEL(V(OP_IPUT_WIDE_VOLATILE_JUMBO));
DCLR_LABEL(V(OP_IPUT_OBJECT_VOLATILE_JUMBO));
DCLR_LABEL(V(OP_SGET_VOLATILE_JUMBO));
DCLR_LABEL(V(OP_SGET_WIDE_VOLATILE_JUMBO));
DCLR_LABEL(V(OP_SGET_OBJECT_VOLATILE_JUMBO));
DCLR_LABEL(V(OP_SPUT_VOLATILE_JUMBO));
DCLR_LABEL(V(OP_SPUT_WIDE_VOLATILE_JUMBO));
DCLR_LABEL(V(OP_SPUT_OBJECT_VOLATILE_JUMBO));
DCLR_LABEL(V(OP_THROW_VERIFICATION_ERROR_JUMBO));
    // END(libdex-opcode-enum)

#define NEW_GOTO_TABLE()	\
{ \
	SAVE_LABEL(OP_NOP);    \
	SAVE_LABEL(OP_MOVE);    \
	SAVE_LABEL(OP_MOVE_FROM16);    \
	SAVE_LABEL(OP_MOVE_16);    \
	SAVE_LABEL(OP_MOVE_WIDE);    \
	SAVE_LABEL(OP_MOVE_WIDE_FROM16);    \
	SAVE_LABEL(OP_MOVE_WIDE_16);    \
	SAVE_LABEL(OP_MOVE_OBJECT);    \
	SAVE_LABEL(OP_MOVE_OBJECT_FROM16);    \
	SAVE_LABEL(OP_MOVE_OBJECT_16);    \
	SAVE_LABEL(OP_MOVE_RESULT);    \
	SAVE_LABEL(OP_MOVE_RESULT_WIDE);    \
	SAVE_LABEL(OP_MOVE_RESULT_OBJECT);    \
	SAVE_LABEL(OP_MOVE_EXCEPTION);    \
	SAVE_LABEL(OP_RETURN_VOID);    \
	SAVE_LABEL(OP_RETURN);    \
	SAVE_LABEL(OP_RETURN_WIDE);    \
	SAVE_LABEL(OP_RETURN_OBJECT);    \
	SAVE_LABEL(OP_CONST_4);    \
	SAVE_LABEL(OP_CONST_16);    \
	SAVE_LABEL(OP_CONST);    \
	SAVE_LABEL(OP_CONST_HIGH16);    \
	SAVE_LABEL(OP_CONST_WIDE_16);    \
	SAVE_LABEL(OP_CONST_WIDE_32);    \
	SAVE_LABEL(OP_CONST_WIDE);    \
	SAVE_LABEL(OP_CONST_WIDE_HIGH16);    \
	SAVE_LABEL(OP_CONST_STRING);    \
	SAVE_LABEL(OP_CONST_STRING_JUMBO);    \
	SAVE_LABEL(OP_CONST_CLASS);    \
	SAVE_LABEL(OP_MONITOR_ENTER);    \
	SAVE_LABEL(OP_MONITOR_EXIT);    \
	SAVE_LABEL(OP_CHECK_CAST);    \
	SAVE_LABEL(OP_INSTANCE_OF);    \
	SAVE_LABEL(OP_ARRAY_LENGTH);    \
	SAVE_LABEL(OP_NEW_INSTANCE);    \
	SAVE_LABEL(OP_NEW_ARRAY);    \
	SAVE_LABEL(OP_FILLED_NEW_ARRAY);    \
	SAVE_LABEL(OP_FILLED_NEW_ARRAY_RANGE);    \
	SAVE_LABEL(OP_FILL_ARRAY_DATA);    \
	SAVE_LABEL(OP_THROW);    \
	SAVE_LABEL(OP_GOTO);    \
	SAVE_LABEL(OP_GOTO_16);    \
	SAVE_LABEL(OP_GOTO_32);    \
	SAVE_LABEL(OP_PACKED_SWITCH);    \
	SAVE_LABEL(OP_SPARSE_SWITCH);    \
	SAVE_LABEL(OP_CMPL_FLOAT);    \
	SAVE_LABEL(OP_CMPG_FLOAT);    \
	SAVE_LABEL(OP_CMPL_DOUBLE);    \
	SAVE_LABEL(OP_CMPG_DOUBLE);    \
	SAVE_LABEL(OP_CMP_LONG);    \
	SAVE_LABEL(OP_IF_EQ);    \
	SAVE_LABEL(OP_IF_NE);    \
	SAVE_LABEL(OP_IF_LT);    \
	SAVE_LABEL(OP_IF_GE);    \
	SAVE_LABEL(OP_IF_GT);    \
	SAVE_LABEL(OP_IF_LE);    \
	SAVE_LABEL(OP_IF_EQZ);    \
	SAVE_LABEL(OP_IF_NEZ);    \
	SAVE_LABEL(OP_IF_LTZ);    \
	SAVE_LABEL(OP_IF_GEZ);    \
	SAVE_LABEL(OP_IF_GTZ);    \
	SAVE_LABEL(OP_IF_LEZ);    \
	SAVE_LABEL(OP_UNUSED_3E);    \
	SAVE_LABEL(OP_UNUSED_3F);    \
	SAVE_LABEL(OP_UNUSED_40);    \
	SAVE_LABEL(OP_UNUSED_41);    \
	SAVE_LABEL(OP_UNUSED_42);    \
	SAVE_LABEL(OP_UNUSED_43);    \
	SAVE_LABEL(OP_AGET);    \
	SAVE_LABEL(OP_AGET_WIDE);    \
	SAVE_LABEL(OP_AGET_OBJECT);    \
	SAVE_LABEL(OP_AGET_BOOLEAN);    \
	SAVE_LABEL(OP_AGET_BYTE);    \
	SAVE_LABEL(OP_AGET_CHAR);    \
	SAVE_LABEL(OP_AGET_SHORT);    \
	SAVE_LABEL(OP_APUT);    \
	SAVE_LABEL(OP_APUT_WIDE);    \
	SAVE_LABEL(OP_APUT_OBJECT);    \
	SAVE_LABEL(OP_APUT_BOOLEAN);    \
	SAVE_LABEL(OP_APUT_BYTE);    \
	SAVE_LABEL(OP_APUT_CHAR);    \
	SAVE_LABEL(OP_APUT_SHORT);    \
	SAVE_LABEL(OP_IGET);    \
	SAVE_LABEL(OP_IGET_WIDE);    \
	SAVE_LABEL(OP_IGET_OBJECT);    \
	SAVE_LABEL(OP_IGET_BOOLEAN);    \
	SAVE_LABEL(OP_IGET_BYTE);    \
	SAVE_LABEL(OP_IGET_CHAR);    \
	SAVE_LABEL(OP_IGET_SHORT);    \
	SAVE_LABEL(OP_IPUT);    \
	SAVE_LABEL(OP_IPUT_WIDE);    \
	SAVE_LABEL(OP_IPUT_OBJECT);    \
	SAVE_LABEL(OP_IPUT_BOOLEAN);    \
	SAVE_LABEL(OP_IPUT_BYTE);    \
	SAVE_LABEL(OP_IPUT_CHAR);    \
	SAVE_LABEL(OP_IPUT_SHORT);    \
	SAVE_LABEL(OP_SGET);    \
	SAVE_LABEL(OP_SGET_WIDE);    \
	SAVE_LABEL(OP_SGET_OBJECT);    \
	SAVE_LABEL(OP_SGET_BOOLEAN);    \
	SAVE_LABEL(OP_SGET_BYTE);    \
	SAVE_LABEL(OP_SGET_CHAR);    \
	SAVE_LABEL(OP_SGET_SHORT);    \
	SAVE_LABEL(OP_SPUT);    \
	SAVE_LABEL(OP_SPUT_WIDE);    \
	SAVE_LABEL(OP_SPUT_OBJECT);    \
	SAVE_LABEL(OP_SPUT_BOOLEAN);    \
	SAVE_LABEL(OP_SPUT_BYTE);    \
	SAVE_LABEL(OP_SPUT_CHAR);    \
	SAVE_LABEL(OP_SPUT_SHORT);    \
	SAVE_LABEL(OP_INVOKE_VIRTUAL);    \
	SAVE_LABEL(OP_INVOKE_SUPER);    \
	SAVE_LABEL(OP_INVOKE_DIRECT);    \
	SAVE_LABEL(OP_INVOKE_STATIC);    \
	SAVE_LABEL(OP_INVOKE_INTERFACE);    \
	SAVE_LABEL(OP_UNUSED_73);    \
	SAVE_LABEL(OP_INVOKE_VIRTUAL_RANGE);    \
	SAVE_LABEL(OP_INVOKE_SUPER_RANGE);    \
	SAVE_LABEL(OP_INVOKE_DIRECT_RANGE);    \
	SAVE_LABEL(OP_INVOKE_STATIC_RANGE);    \
	SAVE_LABEL(OP_INVOKE_INTERFACE_RANGE);    \
	SAVE_LABEL(OP_UNUSED_79);    \
	SAVE_LABEL(OP_UNUSED_7A);    \
	SAVE_LABEL(OP_NEG_INT);    \
	SAVE_LABEL(OP_NOT_INT);    \
	SAVE_LABEL(OP_NEG_LONG);    \
	SAVE_LABEL(OP_NOT_LONG);    \
	SAVE_LABEL(OP_NEG_FLOAT);    \
	SAVE_LABEL(OP_NEG_DOUBLE);    \
	SAVE_LABEL(OP_INT_TO_LONG);    \
	SAVE_LABEL(OP_INT_TO_FLOAT);    \
	SAVE_LABEL(OP_INT_TO_DOUBLE);    \
	SAVE_LABEL(OP_LONG_TO_INT);    \
	SAVE_LABEL(OP_LONG_TO_FLOAT);    \
	SAVE_LABEL(OP_LONG_TO_DOUBLE);    \
	SAVE_LABEL(OP_FLOAT_TO_INT);    \
	SAVE_LABEL(OP_FLOAT_TO_LONG);    \
	SAVE_LABEL(OP_FLOAT_TO_DOUBLE);    \
	SAVE_LABEL(OP_DOUBLE_TO_INT);    \
	SAVE_LABEL(OP_DOUBLE_TO_LONG);    \
	SAVE_LABEL(OP_DOUBLE_TO_FLOAT);    \
	SAVE_LABEL(OP_INT_TO_BYTE);    \
	SAVE_LABEL(OP_INT_TO_CHAR);    \
	SAVE_LABEL(OP_INT_TO_SHORT);    \
	SAVE_LABEL(OP_ADD_INT);    \
	SAVE_LABEL(OP_SUB_INT);    \
	SAVE_LABEL(OP_MUL_INT);    \
	SAVE_LABEL(OP_DIV_INT);    \
	SAVE_LABEL(OP_REM_INT);    \
	SAVE_LABEL(OP_AND_INT);    \
	SAVE_LABEL(OP_OR_INT);    \
	SAVE_LABEL(OP_XOR_INT);    \
	SAVE_LABEL(OP_SHL_INT);    \
	SAVE_LABEL(OP_SHR_INT);    \
	SAVE_LABEL(OP_USHR_INT);    \
	SAVE_LABEL(OP_ADD_LONG);    \
	SAVE_LABEL(OP_SUB_LONG);    \
	SAVE_LABEL(OP_MUL_LONG);    \
	SAVE_LABEL(OP_DIV_LONG);    \
	SAVE_LABEL(OP_REM_LONG);    \
	SAVE_LABEL(OP_AND_LONG);    \
	SAVE_LABEL(OP_OR_LONG);    \
	SAVE_LABEL(OP_XOR_LONG);    \
	SAVE_LABEL(OP_SHL_LONG);    \
	SAVE_LABEL(OP_SHR_LONG);    \
	SAVE_LABEL(OP_USHR_LONG);    \
	SAVE_LABEL(OP_ADD_FLOAT);    \
	SAVE_LABEL(OP_SUB_FLOAT);    \
	SAVE_LABEL(OP_MUL_FLOAT);    \
	SAVE_LABEL(OP_DIV_FLOAT);    \
	SAVE_LABEL(OP_REM_FLOAT);    \
	SAVE_LABEL(OP_ADD_DOUBLE);    \
	SAVE_LABEL(OP_SUB_DOUBLE);    \
	SAVE_LABEL(OP_MUL_DOUBLE);    \
	SAVE_LABEL(OP_DIV_DOUBLE);    \
	SAVE_LABEL(OP_REM_DOUBLE);    \
	SAVE_LABEL(OP_ADD_INT_2ADDR);    \
	SAVE_LABEL(OP_SUB_INT_2ADDR);    \
	SAVE_LABEL(OP_MUL_INT_2ADDR);    \
	SAVE_LABEL(OP_DIV_INT_2ADDR);    \
	SAVE_LABEL(OP_REM_INT_2ADDR);    \
	SAVE_LABEL(OP_AND_INT_2ADDR);    \
	SAVE_LABEL(OP_OR_INT_2ADDR);    \
	SAVE_LABEL(OP_XOR_INT_2ADDR);    \
	SAVE_LABEL(OP_SHL_INT_2ADDR);    \
	SAVE_LABEL(OP_SHR_INT_2ADDR);    \
	SAVE_LABEL(OP_USHR_INT_2ADDR);    \
	SAVE_LABEL(OP_ADD_LONG_2ADDR);    \
	SAVE_LABEL(OP_SUB_LONG_2ADDR);    \
	SAVE_LABEL(OP_MUL_LONG_2ADDR);    \
	SAVE_LABEL(OP_DIV_LONG_2ADDR);    \
	SAVE_LABEL(OP_REM_LONG_2ADDR);    \
	SAVE_LABEL(OP_AND_LONG_2ADDR);    \
	SAVE_LABEL(OP_OR_LONG_2ADDR);    \
	SAVE_LABEL(OP_XOR_LONG_2ADDR);    \
	SAVE_LABEL(OP_SHL_LONG_2ADDR);    \
	SAVE_LABEL(OP_SHR_LONG_2ADDR);    \
	SAVE_LABEL(OP_USHR_LONG_2ADDR);    \
	SAVE_LABEL(OP_ADD_FLOAT_2ADDR);    \
	SAVE_LABEL(OP_SUB_FLOAT_2ADDR);    \
	SAVE_LABEL(OP_MUL_FLOAT_2ADDR);    \
	SAVE_LABEL(OP_DIV_FLOAT_2ADDR);    \
	SAVE_LABEL(OP_REM_FLOAT_2ADDR);    \
	SAVE_LABEL(OP_ADD_DOUBLE_2ADDR);    \
	SAVE_LABEL(OP_SUB_DOUBLE_2ADDR);    \
	SAVE_LABEL(OP_MUL_DOUBLE_2ADDR);    \
	SAVE_LABEL(OP_DIV_DOUBLE_2ADDR);    \
	SAVE_LABEL(OP_REM_DOUBLE_2ADDR);    \
	SAVE_LABEL(OP_ADD_INT_LIT16);    \
	SAVE_LABEL(OP_RSUB_INT);    \
	SAVE_LABEL(OP_MUL_INT_LIT16);    \
	SAVE_LABEL(OP_DIV_INT_LIT16);    \
	SAVE_LABEL(OP_REM_INT_LIT16);    \
	SAVE_LABEL(OP_AND_INT_LIT16);    \
	SAVE_LABEL(OP_OR_INT_LIT16);    \
	SAVE_LABEL(OP_XOR_INT_LIT16);    \
	SAVE_LABEL(OP_ADD_INT_LIT8);    \
	SAVE_LABEL(OP_RSUB_INT_LIT8);    \
	SAVE_LABEL(OP_MUL_INT_LIT8);    \
	SAVE_LABEL(OP_DIV_INT_LIT8);    \
	SAVE_LABEL(OP_REM_INT_LIT8);    \
	SAVE_LABEL(OP_AND_INT_LIT8);    \
	SAVE_LABEL(OP_OR_INT_LIT8);    \
	SAVE_LABEL(OP_XOR_INT_LIT8);    \
	SAVE_LABEL(OP_SHL_INT_LIT8);    \
	SAVE_LABEL(OP_SHR_INT_LIT8);    \
	SAVE_LABEL(OP_USHR_INT_LIT8);    \
	SAVE_LABEL(OP_IGET_VOLATILE);    \
	SAVE_LABEL(OP_IPUT_VOLATILE);    \
	SAVE_LABEL(OP_SGET_VOLATILE);    \
	SAVE_LABEL(OP_SPUT_VOLATILE);    \
	SAVE_LABEL(OP_IGET_OBJECT_VOLATILE);    \
	SAVE_LABEL(OP_IGET_WIDE_VOLATILE);    \
	SAVE_LABEL(OP_IPUT_WIDE_VOLATILE);    \
	SAVE_LABEL(OP_SGET_WIDE_VOLATILE);    \
	SAVE_LABEL(OP_SPUT_WIDE_VOLATILE);    \
	SAVE_LABEL(OP_BREAKPOINT);    \
	SAVE_LABEL(OP_THROW_VERIFICATION_ERROR);    \
	SAVE_LABEL(OP_EXECUTE_INLINE);    \
	SAVE_LABEL(OP_EXECUTE_INLINE_RANGE);    \
	SAVE_LABEL(OP_INVOKE_OBJECT_INIT_RANGE);    \
	SAVE_LABEL(OP_RETURN_VOID_BARRIER);    \
	SAVE_LABEL(OP_IGET_QUICK);    \
	SAVE_LABEL(OP_IGET_WIDE_QUICK);    \
	SAVE_LABEL(OP_IGET_OBJECT_QUICK);    \
	SAVE_LABEL(OP_IPUT_QUICK);    \
	SAVE_LABEL(OP_IPUT_WIDE_QUICK);    \
	SAVE_LABEL(OP_IPUT_OBJECT_QUICK);    \
	SAVE_LABEL(OP_INVOKE_VIRTUAL_QUICK);    \
	SAVE_LABEL(OP_INVOKE_VIRTUAL_QUICK_RANGE);    \
	SAVE_LABEL(OP_INVOKE_SUPER_QUICK);    \
	SAVE_LABEL(OP_INVOKE_SUPER_QUICK_RANGE);    \
	SAVE_LABEL(OP_IPUT_OBJECT_VOLATILE);    \
	SAVE_LABEL(OP_SGET_OBJECT_VOLATILE);    \
	SAVE_LABEL(OP_SPUT_OBJECT_VOLATILE);    \
	SAVE_LABEL(OP_DISPATCH_FF);    \
	SAVE_LABEL(OP_CONST_CLASS_JUMBO);    \
	SAVE_LABEL(OP_CHECK_CAST_JUMBO);    \
	SAVE_LABEL(OP_INSTANCE_OF_JUMBO);    \
	SAVE_LABEL(OP_NEW_INSTANCE_JUMBO);    \
	SAVE_LABEL(OP_NEW_ARRAY_JUMBO);    \
	SAVE_LABEL(OP_FILLED_NEW_ARRAY_JUMBO);    \
	SAVE_LABEL(OP_IGET_JUMBO);    \
	SAVE_LABEL(OP_IGET_WIDE_JUMBO);    \
	SAVE_LABEL(OP_IGET_OBJECT_JUMBO);    \
	SAVE_LABEL(OP_IGET_BOOLEAN_JUMBO);    \
	SAVE_LABEL(OP_IGET_BYTE_JUMBO);    \
	SAVE_LABEL(OP_IGET_CHAR_JUMBO);    \
	SAVE_LABEL(OP_IGET_SHORT_JUMBO);    \
	SAVE_LABEL(OP_IPUT_JUMBO);    \
	SAVE_LABEL(OP_IPUT_WIDE_JUMBO);    \
	SAVE_LABEL(OP_IPUT_OBJECT_JUMBO);    \
	SAVE_LABEL(OP_IPUT_BOOLEAN_JUMBO);    \
	SAVE_LABEL(OP_IPUT_BYTE_JUMBO);    \
	SAVE_LABEL(OP_IPUT_CHAR_JUMBO);    \
	SAVE_LABEL(OP_IPUT_SHORT_JUMBO);    \
	SAVE_LABEL(OP_SGET_JUMBO);    \
	SAVE_LABEL(OP_SGET_WIDE_JUMBO);    \
	SAVE_LABEL(OP_SGET_OBJECT_JUMBO);    \
	SAVE_LABEL(OP_SGET_BOOLEAN_JUMBO);    \
	SAVE_LABEL(OP_SGET_BYTE_JUMBO);    \
	SAVE_LABEL(OP_SGET_CHAR_JUMBO);    \
	SAVE_LABEL(OP_SGET_SHORT_JUMBO);    \
	SAVE_LABEL(OP_SPUT_JUMBO);    \
	SAVE_LABEL(OP_SPUT_WIDE_JUMBO);    \
	SAVE_LABEL(OP_SPUT_OBJECT_JUMBO);    \
	SAVE_LABEL(OP_SPUT_BOOLEAN_JUMBO);    \
	SAVE_LABEL(OP_SPUT_BYTE_JUMBO);    \
	SAVE_LABEL(OP_SPUT_CHAR_JUMBO);    \
	SAVE_LABEL(OP_SPUT_SHORT_JUMBO);    \
	SAVE_LABEL(OP_INVOKE_VIRTUAL_JUMBO);    \
	SAVE_LABEL(OP_INVOKE_SUPER_JUMBO);    \
	SAVE_LABEL(OP_INVOKE_DIRECT_JUMBO);    \
	SAVE_LABEL(OP_INVOKE_STATIC_JUMBO);    \
	SAVE_LABEL(OP_INVOKE_INTERFACE_JUMBO);    \
	SAVE_LABEL(OP_UNUSED_27FF);    \
	SAVE_LABEL(OP_UNUSED_28FF);    \
	SAVE_LABEL(OP_UNUSED_29FF);    \
	SAVE_LABEL(OP_UNUSED_2AFF);    \
	SAVE_LABEL(OP_UNUSED_2BFF);    \
	SAVE_LABEL(OP_UNUSED_2CFF);    \
	SAVE_LABEL(OP_UNUSED_2DFF);    \
	SAVE_LABEL(OP_UNUSED_2EFF);    \
	SAVE_LABEL(OP_UNUSED_2FFF);    \
	SAVE_LABEL(OP_UNUSED_30FF);    \
	SAVE_LABEL(OP_UNUSED_31FF);    \
	SAVE_LABEL(OP_UNUSED_32FF);    \
	SAVE_LABEL(OP_UNUSED_33FF);    \
	SAVE_LABEL(OP_UNUSED_34FF);    \
	SAVE_LABEL(OP_UNUSED_35FF);    \
	SAVE_LABEL(OP_UNUSED_36FF);    \
	SAVE_LABEL(OP_UNUSED_37FF);    \
	SAVE_LABEL(OP_UNUSED_38FF);    \
	SAVE_LABEL(OP_UNUSED_39FF);    \
	SAVE_LABEL(OP_UNUSED_3AFF);    \
	SAVE_LABEL(OP_UNUSED_3BFF);    \
	SAVE_LABEL(OP_UNUSED_3CFF);    \
	SAVE_LABEL(OP_UNUSED_3DFF);    \
	SAVE_LABEL(OP_UNUSED_3EFF);    \
	SAVE_LABEL(OP_UNUSED_3FFF);    \
	SAVE_LABEL(OP_UNUSED_40FF);    \
	SAVE_LABEL(OP_UNUSED_41FF);    \
	SAVE_LABEL(OP_UNUSED_42FF);    \
	SAVE_LABEL(OP_UNUSED_43FF);    \
	SAVE_LABEL(OP_UNUSED_44FF);    \
	SAVE_LABEL(OP_UNUSED_45FF);    \
	SAVE_LABEL(OP_UNUSED_46FF);    \
	SAVE_LABEL(OP_UNUSED_47FF);    \
	SAVE_LABEL(OP_UNUSED_48FF);    \
	SAVE_LABEL(OP_UNUSED_49FF);    \
	SAVE_LABEL(OP_UNUSED_4AFF);    \
	SAVE_LABEL(OP_UNUSED_4BFF);    \
	SAVE_LABEL(OP_UNUSED_4CFF);    \
	SAVE_LABEL(OP_UNUSED_4DFF);    \
	SAVE_LABEL(OP_UNUSED_4EFF);    \
	SAVE_LABEL(OP_UNUSED_4FFF);    \
	SAVE_LABEL(OP_UNUSED_50FF);    \
	SAVE_LABEL(OP_UNUSED_51FF);    \
	SAVE_LABEL(OP_UNUSED_52FF);    \
	SAVE_LABEL(OP_UNUSED_53FF);    \
	SAVE_LABEL(OP_UNUSED_54FF);    \
	SAVE_LABEL(OP_UNUSED_55FF);    \
	SAVE_LABEL(OP_UNUSED_56FF);    \
	SAVE_LABEL(OP_UNUSED_57FF);    \
	SAVE_LABEL(OP_UNUSED_58FF);    \
	SAVE_LABEL(OP_UNUSED_59FF);    \
	SAVE_LABEL(OP_UNUSED_5AFF);    \
	SAVE_LABEL(OP_UNUSED_5BFF);    \
	SAVE_LABEL(OP_UNUSED_5CFF);    \
	SAVE_LABEL(OP_UNUSED_5DFF);    \
	SAVE_LABEL(OP_UNUSED_5EFF);    \
	SAVE_LABEL(OP_UNUSED_5FFF);    \
	SAVE_LABEL(OP_UNUSED_60FF);    \
	SAVE_LABEL(OP_UNUSED_61FF);    \
	SAVE_LABEL(OP_UNUSED_62FF);    \
	SAVE_LABEL(OP_UNUSED_63FF);    \
	SAVE_LABEL(OP_UNUSED_64FF);    \
	SAVE_LABEL(OP_UNUSED_65FF);    \
	SAVE_LABEL(OP_UNUSED_66FF);    \
	SAVE_LABEL(OP_UNUSED_67FF);    \
	SAVE_LABEL(OP_UNUSED_68FF);    \
	SAVE_LABEL(OP_UNUSED_69FF);    \
	SAVE_LABEL(OP_UNUSED_6AFF);    \
	SAVE_LABEL(OP_UNUSED_6BFF);    \
	SAVE_LABEL(OP_UNUSED_6CFF);    \
	SAVE_LABEL(OP_UNUSED_6DFF);    \
	SAVE_LABEL(OP_UNUSED_6EFF);    \
	SAVE_LABEL(OP_UNUSED_6FFF);    \
	SAVE_LABEL(OP_UNUSED_70FF);    \
	SAVE_LABEL(OP_UNUSED_71FF);    \
	SAVE_LABEL(OP_UNUSED_72FF);    \
	SAVE_LABEL(OP_UNUSED_73FF);    \
	SAVE_LABEL(OP_UNUSED_74FF);    \
	SAVE_LABEL(OP_UNUSED_75FF);    \
	SAVE_LABEL(OP_UNUSED_76FF);    \
	SAVE_LABEL(OP_UNUSED_77FF);    \
	SAVE_LABEL(OP_UNUSED_78FF);    \
	SAVE_LABEL(OP_UNUSED_79FF);    \
	SAVE_LABEL(OP_UNUSED_7AFF);    \
	SAVE_LABEL(OP_UNUSED_7BFF);    \
	SAVE_LABEL(OP_UNUSED_7CFF);    \
	SAVE_LABEL(OP_UNUSED_7DFF);    \
	SAVE_LABEL(OP_UNUSED_7EFF);    \
	SAVE_LABEL(OP_UNUSED_7FFF);    \
	SAVE_LABEL(OP_UNUSED_80FF);    \
	SAVE_LABEL(OP_UNUSED_81FF);    \
	SAVE_LABEL(OP_UNUSED_82FF);    \
	SAVE_LABEL(OP_UNUSED_83FF);    \
	SAVE_LABEL(OP_UNUSED_84FF);    \
	SAVE_LABEL(OP_UNUSED_85FF);    \
	SAVE_LABEL(OP_UNUSED_86FF);    \
	SAVE_LABEL(OP_UNUSED_87FF);    \
	SAVE_LABEL(OP_UNUSED_88FF);    \
	SAVE_LABEL(OP_UNUSED_89FF);    \
	SAVE_LABEL(OP_UNUSED_8AFF);    \
	SAVE_LABEL(OP_UNUSED_8BFF);    \
	SAVE_LABEL(OP_UNUSED_8CFF);    \
	SAVE_LABEL(OP_UNUSED_8DFF);    \
	SAVE_LABEL(OP_UNUSED_8EFF);    \
	SAVE_LABEL(OP_UNUSED_8FFF);    \
	SAVE_LABEL(OP_UNUSED_90FF);    \
	SAVE_LABEL(OP_UNUSED_91FF);    \
	SAVE_LABEL(OP_UNUSED_92FF);    \
	SAVE_LABEL(OP_UNUSED_93FF);    \
	SAVE_LABEL(OP_UNUSED_94FF);    \
	SAVE_LABEL(OP_UNUSED_95FF);    \
	SAVE_LABEL(OP_UNUSED_96FF);    \
	SAVE_LABEL(OP_UNUSED_97FF);    \
	SAVE_LABEL(OP_UNUSED_98FF);    \
	SAVE_LABEL(OP_UNUSED_99FF);    \
	SAVE_LABEL(OP_UNUSED_9AFF);    \
	SAVE_LABEL(OP_UNUSED_9BFF);    \
	SAVE_LABEL(OP_UNUSED_9CFF);    \
	SAVE_LABEL(OP_UNUSED_9DFF);    \
	SAVE_LABEL(OP_UNUSED_9EFF);    \
	SAVE_LABEL(OP_UNUSED_9FFF);    \
	SAVE_LABEL(OP_UNUSED_A0FF);    \
	SAVE_LABEL(OP_UNUSED_A1FF);    \
	SAVE_LABEL(OP_UNUSED_A2FF);    \
	SAVE_LABEL(OP_UNUSED_A3FF);    \
	SAVE_LABEL(OP_UNUSED_A4FF);    \
	SAVE_LABEL(OP_UNUSED_A5FF);    \
	SAVE_LABEL(OP_UNUSED_A6FF);    \
	SAVE_LABEL(OP_UNUSED_A7FF);    \
	SAVE_LABEL(OP_UNUSED_A8FF);    \
	SAVE_LABEL(OP_UNUSED_A9FF);    \
	SAVE_LABEL(OP_UNUSED_AAFF);    \
	SAVE_LABEL(OP_UNUSED_ABFF);    \
	SAVE_LABEL(OP_UNUSED_ACFF);    \
	SAVE_LABEL(OP_UNUSED_ADFF);    \
	SAVE_LABEL(OP_UNUSED_AEFF);    \
	SAVE_LABEL(OP_UNUSED_AFFF);    \
	SAVE_LABEL(OP_UNUSED_B0FF);    \
	SAVE_LABEL(OP_UNUSED_B1FF);    \
	SAVE_LABEL(OP_UNUSED_B2FF);    \
	SAVE_LABEL(OP_UNUSED_B3FF);    \
	SAVE_LABEL(OP_UNUSED_B4FF);    \
	SAVE_LABEL(OP_UNUSED_B5FF);    \
	SAVE_LABEL(OP_UNUSED_B6FF);    \
	SAVE_LABEL(OP_UNUSED_B7FF);    \
	SAVE_LABEL(OP_UNUSED_B8FF);    \
	SAVE_LABEL(OP_UNUSED_B9FF);    \
	SAVE_LABEL(OP_UNUSED_BAFF);    \
	SAVE_LABEL(OP_UNUSED_BBFF);    \
	SAVE_LABEL(OP_UNUSED_BCFF);    \
	SAVE_LABEL(OP_UNUSED_BDFF);    \
	SAVE_LABEL(OP_UNUSED_BEFF);    \
	SAVE_LABEL(OP_UNUSED_BFFF);    \
	SAVE_LABEL(OP_UNUSED_C0FF);    \
	SAVE_LABEL(OP_UNUSED_C1FF);    \
	SAVE_LABEL(OP_UNUSED_C2FF);    \
	SAVE_LABEL(OP_UNUSED_C3FF);    \
	SAVE_LABEL(OP_UNUSED_C4FF);    \
	SAVE_LABEL(OP_UNUSED_C5FF);    \
	SAVE_LABEL(OP_UNUSED_C6FF);    \
	SAVE_LABEL(OP_UNUSED_C7FF);    \
	SAVE_LABEL(OP_UNUSED_C8FF);    \
	SAVE_LABEL(OP_UNUSED_C9FF);    \
	SAVE_LABEL(OP_UNUSED_CAFF);    \
	SAVE_LABEL(OP_UNUSED_CBFF);    \
	SAVE_LABEL(OP_UNUSED_CCFF);    \
	SAVE_LABEL(OP_UNUSED_CDFF);    \
	SAVE_LABEL(OP_UNUSED_CEFF);    \
	SAVE_LABEL(OP_UNUSED_CFFF);    \
	SAVE_LABEL(OP_UNUSED_D0FF);    \
	SAVE_LABEL(OP_UNUSED_D1FF);    \
	SAVE_LABEL(OP_UNUSED_D2FF);    \
	SAVE_LABEL(OP_UNUSED_D3FF);    \
	SAVE_LABEL(OP_UNUSED_D4FF);    \
	SAVE_LABEL(OP_UNUSED_D5FF);    \
	SAVE_LABEL(OP_UNUSED_D6FF);    \
	SAVE_LABEL(OP_UNUSED_D7FF);    \
	SAVE_LABEL(OP_UNUSED_D8FF);    \
	SAVE_LABEL(OP_UNUSED_D9FF);    \
	SAVE_LABEL(OP_UNUSED_DAFF);    \
	SAVE_LABEL(OP_UNUSED_DBFF);    \
	SAVE_LABEL(OP_UNUSED_DCFF);    \
	SAVE_LABEL(OP_UNUSED_DDFF);    \
	SAVE_LABEL(OP_UNUSED_DEFF);    \
	SAVE_LABEL(OP_UNUSED_DFFF);    \
	SAVE_LABEL(OP_UNUSED_E0FF);    \
	SAVE_LABEL(OP_UNUSED_E1FF);    \
	SAVE_LABEL(OP_UNUSED_E2FF);    \
	SAVE_LABEL(OP_UNUSED_E3FF);    \
	SAVE_LABEL(OP_UNUSED_E4FF);    \
	SAVE_LABEL(OP_UNUSED_E5FF);    \
	SAVE_LABEL(OP_UNUSED_E6FF);    \
	SAVE_LABEL(OP_UNUSED_E7FF);    \
	SAVE_LABEL(OP_UNUSED_E8FF);    \
	SAVE_LABEL(OP_UNUSED_E9FF);    \
	SAVE_LABEL(OP_UNUSED_EAFF);    \
	SAVE_LABEL(OP_UNUSED_EBFF);    \
	SAVE_LABEL(OP_UNUSED_ECFF);    \
	SAVE_LABEL(OP_UNUSED_EDFF);    \
	SAVE_LABEL(OP_UNUSED_EEFF);    \
	SAVE_LABEL(OP_UNUSED_EFFF);    \
	SAVE_LABEL(OP_UNUSED_F0FF);    \
	SAVE_LABEL(OP_UNUSED_F1FF);    \
	SAVE_LABEL(OP_INVOKE_OBJECT_INIT_JUMBO);    \
	SAVE_LABEL(OP_IGET_VOLATILE_JUMBO);    \
	SAVE_LABEL(OP_IGET_WIDE_VOLATILE_JUMBO);    \
	SAVE_LABEL(OP_IGET_OBJECT_VOLATILE_JUMBO);    \
	SAVE_LABEL(OP_IPUT_VOLATILE_JUMBO);    \
	SAVE_LABEL(OP_IPUT_WIDE_VOLATILE_JUMBO);    \
	SAVE_LABEL(OP_IPUT_OBJECT_VOLATILE_JUMBO);    \
	SAVE_LABEL(OP_SGET_VOLATILE_JUMBO);    \
	SAVE_LABEL(OP_SGET_WIDE_VOLATILE_JUMBO);    \
	SAVE_LABEL(OP_SGET_OBJECT_VOLATILE_JUMBO);    \
	SAVE_LABEL(OP_SPUT_VOLATILE_JUMBO);    \
	SAVE_LABEL(OP_SPUT_WIDE_VOLATILE_JUMBO);    \
	SAVE_LABEL(OP_SPUT_OBJECT_VOLATILE_JUMBO);    \
	SAVE_LABEL(OP_THROW_VERIFICATION_ERROR_JUMBO);    \
	\
	label_little = ADDR_LABEL(OP_NOP) < ADDR_LABEL(OP_THROW_VERIFICATION_ERROR_JUMBO) ? \
					ADDR_LABEL(OP_NOP) : ADDR_LABEL(OP_THROW_VERIFICATION_ERROR_JUMBO) ;	\
	label_big    = ADDR_LABEL(OP_NOP) > ADDR_LABEL(OP_THROW_VERIFICATION_ERROR_JUMBO) ? \
					ADDR_LABEL(OP_NOP) : ADDR_LABEL(OP_THROW_VERIFICATION_ERROR_JUMBO) ;	\
}
#endif

/* File: portable/entry.cpp */
/*
 * Main interpreter loop.
 *
 * This was written with an ARM implementation in mind.
 */
void dvmInterpretPortable(Thread* self)
{
#if defined(EASY_GDB)
    StackSaveArea* debugSaveArea = SAVEAREA_FROM_FP(self->interpSave.curFrame);
#endif
    DvmDex* methodClassDex;     // curMethod->clazz->pDvmDex
    JValue retval;

    /* core state */
    const Method* curMethod;    // method we're interpreting
    const u2* pc;               // program counter
    u4* fp;                     // frame pointer
    u2 inst;                    // current instruction
    /* instruction decoding */
    u4 ref;                     // 16 or 32-bit quantity fetched directly
    u2 vsrc1, vsrc2, vdst;      // usually used for register indexes
    /* method call setup */
    const Method* methodToCall;
    vbool methodCallRange;
    vbool jumboFormat;

#if defined(ARCH_X86)
    /* static computed goto table */
	NEW_GOTO_TABLE();
#endif
    /* copy state in */
    curMethod = self->interpSave.method;
    pc = self->interpSave.pc;
    fp = self->interpSave.curFrame;
    retval = self->interpSave.retval;   /* only need for kInterpEntryReturn? */

    methodClassDex = curMethod->clazz->pDvmDex;

	/*
     * DEBUG: scramble this to ensure we're not relying on it.
     */
    methodToCall = (const Method*) -1;

	if(!self->bInterpFirst)
	{
		inst  = self->itpSchdSave.inst;
		vsrc1 = self->itpSchdSave.vsrc1;
		vsrc2 = self->itpSchdSave.vsrc2;
		vdst  = self->itpSchdSave.vdst;
		ref   = self->itpSchdSave.ref;

		methodToCall = self->itpSchdSave.methodToCall;
		methodCallRange = self->itpSchdSave.methodCallRange;
		jumboFormat = self->itpSchdSave.jumboFormat;
	
		MACRO_LOG("Thread id:%d,load globals!\n",self->threadId);
		MACRO_LOG_L("self->interpSave.method:0x%x\n",(int)curMethod);
		MACRO_LOG_L("self->interpSave.pc:0x%x\n",(int)pc);
		MACRO_LOG_L("self->interpSave.fp:0x%x\n",(int)fp);
		MACRO_LOG_L("self->interpSave.retval:0x%x\n",(int)retval.j);
		MACRO_LOG_L("self->itpSchdSave.inst:0x%x\n",(int)inst);
		MACRO_LOG_L("self->itpSchdSave.vsrc1:0x%x\n",(int)vsrc1);
		MACRO_LOG_L("self->itpSchdSave.vsrc2:0x%x\n",(int)vsrc2);
		MACRO_LOG_L("self->itpSchdSave.vdst:0x%x\n",(int)vdst);
		MACRO_LOG_L("self->itpSchdSave.ref:0x%x\n",(int)ref);
		MACRO_LOG_L("self->itpSchdSave.methodToCall:0x%x\n",(int)methodToCall);
		MACRO_LOG_L("self->itpSchdSave.methodCallRange:0x%x\n",(int)methodCallRange);
		MACRO_LOG_L("self->itpSchdSave.jumboFormat:0x%x\n",(int)jumboFormat);
		
	}

	self->bInterpFirst = FALSE;
#if __NIX__
    LOGVV("threadid=%d: %s.%s pc=%#x fp=%p",
        self->threadId, curMethod->clazz->descriptor, curMethod->name,
        pc - curMethod->insns, fp);
#endif
    /*
     * Handle any ongoing profiling and prep for debugging.
     */
#if __NIX__
    if (self->interpBreak.ctl.subMode != 0) {
        TRACE_METHOD_ENTER(self, curMethod);
        self->debugIsMethodEntry = true;   // Always true on startup
    }
#endif
    

#if 0
    if (self->debugIsMethodEntry) {
        ILOGD("|-- Now interpreting %s.%s", curMethod->clazz->descriptor,
                curMethod->name);
        DUMP_REGS(curMethod, self->interpSave.curFrame, false);
    }
#endif

    FINISH(0);                  /* fetch and execute first instruction */

/*--- start of opcodes ---*/

/* File: c/OP_NOP.cpp */
HANDLE_OPCODE(OP_NOP)
    FINISH(1);
OP_END

/* File: c/OP_MOVE.cpp */
HANDLE_OPCODE(OP_MOVE /*vA, vB*/)
    vdst = INST_A(inst);
    vsrc1 = INST_B(inst);
#if __NIX__
	ILOGV("|move%s v%d,v%d %s(v%d=0x%08x)",
        (INST_INST(inst) == OP_MOVE) ? "" : "-object", vdst, vsrc1,
        kSpacing, vdst, GET_REGISTER(vsrc1));
#endif
    SET_REGISTER(vdst, GET_REGISTER(vsrc1));
    FINISH(1);
OP_END

/* File: c/OP_MOVE_FROM16.cpp */
HANDLE_OPCODE(OP_MOVE_FROM16 /*vAA, vBBBB*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
#if __NIX__
    ILOGV("|move%s/from16 v%d,v%d %s(v%d=0x%08x)",
        (INST_INST(inst) == OP_MOVE_FROM16) ? "" : "-object", vdst, vsrc1,
        kSpacing, vdst, GET_REGISTER(vsrc1));
#endif
    SET_REGISTER(vdst, GET_REGISTER(vsrc1));
    FINISH(2);
OP_END

/* File: c/OP_MOVE_16.cpp */
HANDLE_OPCODE(OP_MOVE_16 /*vAAAA, vBBBB*/)
    vdst = FETCH(1);
    vsrc1 = FETCH(2);
#if __NIX__
    ILOGV("|move%s/16 v%d,v%d %s(v%d=0x%08x)",
        (INST_INST(inst) == OP_MOVE_16) ? "" : "-object", vdst, vsrc1,
        kSpacing, vdst, GET_REGISTER(vsrc1));
#endif
    SET_REGISTER(vdst, GET_REGISTER(vsrc1));
    FINISH(3);
OP_END

/* File: c/OP_MOVE_WIDE.cpp */
HANDLE_OPCODE(OP_MOVE_WIDE /*vA, vB*/)
    /* IMPORTANT: must correctly handle overlapping registers, e.g. both
     * "move-wide v6, v7" and "move-wide v7, v6" */
    vdst = INST_A(inst);
    vsrc1 = INST_B(inst);
#if __NIX__
    ILOGV("|move-wide v%d,v%d %s(v%d=0x%08llx)", vdst, vsrc1,
        kSpacing+5, vdst, GET_REGISTER_WIDE(vsrc1));
#endif
    SET_REGISTER_WIDE(vdst, GET_REGISTER_WIDE(vsrc1));
    FINISH(1);
OP_END

/* File: c/OP_MOVE_WIDE_FROM16.cpp */
HANDLE_OPCODE(OP_MOVE_WIDE_FROM16 /*vAA, vBBBB*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
#if __NIX__
    ILOGV("|move-wide/from16 v%d,v%d  (v%d=0x%08llx)", vdst, vsrc1,
        vdst, GET_REGISTER_WIDE(vsrc1));
#endif
    SET_REGISTER_WIDE(vdst, GET_REGISTER_WIDE(vsrc1));
    FINISH(2);
OP_END

/* File: c/OP_MOVE_WIDE_16.cpp */
HANDLE_OPCODE(OP_MOVE_WIDE_16 /*vAAAA, vBBBB*/)
    vdst = FETCH(1);
    vsrc1 = FETCH(2);
#if __NIX__
    ILOGV("|move-wide/16 v%d,v%d %s(v%d=0x%08llx)", vdst, vsrc1,
        kSpacing+8, vdst, GET_REGISTER_WIDE(vsrc1));
#endif
    SET_REGISTER_WIDE(vdst, GET_REGISTER_WIDE(vsrc1));
    FINISH(3);
OP_END

/* File: c/OP_MOVE_OBJECT.cpp */
/* File: c/OP_MOVE.cpp */
HANDLE_OPCODE(OP_MOVE_OBJECT /*vA, vB*/)
    vdst = INST_A(inst);
    vsrc1 = INST_B(inst);
#if __NIX__
    ILOGV("|move%s v%d,v%d %s(v%d=0x%08x)",
        (INST_INST(inst) == OP_MOVE) ? "" : "-object", vdst, vsrc1,
        kSpacing, vdst, GET_REGISTER(vsrc1));
#endif
    SET_REGISTER(vdst, GET_REGISTER(vsrc1));
    FINISH(1);
OP_END


/* File: c/OP_MOVE_OBJECT_FROM16.cpp */
/* File: c/OP_MOVE_FROM16.cpp */
HANDLE_OPCODE(OP_MOVE_OBJECT_FROM16 /*vAA, vBBBB*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
#if __NIX__
    ILOGV("|move%s/from16 v%d,v%d %s(v%d=0x%08x)",
        (INST_INST(inst) == OP_MOVE_FROM16) ? "" : "-object", vdst, vsrc1,
        kSpacing, vdst, GET_REGISTER(vsrc1));
#endif
    SET_REGISTER(vdst, GET_REGISTER(vsrc1));
    FINISH(2);
OP_END


/* File: c/OP_MOVE_OBJECT_16.cpp */
/* File: c/OP_MOVE_16.cpp */
HANDLE_OPCODE(OP_MOVE_OBJECT_16 /*vAAAA, vBBBB*/)
    vdst = FETCH(1);
    vsrc1 = FETCH(2);
#if __NIX__
    ILOGV("|move%s/16 v%d,v%d %s(v%d=0x%08x)",
        (INST_INST(inst) == OP_MOVE_16) ? "" : "-object", vdst, vsrc1,
        kSpacing, vdst, GET_REGISTER(vsrc1));
#endif
    SET_REGISTER(vdst, GET_REGISTER(vsrc1));
    FINISH(3);
OP_END


/* File: c/OP_MOVE_RESULT.cpp */
HANDLE_OPCODE(OP_MOVE_RESULT /*vAA*/)
    vdst = INST_AA(inst);
#if __NIX__
    ILOGV("|move-result%s v%d %s(v%d=0x%08x)",
         (INST_INST(inst) == OP_MOVE_RESULT) ? "" : "-object",
         vdst, kSpacing+4, vdst,retval.i);
#endif
    SET_REGISTER(vdst, retval.i);
    FINISH(1);
OP_END

/* File: c/OP_MOVE_RESULT_WIDE.cpp */
HANDLE_OPCODE(OP_MOVE_RESULT_WIDE /*vAA*/)
    vdst = INST_AA(inst);
#if __NIX__
    ILOGV("|move-result-wide v%d %s(0x%08llx)", vdst, kSpacing, retval.j);
#endif
    SET_REGISTER_WIDE(vdst, retval.j);
    FINISH(1);
OP_END

/* File: c/OP_MOVE_RESULT_OBJECT.cpp */
/* File: c/OP_MOVE_RESULT.cpp */
HANDLE_OPCODE(OP_MOVE_RESULT_OBJECT /*vAA*/)
    vdst = INST_AA(inst);
#if __NIX__
    ILOGV("|move-result%s v%d %s(v%d=0x%08x)",
         (INST_INST(inst) == OP_MOVE_RESULT) ? "" : "-object",
         vdst, kSpacing+4, vdst,retval.i);
#endif
    SET_REGISTER(vdst, retval.i);
    FINISH(1);
OP_END


/* File: c/OP_MOVE_EXCEPTION.cpp */
HANDLE_OPCODE(OP_MOVE_EXCEPTION /*vAA*/)
    vdst = INST_AA(inst);
#if __NIX__
    ILOGV("|move-exception v%d", vdst);
    assert(self->exception != NULL);
#endif
    SET_REGISTER(vdst, (u4)self->exception);
    dvmClearException(self);
    FINISH(1);
OP_END

/* File: c/OP_RETURN_VOID.cpp */
HANDLE_OPCODE(OP_RETURN_VOID /**/)
#if __NIX__
    ILOGV("|return-void");
#endif
#ifndef NDEBUG
    retval.j = 0x0;//0xaeaeabab ULL;    // placate valgrind
#endif
    GOTO_returnFromMethod();
OP_END

/* File: c/OP_RETURN.cpp */
HANDLE_OPCODE(OP_RETURN /*vAA*/)
    vsrc1 = INST_AA(inst);
#if __NIX__
    ILOGV("|return%s v%d",
        (INST_INST(inst) == OP_RETURN) ? "" : "-object", vsrc1);
#endif
    retval.i = GET_REGISTER(vsrc1);
    GOTO_returnFromMethod();
OP_END

/* File: c/OP_RETURN_WIDE.cpp */
HANDLE_OPCODE(OP_RETURN_WIDE /*vAA*/)
    vsrc1 = INST_AA(inst);
#if __NIX__
    ILOGV("|return-wide v%d", vsrc1);
#endif
    retval.j = GET_REGISTER_WIDE(vsrc1);
    GOTO_returnFromMethod();
OP_END

/* File: c/OP_RETURN_OBJECT.cpp */
/* File: c/OP_RETURN.cpp */
HANDLE_OPCODE(OP_RETURN_OBJECT /*vAA*/)
    vsrc1 = INST_AA(inst);
#if __NIX__
    ILOGV("|return%s v%d",
        (INST_INST(inst) == OP_RETURN) ? "" : "-object", vsrc1);
#endif
    retval.i = GET_REGISTER(vsrc1);
    GOTO_returnFromMethod();
OP_END


/* File: c/OP_CONST_4.cpp */
HANDLE_OPCODE(OP_CONST_4 /*vA, #+B*/)
    {
        s4 tmp;

        vdst = INST_A(inst);
        tmp = (s4) (INST_B(inst) << 28) >> 28;  // sign extend 4-bit value
#if __NIX__
        ILOGV("|const/4 v%d,#0x%02x", vdst, (s4)tmp);
#endif
        SET_REGISTER(vdst, tmp);
    }
    FINISH(1);
OP_END

/* File: c/OP_CONST_16.cpp */
HANDLE_OPCODE(OP_CONST_16 /*vAA, #+BBBB*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
#if __NIX__
    ILOGV("|const/16 v%d,#0x%04x", vdst, (s2)vsrc1);
#endif
    SET_REGISTER(vdst, (s2) vsrc1);
    FINISH(2);
OP_END

/* File: c/OP_CONST.cpp */
HANDLE_OPCODE(OP_CONST /*vAA, #+BBBBBBBB*/)
    {
        u4 tmp;

        vdst = INST_AA(inst);
        tmp = FETCH(1);
        tmp |= (u4)FETCH(2) << 16;
#if __NIX__
        ILOGV("|const v%d,#0x%08x", vdst, tmp);
#endif
        SET_REGISTER(vdst, tmp);
    }
    FINISH(3);
OP_END

/* File: c/OP_CONST_HIGH16.cpp */
HANDLE_OPCODE(OP_CONST_HIGH16 /*vAA, #+BBBB0000*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
#if __NIX__
    ILOGV("|const/high16 v%d,#0x%04x0000", vdst, vsrc1);
#endif
    SET_REGISTER(vdst, vsrc1 << 16);
    FINISH(2);
OP_END

/* File: c/OP_CONST_WIDE_16.cpp */
HANDLE_OPCODE(OP_CONST_WIDE_16 /*vAA, #+BBBB*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
#if __NIX__
    ILOGV("|const-wide/16 v%d,#0x%04x", vdst, (s2)vsrc1);
#endif
    SET_REGISTER_WIDE(vdst, (s2)vsrc1);
    FINISH(2);
OP_END

/* File: c/OP_CONST_WIDE_32.cpp */
HANDLE_OPCODE(OP_CONST_WIDE_32 /*vAA, #+BBBBBBBB*/)
    {
        u4 tmp;

        vdst = INST_AA(inst);
        tmp = FETCH(1);
        tmp |= (u4)FETCH(2) << 16;
#if __NIX__
        ILOGV("|const-wide/32 v%d,#0x%08x", vdst, tmp);
#endif
        SET_REGISTER_WIDE(vdst, (s4) tmp);
    }
    FINISH(3);
OP_END

/* File: c/OP_CONST_WIDE.cpp */
HANDLE_OPCODE(OP_CONST_WIDE /*vAA, #+BBBBBBBBBBBBBBBB*/)
    {
        u8 tmp;

        vdst = INST_AA(inst);
        tmp = FETCH(1);
        tmp |= (u8)FETCH(2) << 16;
        tmp |= (u8)FETCH(3) << 32;
        tmp |= (u8)FETCH(4) << 48;
#if __NIX__
        ILOGV("|const-wide v%d,#0x%08llx", vdst, tmp);
#endif
        SET_REGISTER_WIDE(vdst, tmp);
    }
    FINISH(5);
OP_END

/* File: c/OP_CONST_WIDE_HIGH16.cpp */
HANDLE_OPCODE(OP_CONST_WIDE_HIGH16 /*vAA, #+BBBB000000000000*/)
    vdst = INST_AA(inst);
    vsrc1 = FETCH(1);
#if __NIX__
    ILOGV("|const-wide/high16 v%d,#0x%04x000000000000", vdst, vsrc1);
#endif
    SET_REGISTER_WIDE(vdst, ((u8) vsrc1) << 48);
    FINISH(2);
OP_END

/* File: c/OP_CONST_STRING.cpp */
HANDLE_OPCODE(OP_CONST_STRING /*vAA, string@BBBB*/)
    {
        StringObject* strObj;

        vdst = INST_AA(inst);
        ref = FETCH(1);
#if __NIX__
        ILOGV("|const-string v%d string@0x%04x", vdst, ref);
#endif
        strObj = dvmDexGetResolvedString(methodClassDex, ref);
        if (strObj == NULL) {
            EXPORT_PC();
            strObj = dvmResolveString(curMethod->clazz, ref);
            if (strObj == NULL)
                GOTO_exceptionThrown();
        }
        SET_REGISTER(vdst, (u4) strObj);
    }
    FINISH(2);
OP_END

/* File: c/OP_CONST_STRING_JUMBO.cpp */
HANDLE_OPCODE(OP_CONST_STRING_JUMBO /*vAA, string@BBBBBBBB*/)
    {
        StringObject* strObj;
        u4 tmp;

        vdst = INST_AA(inst);
        tmp = FETCH(1);
        tmp |= (u4)FETCH(2) << 16;
#if __NIX__
        ILOGV("|const-string/jumbo v%d string@0x%08x", vdst, tmp);
#endif
        strObj = dvmDexGetResolvedString(methodClassDex, tmp);
        if (strObj == NULL) {
            EXPORT_PC();
            strObj = dvmResolveString(curMethod->clazz, tmp);
            if (strObj == NULL)
                GOTO_exceptionThrown();
        }
        SET_REGISTER(vdst, (u4) strObj);
    }
    FINISH(3);
OP_END

/* File: c/OP_CONST_CLASS.cpp */
HANDLE_OPCODE(OP_CONST_CLASS /*vAA, class@BBBB*/)
    {
        ClassObject* clazz;

        vdst = INST_AA(inst);
        ref = FETCH(1);
#if __NIX__
        ILOGV("|const-class v%d class@0x%04x", vdst, ref);
#endif
        clazz = dvmDexGetResolvedClass(methodClassDex, ref);
        if (clazz == NULL) {
            EXPORT_PC();
            clazz = dvmResolveClass(curMethod->clazz, ref, true);
            if (clazz == NULL)
                GOTO_exceptionThrown();
        }
        SET_REGISTER(vdst, (u4) clazz);
    }
    FINISH(2);
OP_END

/* File: c/OP_MONITOR_ENTER.cpp */
HANDLE_OPCODE(OP_MONITOR_ENTER /*vAA*/)
    {
        Object* obj;

        vsrc1 = INST_AA(inst);
#if __NIX__
        ILOGV("|monitor-enter v%d %s(0x%08x)",
            vsrc1, kSpacing+6, GET_REGISTER(vsrc1));
#endif
        obj = (Object*)GET_REGISTER(vsrc1);
        if (!checkForNullExportPC(obj, fp, pc))
            GOTO_exceptionThrown();
#if __NIX__
        ILOGV("+ locking %p %s", obj, obj->clazz->descriptor);
#endif
        EXPORT_PC();    /* need for precise GC */
        //dvmLockObject(self, obj);
		if(!Sync_dvmLockObject(self,obj))
		{
			/*launch one schdule*/
			SET_SCHEDULE();
			FINISH(0);
		}		
    }
    FINISH(1);
OP_END

/* File: c/OP_MONITOR_EXIT.cpp */
HANDLE_OPCODE(OP_MONITOR_EXIT /*vAA*/)
    {
        Object* obj;

        EXPORT_PC();

        vsrc1 = INST_AA(inst);
#if __NIX__
        ILOGV("|monitor-exit v%d %s(0x%08x)",
            vsrc1, kSpacing+5, GET_REGISTER(vsrc1));
#endif
        obj = (Object*)GET_REGISTER(vsrc1);
        if (!checkForNull(obj)) {
            /*
             * The exception needs to be processed at the *following*
             * instruction, not the current instruction (see the Dalvik
             * spec).  Because we're jumping to an exception handler,
             * we're not actually at risk of skipping an instruction
             * by doing so.
             */
            ADJUST_PC(1);           /* monitor-exit width is 1 */
            GOTO_exceptionThrown();
        }
#if __NIX__
        ILOGV("+ unlocking %p %s", obj, obj->clazz->descriptor);
#endif
        //if (!dvmUnlockObject(self, obj)) 
		if (!Sync_dvmUnlockObject(self, obj)) 
		{
            assert(dvmCheckException(self));
            ADJUST_PC(1);
            GOTO_exceptionThrown();
        }
    }
    FINISH(1);
OP_END

/* File: c/OP_CHECK_CAST.cpp */
HANDLE_OPCODE(OP_CHECK_CAST /*vAA, class@BBBB*/)
    {
        ClassObject* clazz;
        Object* obj;

        EXPORT_PC();

        vsrc1 = INST_AA(inst);
        ref = FETCH(1);         /* class to check against */
#if __NIX__
        ILOGV("|check-cast v%d,class@0x%04x", vsrc1, ref);
#endif
        obj = (Object*)GET_REGISTER(vsrc1);
        if (obj != NULL) {
#if defined(WITH_EXTRA_OBJECT_VALIDATION)
            if (!checkForNull(obj))
                GOTO_exceptionThrown();
#endif
            clazz = dvmDexGetResolvedClass(methodClassDex, ref);
            if (clazz == NULL) {
                clazz = dvmResolveClass(curMethod->clazz, ref, false);
                if (clazz == NULL)
                    GOTO_exceptionThrown();
            }
            if (!dvmInstanceof(obj->clazz, clazz)) {
                dvmThrowClassCastException(obj->clazz, clazz);
                GOTO_exceptionThrown();
            }
        }
    }
    FINISH(2);
OP_END

/* File: c/OP_INSTANCE_OF.cpp */
HANDLE_OPCODE(OP_INSTANCE_OF /*vA, vB, class@CCCC*/)
    {
        ClassObject* clazz;
        Object* obj;

        vdst = INST_A(inst);
        vsrc1 = INST_B(inst);   /* object to check */
        ref = FETCH(1);         /* class to check against */
#if __NIX__
        ILOGV("|instance-of v%d,v%d,class@0x%04x", vdst, vsrc1, ref);
#endif
        obj = (Object*)GET_REGISTER(vsrc1);
        if (obj == NULL) {
            SET_REGISTER(vdst, 0);
        } else {
#if defined(WITH_EXTRA_OBJECT_VALIDATION)
            if (!checkForNullExportPC(obj, fp, pc))
                GOTO_exceptionThrown();
#endif
            clazz = dvmDexGetResolvedClass(methodClassDex, ref);
            if (clazz == NULL) {
                EXPORT_PC();
                clazz = dvmResolveClass(curMethod->clazz, ref, true);
                if (clazz == NULL)
                    GOTO_exceptionThrown();
            }
            SET_REGISTER(vdst, dvmInstanceof(obj->clazz, clazz));
        }
    }
    FINISH(2);
OP_END

/* File: c/OP_ARRAY_LENGTH.cpp */
HANDLE_OPCODE(OP_ARRAY_LENGTH /*vA, vB*/)
    {
        ArrayObject* arrayObj;

        vdst = INST_A(inst);
        vsrc1 = INST_B(inst);
        arrayObj = (ArrayObject*) GET_REGISTER(vsrc1);
#if __NIX__
        ILOGV("|array-length v%d,v%d  (%p)", vdst, vsrc1, arrayObj);
#endif
        if (!checkForNullExportPC((Object*) arrayObj, fp, pc))
            GOTO_exceptionThrown();
        /* verifier guarantees this is an array reference */
        SET_REGISTER(vdst, arrayObj->length);
    }
    FINISH(1);
OP_END

/* File: c/OP_NEW_INSTANCE.cpp */
HANDLE_OPCODE(OP_NEW_INSTANCE /*vAA, class@BBBB*/)
    {
        ClassObject* clazz;
        Object* newObj;

        EXPORT_PC();

        vdst = INST_AA(inst);
        ref = FETCH(1);
#if __NIX__
        ILOGV("|new-instance v%d,class@0x%04x", vdst, ref);
#endif
        clazz = dvmDexGetResolvedClass(methodClassDex, ref);
        if (clazz == NULL) {
            clazz = dvmResolveClass(curMethod->clazz, ref, false);
            if (clazz == NULL)
                GOTO_exceptionThrown();
        }

        if (!dvmIsClassInitialized(clazz) && !dvmInitClass(clazz))
            GOTO_exceptionThrown();

#if defined(WITH_JIT)
        /*
         * The JIT needs dvmDexGetResolvedClass() to return non-null.
         * Since we use the portable interpreter to build the trace, this extra
         * check is not needed for mterp.
         */
        if ((self->interpBreak.ctl.subMode & kSubModeJitTraceBuild) &&
            (!dvmDexGetResolvedClass(methodClassDex, ref))) {
            /* Class initialization is still ongoing - end the trace */
            dvmJitEndTraceSelect(self,pc);
        }
#endif

        /*
         * Verifier now tests for interface/abstract class.
         */
        //if (dvmIsInterfaceClass(clazz) || dvmIsAbstractClass(clazz)) {
        //    dvmThrowExceptionWithClassMessage(gDvm.exInstantiationError,
        //        clazz->descriptor);
        //    GOTO_exceptionThrown();
        //}
        newObj = dvmAllocObject(clazz, ALLOC_DONT_TRACK);
        if (newObj == NULL)
            GOTO_exceptionThrown();
        SET_REGISTER(vdst, (u4) newObj);
    }
    FINISH(2);
OP_END

/* File: c/OP_NEW_ARRAY.cpp */
HANDLE_OPCODE(OP_NEW_ARRAY /*vA, vB, class@CCCC*/)
    {
        ClassObject* arrayClass;
        ArrayObject* newArray;
        s4 length;

        EXPORT_PC();

        vdst = INST_A(inst);
        vsrc1 = INST_B(inst);       /* length reg */
        ref = FETCH(1);
#if __NIX__
        ILOGV("|new-array v%d,v%d,class@0x%04x  (%d elements)",
            vdst, vsrc1, ref, (s4) GET_REGISTER(vsrc1));
#endif
        length = (s4) GET_REGISTER(vsrc1);
        if (length < 0) {
            dvmThrowNegativeArraySizeException(length);
            GOTO_exceptionThrown();
        }
        arrayClass = dvmDexGetResolvedClass(methodClassDex, ref);
        if (arrayClass == NULL) {
            arrayClass = dvmResolveClass(curMethod->clazz, ref, false);
            if (arrayClass == NULL)
                GOTO_exceptionThrown();
        }
        /* verifier guarantees this is an array class */
        assert(dvmIsArrayClass(arrayClass));
        assert(dvmIsClassInitialized(arrayClass));

        newArray = dvmAllocArrayByClass(arrayClass, length, ALLOC_DONT_TRACK);
        if (newArray == NULL)
            GOTO_exceptionThrown();
        SET_REGISTER(vdst, (u4) newArray);
    }
    FINISH(2);
OP_END

/* File: c/OP_FILLED_NEW_ARRAY.cpp */
HANDLE_OPCODE(OP_FILLED_NEW_ARRAY /*vB, {vD, vE, vF, vG, vA}, class@CCCC*/)
    GOTO_invoke(filledNewArray, false, false);
OP_END

/* File: c/OP_FILLED_NEW_ARRAY_RANGE.cpp */
HANDLE_OPCODE(OP_FILLED_NEW_ARRAY_RANGE /*{vCCCC..v(CCCC+AA-1)}, class@BBBB*/)
    GOTO_invoke(filledNewArray, true, false);
OP_END

/* File: c/OP_FILL_ARRAY_DATA.cpp */
HANDLE_OPCODE(OP_FILL_ARRAY_DATA)   /*vAA, +BBBBBBBB*/
    {
        const u2* arrayData;
        s4 offset;
        ArrayObject* arrayObj;

        EXPORT_PC();
        vsrc1 = INST_AA(inst);
        offset = FETCH(1) | (((s4) FETCH(2)) << 16);
#if __NIX__
        ILOGV("|fill-array-data v%d +0x%04x", vsrc1, offset);
#endif
        arrayData = pc + offset;       // offset in 16-bit units
#ifndef NDEBUG
        if (arrayData < curMethod->insns ||
            arrayData >= curMethod->insns + dvmGetMethodInsnsSize(curMethod))
        {
            /* should have been caught in verifier */
            dvmThrowInternalError("bad fill array data");
            GOTO_exceptionThrown();
        }
#endif
        arrayObj = (ArrayObject*) GET_REGISTER(vsrc1);
        if (!dvmInterpHandleFillArrayData(arrayObj, arrayData)) {
            GOTO_exceptionThrown();
        }
        FINISH(3);
    }
OP_END

/* File: c/OP_THROW.cpp */
HANDLE_OPCODE(OP_THROW /*vAA*/)
    {
        Object* obj;

        /*
         * We don't create an exception here, but the process of searching
         * for a catch block can do class lookups and throw exceptions.
         * We need to update the saved PC.
         */
        EXPORT_PC();

        vsrc1 = INST_AA(inst);
#if __NIX__
        ILOGV("|throw v%d  (%p)", vsrc1, (void*)GET_REGISTER(vsrc1));
#endif
        obj = (Object*) GET_REGISTER(vsrc1);
        if (!checkForNull(obj)) {
            /* will throw a null pointer exception */
#if __NIX__            
            LOGVV("Bad exception");
#endif
        } else {
            /* use the requested exception */
            dvmSetException(self, obj);
        }
        GOTO_exceptionThrown();
    }
OP_END

/* File: c/OP_GOTO.cpp */
HANDLE_OPCODE(OP_GOTO /*+AA*/)
    vdst = INST_AA(inst);
#if __NIX__
    if ((s1)vdst < 0)
        ILOGV("|goto -0x%02x", -((s1)vdst));
    else
        ILOGV("|goto +0x%02x", ((s1)vdst));
    ILOGV("> branch taken");
#endif
    if ((s1)vdst < 0)
        PERIODIC_CHECKS((s1)vdst);
    FINISH((s1)vdst);
OP_END

/* File: c/OP_GOTO_16.cpp */
HANDLE_OPCODE(OP_GOTO_16 /*+AAAA*/)
    {
        s4 offset = (s2) FETCH(1);          /* sign-extend next code unit */
#if __NIX__
        if (offset < 0)
            ILOGV("|goto/16 -0x%04x", -offset);
        else
            ILOGV("|goto/16 +0x%04x", offset);
        ILOGV("> branch taken");
#endif
        if (offset < 0)
            PERIODIC_CHECKS(offset);
        FINISH(offset);
    }
OP_END

/* File: c/OP_GOTO_32.cpp */
HANDLE_OPCODE(OP_GOTO_32 /*+AAAAAAAA*/)
    {
        s4 offset = FETCH(1);               /* low-order 16 bits */
        offset |= ((s4) FETCH(2)) << 16;    /* high-order 16 bits */
#if __NIX__
        if (offset < 0)
            ILOGV("|goto/32 -0x%08x", -offset);
        else
            ILOGV("|goto/32 +0x%08x", offset);
        ILOGV("> branch taken");
#endif
        if (offset <= 0)    /* allowed to branch to self */
            PERIODIC_CHECKS(offset);
        FINISH(offset);
    }
OP_END

/* File: c/OP_PACKED_SWITCH.cpp */
HANDLE_OPCODE(OP_PACKED_SWITCH /*vAA, +BBBB*/)
    {
        const u2* switchData;
        u4 testVal;
        s4 offset;

        vsrc1 = INST_AA(inst);
        offset = FETCH(1) | (((s4) FETCH(2)) << 16);
#if __NIX__
        ILOGV("|packed-switch v%d +0x%04x", vsrc1, vsrc2);
#endif
        switchData = pc + offset;       // offset in 16-bit units
#ifndef NDEBUG
        if (switchData < curMethod->insns ||
            switchData >= curMethod->insns + dvmGetMethodInsnsSize(curMethod))
        {
            /* should have been caught in verifier */
            EXPORT_PC();
            dvmThrowInternalError("bad packed switch");
            GOTO_exceptionThrown();
        }
#endif
        testVal = GET_REGISTER(vsrc1);

        offset = dvmInterpHandlePackedSwitch(switchData, testVal);
#if __NIX__
        ILOGV("> branch taken (0x%04x)", offset);
#endif
        if (offset <= 0)  /* uncommon */
            PERIODIC_CHECKS(offset);
        FINISH(offset);
    }
OP_END

/* File: c/OP_SPARSE_SWITCH.cpp */
HANDLE_OPCODE(OP_SPARSE_SWITCH /*vAA, +BBBB*/)
    {
        const u2* switchData;
        u4 testVal;
        s4 offset;

        vsrc1 = INST_AA(inst);
        offset = FETCH(1) | (((s4) FETCH(2)) << 16);
#if __NIX__
        ILOGV("|sparse-switch v%d +0x%04x", vsrc1, vsrc2);
#endif
        switchData = pc + offset;       // offset in 16-bit units
#ifndef NDEBUG
        if (switchData < curMethod->insns ||
            switchData >= curMethod->insns + dvmGetMethodInsnsSize(curMethod))
        {
            /* should have been caught in verifier */
            EXPORT_PC();
            dvmThrowInternalError("bad sparse switch");
            GOTO_exceptionThrown();
        }
#endif
        testVal = GET_REGISTER(vsrc1);

        offset = dvmInterpHandleSparseSwitch(switchData, testVal);
#if __NIX__
        ILOGV("> branch taken (0x%04x)", offset);
#endif
        if (offset <= 0)  /* uncommon */
            PERIODIC_CHECKS(offset);
        FINISH(offset);
    }
OP_END

/* File: c/OP_CMPL_FLOAT.cpp */
HANDLE_OP_CMPX(OP_CMPL_FLOAT, "l-float", float, _FLOAT, -1)
OP_END

/* File: c/OP_CMPG_FLOAT.cpp */
HANDLE_OP_CMPX(OP_CMPG_FLOAT, "g-float", float, _FLOAT, 1)
OP_END

/* File: c/OP_CMPL_DOUBLE.cpp */
HANDLE_OP_CMPX(OP_CMPL_DOUBLE, "l-double", double, _DOUBLE, -1)
OP_END

/* File: c/OP_CMPG_DOUBLE.cpp */
HANDLE_OP_CMPX(OP_CMPG_DOUBLE, "g-double", double, _DOUBLE, 1)
OP_END

/* File: c/OP_CMP_LONG.cpp */
HANDLE_OP_CMPX(OP_CMP_LONG, "-long", s8, _WIDE, 0)
OP_END

/* File: c/OP_IF_EQ.cpp */
HANDLE_OP_IF_XX(OP_IF_EQ, "eq", ==)
OP_END

/* File: c/OP_IF_NE.cpp */
HANDLE_OP_IF_XX(OP_IF_NE, "ne", !=)
OP_END

/* File: c/OP_IF_LT.cpp */
HANDLE_OP_IF_XX(OP_IF_LT, "lt", <)
OP_END

/* File: c/OP_IF_GE.cpp */
HANDLE_OP_IF_XX(OP_IF_GE, "ge", >=)
OP_END

/* File: c/OP_IF_GT.cpp */
HANDLE_OP_IF_XX(OP_IF_GT, "gt", >)
OP_END

/* File: c/OP_IF_LE.cpp */
HANDLE_OP_IF_XX(OP_IF_LE, "le", <=)
OP_END

/* File: c/OP_IF_EQZ.cpp */
HANDLE_OP_IF_XXZ(OP_IF_EQZ, "eqz", ==)
OP_END

/* File: c/OP_IF_NEZ.cpp */
HANDLE_OP_IF_XXZ(OP_IF_NEZ, "nez", !=)
OP_END

/* File: c/OP_IF_LTZ.cpp */
HANDLE_OP_IF_XXZ(OP_IF_LTZ, "ltz", <)
OP_END

/* File: c/OP_IF_GEZ.cpp */
HANDLE_OP_IF_XXZ(OP_IF_GEZ, "gez", >=)
OP_END

/* File: c/OP_IF_GTZ.cpp */
HANDLE_OP_IF_XXZ(OP_IF_GTZ, "gtz", >)
OP_END

/* File: c/OP_IF_LEZ.cpp */
HANDLE_OP_IF_XXZ(OP_IF_LEZ, "lez", <=)
OP_END

/* File: c/OP_UNUSED_3E.cpp */
HANDLE_OPCODE(OP_UNUSED_3E)
OP_END

/* File: c/OP_UNUSED_3F.cpp */
HANDLE_OPCODE(OP_UNUSED_3F)
OP_END

/* File: c/OP_UNUSED_40.cpp */
HANDLE_OPCODE(OP_UNUSED_40)
OP_END

/* File: c/OP_UNUSED_41.cpp */
HANDLE_OPCODE(OP_UNUSED_41)
OP_END

/* File: c/OP_UNUSED_42.cpp */
HANDLE_OPCODE(OP_UNUSED_42)
OP_END

/* File: c/OP_UNUSED_43.cpp */
HANDLE_OPCODE(OP_UNUSED_43)
OP_END

/* File: c/OP_AGET.cpp */
HANDLE_OP_AGET(OP_AGET, "", u4, )
OP_END

/* File: c/OP_AGET_WIDE.cpp */
HANDLE_OP_AGET(OP_AGET_WIDE, "-wide", s8, _WIDE)
OP_END

/* File: c/OP_AGET_OBJECT.cpp */
HANDLE_OP_AGET(OP_AGET_OBJECT, "-object", u4, )
OP_END

/* File: c/OP_AGET_BOOLEAN.cpp */
HANDLE_OP_AGET(OP_AGET_BOOLEAN, "-boolean", u1, )
OP_END

/* File: c/OP_AGET_BYTE.cpp */
HANDLE_OP_AGET(OP_AGET_BYTE, "-byte", s1, )
OP_END

/* File: c/OP_AGET_CHAR.cpp */
HANDLE_OP_AGET(OP_AGET_CHAR, "-char", u2, )
OP_END

/* File: c/OP_AGET_SHORT.cpp */
HANDLE_OP_AGET(OP_AGET_SHORT, "-short", s2, )
OP_END

/* File: c/OP_APUT.cpp */
HANDLE_OP_APUT(OP_APUT, "", u4, )
OP_END

/* File: c/OP_APUT_WIDE.cpp */
HANDLE_OP_APUT(OP_APUT_WIDE, "-wide", s8, _WIDE)
OP_END

/* File: c/OP_APUT_OBJECT.cpp */
HANDLE_OPCODE(OP_APUT_OBJECT /*vAA, vBB, vCC*/)
    {
        ArrayObject* arrayObj;
        Object* obj;
        u2 arrayInfo;
        EXPORT_PC();
        vdst = INST_AA(inst);       /* AA: source value */
        arrayInfo = FETCH(1);
        vsrc1 = arrayInfo & 0xff;   /* BB: array ptr */
        vsrc2 = arrayInfo >> 8;     /* CC: index */
#if __NIX__
        ILOGV("|aput%s v%d,v%d,v%d", "-object", vdst, vsrc1, vsrc2);
#endif
        arrayObj = (ArrayObject*) GET_REGISTER(vsrc1);
        if (!checkForNull((Object*) arrayObj))
            GOTO_exceptionThrown();
        if (GET_REGISTER(vsrc2) >= arrayObj->length) {
            dvmThrowArrayIndexOutOfBoundsException(
                arrayObj->length, GET_REGISTER(vsrc2));
            GOTO_exceptionThrown();
        }
        obj = (Object*) GET_REGISTER(vdst);
        if (obj != NULL) {
            if (!checkForNull(obj))
                GOTO_exceptionThrown();
            if (!dvmCanPutArrayElement(obj->clazz, arrayObj->obj.clazz)) {  //arrayObj->clazz 
#if __NIX__            
                LOGV("Can't put a '%s'(%p) into array type='%s'(%p)",
                    obj->clazz->descriptor, obj,
                    arrayObj->obj.clazz->descriptor, arrayObj);
#endif
                dvmThrowArrayStoreExceptionIncompatibleElement(obj->clazz, arrayObj->obj.clazz);  //arrayObj->clazz
                GOTO_exceptionThrown();
            }
        }
#if __NIX__
        ILOGV("+ APUT[%d]=0x%08x", GET_REGISTER(vsrc2), GET_REGISTER(vdst));
#endif
        dvmSetObjectArrayElement(arrayObj,
                                 GET_REGISTER(vsrc2),
                                 (Object *)GET_REGISTER(vdst));
    }
    FINISH(2);
OP_END

/* File: c/OP_APUT_BOOLEAN.cpp */
HANDLE_OP_APUT(OP_APUT_BOOLEAN, "-boolean", u1, )
OP_END

/* File: c/OP_APUT_BYTE.cpp */
HANDLE_OP_APUT(OP_APUT_BYTE, "-byte", s1, )
OP_END

/* File: c/OP_APUT_CHAR.cpp */
HANDLE_OP_APUT(OP_APUT_CHAR, "-char", u2, )
OP_END

/* File: c/OP_APUT_SHORT.cpp */
HANDLE_OP_APUT(OP_APUT_SHORT, "-short", s2, )
OP_END

/* File: c/OP_IGET.cpp */
HANDLE_IGET_X(OP_IGET,                  "", Int, )
OP_END

/* File: c/OP_IGET_WIDE.cpp */
HANDLE_IGET_X(OP_IGET_WIDE,             "-wide", Long, _WIDE)
OP_END

/* File: c/OP_IGET_OBJECT.cpp */
HANDLE_IGET_X(OP_IGET_OBJECT,           "-object", Object, _AS_OBJECT)
OP_END

/* File: c/OP_IGET_BOOLEAN.cpp */
HANDLE_IGET_X(OP_IGET_BOOLEAN,          "", Int, )
OP_END

/* File: c/OP_IGET_BYTE.cpp */
HANDLE_IGET_X(OP_IGET_BYTE,             "", Int, )
OP_END

/* File: c/OP_IGET_CHAR.cpp */
HANDLE_IGET_X(OP_IGET_CHAR,             "", Int, )
OP_END

/* File: c/OP_IGET_SHORT.cpp */
HANDLE_IGET_X(OP_IGET_SHORT,            "", Int, )
OP_END

/* File: c/OP_IPUT.cpp */
HANDLE_IPUT_X(OP_IPUT,                  "", Int, )
OP_END

/* File: c/OP_IPUT_WIDE.cpp */
HANDLE_IPUT_X(OP_IPUT_WIDE,             "-wide", Long, _WIDE)
OP_END

/* File: c/OP_IPUT_OBJECT.cpp */
/*
 * The VM spec says we should verify that the reference being stored into
 * the field is assignment compatible.  In practice, many popular VMs don't
 * do this because it slows down a very common operation.  It's not so bad
 * for us, since "dexopt" quickens it whenever possible, but it's still an
 * issue.
 *
 * To make this spec-complaint, we'd need to add a ClassObject pointer to
 * the Field struct, resolve the field's type descriptor at link or class
 * init time, and then verify the type here.
 */
HANDLE_IPUT_X(OP_IPUT_OBJECT,           "-object", Object, _AS_OBJECT)
OP_END

/* File: c/OP_IPUT_BOOLEAN.cpp */
HANDLE_IPUT_X(OP_IPUT_BOOLEAN,          "", Int, )
OP_END

/* File: c/OP_IPUT_BYTE.cpp */
HANDLE_IPUT_X(OP_IPUT_BYTE,             "", Int, )
OP_END

/* File: c/OP_IPUT_CHAR.cpp */
HANDLE_IPUT_X(OP_IPUT_CHAR,             "", Int, )
OP_END

/* File: c/OP_IPUT_SHORT.cpp */
HANDLE_IPUT_X(OP_IPUT_SHORT,            "", Int, )
OP_END

/* File: c/OP_SGET.cpp */
HANDLE_SGET_X(OP_SGET,                  "", Int, )
OP_END

/* File: c/OP_SGET_WIDE.cpp */
HANDLE_SGET_X(OP_SGET_WIDE,             "-wide", Long, _WIDE)
OP_END

/* File: c/OP_SGET_OBJECT.cpp */
HANDLE_SGET_X(OP_SGET_OBJECT,           "-object", Object, _AS_OBJECT)
OP_END

/* File: c/OP_SGET_BOOLEAN.cpp */
HANDLE_SGET_X(OP_SGET_BOOLEAN,          "", Int, )
OP_END

/* File: c/OP_SGET_BYTE.cpp */
HANDLE_SGET_X(OP_SGET_BYTE,             "", Int, )
OP_END

/* File: c/OP_SGET_CHAR.cpp */
HANDLE_SGET_X(OP_SGET_CHAR,             "", Int, )
OP_END

/* File: c/OP_SGET_SHORT.cpp */
HANDLE_SGET_X(OP_SGET_SHORT,            "", Int, )
OP_END

/* File: c/OP_SPUT.cpp */
HANDLE_SPUT_X(OP_SPUT,                  "", Int, )
OP_END

/* File: c/OP_SPUT_WIDE.cpp */
HANDLE_SPUT_X(OP_SPUT_WIDE,             "-wide", Long, _WIDE)
OP_END

/* File: c/OP_SPUT_OBJECT.cpp */
HANDLE_SPUT_X(OP_SPUT_OBJECT,           "-object", Object, _AS_OBJECT)
OP_END

/* File: c/OP_SPUT_BOOLEAN.cpp */
HANDLE_SPUT_X(OP_SPUT_BOOLEAN,          "", Int, )
OP_END

/* File: c/OP_SPUT_BYTE.cpp */
HANDLE_SPUT_X(OP_SPUT_BYTE,             "", Int, )
OP_END

/* File: c/OP_SPUT_CHAR.cpp */
HANDLE_SPUT_X(OP_SPUT_CHAR,             "", Int, )
OP_END

/* File: c/OP_SPUT_SHORT.cpp */
HANDLE_SPUT_X(OP_SPUT_SHORT,            "", Int, )
OP_END

/* File: c/OP_INVOKE_VIRTUAL.cpp */
HANDLE_OPCODE(OP_INVOKE_VIRTUAL /*vB, {vD, vE, vF, vG, vA}, meth@CCCC*/)
    GOTO_invoke(invokeVirtual, false, false);
OP_END

/* File: c/OP_INVOKE_SUPER.cpp */
HANDLE_OPCODE(OP_INVOKE_SUPER /*vB, {vD, vE, vF, vG, vA}, meth@CCCC*/)
    GOTO_invoke(invokeSuper, false, false);
OP_END

/* File: c/OP_INVOKE_DIRECT.cpp */
HANDLE_OPCODE(OP_INVOKE_DIRECT /*vB, {vD, vE, vF, vG, vA}, meth@CCCC*/)
    GOTO_invoke(invokeDirect, false, false);
OP_END

/* File: c/OP_INVOKE_STATIC.cpp */
HANDLE_OPCODE(OP_INVOKE_STATIC /*vB, {vD, vE, vF, vG, vA}, meth@CCCC*/)
    GOTO_invoke(invokeStatic, false, false);
OP_END

/* File: c/OP_INVOKE_INTERFACE.cpp */
HANDLE_OPCODE(OP_INVOKE_INTERFACE /*vB, {vD, vE, vF, vG, vA}, meth@CCCC*/)
    GOTO_invoke(invokeInterface, false, false);
OP_END

/* File: c/OP_UNUSED_73.cpp */
HANDLE_OPCODE(OP_UNUSED_73)
OP_END

/* File: c/OP_INVOKE_VIRTUAL_RANGE.cpp */
HANDLE_OPCODE(OP_INVOKE_VIRTUAL_RANGE /*{vCCCC..v(CCCC+AA-1)}, meth@BBBB*/)
    GOTO_invoke(invokeVirtual, true, false);
OP_END

/* File: c/OP_INVOKE_SUPER_RANGE.cpp */
HANDLE_OPCODE(OP_INVOKE_SUPER_RANGE /*{vCCCC..v(CCCC+AA-1)}, meth@BBBB*/)
    GOTO_invoke(invokeSuper, true, false);
OP_END

/* File: c/OP_INVOKE_DIRECT_RANGE.cpp */
HANDLE_OPCODE(OP_INVOKE_DIRECT_RANGE /*{vCCCC..v(CCCC+AA-1)}, meth@BBBB*/)
    GOTO_invoke(invokeDirect, true, false);
OP_END

/* File: c/OP_INVOKE_STATIC_RANGE.cpp */
HANDLE_OPCODE(OP_INVOKE_STATIC_RANGE /*{vCCCC..v(CCCC+AA-1)}, meth@BBBB*/)
    GOTO_invoke(invokeStatic, true, false);
OP_END

/* File: c/OP_INVOKE_INTERFACE_RANGE.cpp */
HANDLE_OPCODE(OP_INVOKE_INTERFACE_RANGE /*{vCCCC..v(CCCC+AA-1)}, meth@BBBB*/)
    GOTO_invoke(invokeInterface, true, false);
OP_END

/* File: c/OP_UNUSED_79.cpp */
HANDLE_OPCODE(OP_UNUSED_79)
OP_END

/* File: c/OP_UNUSED_7A.cpp */
HANDLE_OPCODE(OP_UNUSED_7A)
OP_END

/* File: c/OP_NEG_INT.cpp */
HANDLE_UNOP(OP_NEG_INT, "neg-int", -, , )
OP_END

/* File: c/OP_NOT_INT.cpp */
HANDLE_UNOP(OP_NOT_INT, "not-int",R_NONE,^0xffffffff, )
OP_END

/* File: c/OP_NEG_LONG.cpp */
HANDLE_UNOP(OP_NEG_LONG, "neg-long", -,& 0xffffffffffffffff,_WIDE)
OP_END

/* File: c/OP_NOT_LONG.cpp */
HANDLE_UNOP(OP_NOT_LONG, "not-long",R_NONE, ^ 0xffffffffffffffff, _WIDE)
OP_END

/* File: c/OP_NEG_FLOAT.cpp */
HANDLE_UNOP(OP_NEG_FLOAT, "neg-float", -, R_NONE, _FLOAT)
OP_END

/* File: c/OP_NEG_DOUBLE.cpp */
HANDLE_UNOP(OP_NEG_DOUBLE, "neg-double", -,R_NONE , _DOUBLE)
OP_END

/* File: c/OP_INT_TO_LONG.cpp */
HANDLE_NUMCONV(OP_INT_TO_LONG,          "int-to-long", _INT, _WIDE)
OP_END

/* File: c/OP_INT_TO_FLOAT.cpp */
HANDLE_NUMCONV(OP_INT_TO_FLOAT,         "int-to-float", _INT, _FLOAT)
OP_END

/* File: c/OP_INT_TO_DOUBLE.cpp */
HANDLE_NUMCONV(OP_INT_TO_DOUBLE,        "int-to-double", _INT, _DOUBLE)
OP_END

/* File: c/OP_LONG_TO_INT.cpp */
HANDLE_NUMCONV(OP_LONG_TO_INT,          "long-to-int", _WIDE, _INT)
OP_END

/* File: c/OP_LONG_TO_FLOAT.cpp */
HANDLE_NUMCONV(OP_LONG_TO_FLOAT,        "long-to-float", _WIDE, _FLOAT)
OP_END

/* File: c/OP_LONG_TO_DOUBLE.cpp */
HANDLE_NUMCONV(OP_LONG_TO_DOUBLE,       "long-to-double", _WIDE, _DOUBLE)
OP_END

/* File: c/OP_FLOAT_TO_INT.cpp */
HANDLE_FLOAT_TO_INT(OP_FLOAT_TO_INT,    "float-to-int",
    float, _FLOAT, s4, _INT)
OP_END

/* File: c/OP_FLOAT_TO_LONG.cpp */
HANDLE_FLOAT_TO_INT(OP_FLOAT_TO_LONG,   "float-to-long",
    float, _FLOAT, s8, _WIDE)
OP_END

/* File: c/OP_FLOAT_TO_DOUBLE.cpp */
HANDLE_NUMCONV(OP_FLOAT_TO_DOUBLE,      "float-to-double", _FLOAT, _DOUBLE)
OP_END

/* File: c/OP_DOUBLE_TO_INT.cpp */
HANDLE_FLOAT_TO_INT(OP_DOUBLE_TO_INT,   "double-to-int",
    double, _DOUBLE, s4, _INT)
OP_END

/* File: c/OP_DOUBLE_TO_LONG.cpp */
HANDLE_FLOAT_TO_INT(OP_DOUBLE_TO_LONG,  "double-to-long",
    double, _DOUBLE, s8, _WIDE)
OP_END

/* File: c/OP_DOUBLE_TO_FLOAT.cpp */
HANDLE_NUMCONV(OP_DOUBLE_TO_FLOAT,      "double-to-float", _DOUBLE, _FLOAT)
OP_END

/* File: c/OP_INT_TO_BYTE.cpp */
HANDLE_INT_TO_SMALL(OP_INT_TO_BYTE,     "byte", s1)
OP_END

/* File: c/OP_INT_TO_CHAR.cpp */
HANDLE_INT_TO_SMALL(OP_INT_TO_CHAR,     "char", u2)
OP_END

/* File: c/OP_INT_TO_SHORT.cpp */
HANDLE_INT_TO_SMALL(OP_INT_TO_SHORT,    "short", s2)    /* want sign bit */
OP_END

/* File: c/OP_ADD_INT.cpp */
HANDLE_OP_X_INT(OP_ADD_INT, "add", +, 0)
OP_END

/* File: c/OP_SUB_INT.cpp */
HANDLE_OP_X_INT(OP_SUB_INT, "sub", -, 0)
OP_END

/* File: c/OP_MUL_INT.cpp */
HANDLE_OP_X_INT(OP_MUL_INT, "mul", *, 0)
OP_END

/* File: c/OP_DIV_INT.cpp */
HANDLE_OP_X_INT(OP_DIV_INT, "div", /, 1)
OP_END

/* File: c/OP_REM_INT.cpp */
HANDLE_OP_X_INT(OP_REM_INT, "rem", %, 2)
OP_END

/* File: c/OP_AND_INT.cpp */
HANDLE_OP_X_INT(OP_AND_INT, "and", &, 0)
OP_END

/* File: c/OP_OR_INT.cpp */
HANDLE_OP_X_INT(OP_OR_INT,  "or",  |, 0)
OP_END

/* File: c/OP_XOR_INT.cpp */
HANDLE_OP_X_INT(OP_XOR_INT, "xor", ^, 0)
OP_END

/* File: c/OP_SHL_INT.cpp */
HANDLE_OP_SHX_INT(OP_SHL_INT, "shl", (s4), <<)
OP_END

/* File: c/OP_SHR_INT.cpp */
HANDLE_OP_SHX_INT(OP_SHR_INT, "shr", (s4), >>)
OP_END

/* File: c/OP_USHR_INT.cpp */
HANDLE_OP_SHX_INT(OP_USHR_INT, "ushr", (u4), >>)
OP_END

/* File: c/OP_ADD_LONG.cpp */
HANDLE_OP_X_LONG(OP_ADD_LONG, "add", +, 0)
OP_END

/* File: c/OP_SUB_LONG.cpp */
HANDLE_OP_X_LONG(OP_SUB_LONG, "sub", -, 0)
OP_END

/* File: c/OP_MUL_LONG.cpp */
HANDLE_OP_X_LONG(OP_MUL_LONG, "mul", *, 0)
OP_END

/* File: c/OP_DIV_LONG.cpp */
HANDLE_OP_X_LONG(OP_DIV_LONG, "div", /, 1)
OP_END

/* File: c/OP_REM_LONG.cpp */
HANDLE_OP_X_LONG(OP_REM_LONG, "rem", %, 2)
OP_END

/* File: c/OP_AND_LONG.cpp */
HANDLE_OP_X_LONG(OP_AND_LONG, "and", &, 0)
OP_END

/* File: c/OP_OR_LONG.cpp */
HANDLE_OP_X_LONG(OP_OR_LONG,  "or", |, 0)
OP_END

/* File: c/OP_XOR_LONG.cpp */
HANDLE_OP_X_LONG(OP_XOR_LONG, "xor", ^, 0)
OP_END

/* File: c/OP_SHL_LONG.cpp */
HANDLE_OP_SHX_LONG(OP_SHL_LONG, "shl", (s8), <<)
OP_END

/* File: c/OP_SHR_LONG.cpp */
HANDLE_OP_SHX_LONG(OP_SHR_LONG, "shr", (s8), >>)
OP_END

/* File: c/OP_USHR_LONG.cpp */
HANDLE_OP_SHX_LONG(OP_USHR_LONG, "ushr", (u8), >>)
OP_END

/* File: c/OP_ADD_FLOAT.cpp */
HANDLE_OP_X_FLOAT(OP_ADD_FLOAT, "add", +)
OP_END

/* File: c/OP_SUB_FLOAT.cpp */
HANDLE_OP_X_FLOAT(OP_SUB_FLOAT, "sub", -)
OP_END

/* File: c/OP_MUL_FLOAT.cpp */
HANDLE_OP_X_FLOAT(OP_MUL_FLOAT, "mul", *)
OP_END

/* File: c/OP_DIV_FLOAT.cpp */
HANDLE_OP_X_FLOAT(OP_DIV_FLOAT, "div", /)
OP_END

/* File: c/OP_REM_FLOAT.cpp */
HANDLE_OPCODE(OP_REM_FLOAT /*vAA, vBB, vCC*/)
    {
        u2 srcRegs;
        vdst = INST_AA(inst);
        srcRegs = FETCH(1);
        vsrc1 = srcRegs & 0xff;
        vsrc2 = srcRegs >> 8;
#if __NIX__
        ILOGV("|%s-float v%d,v%d,v%d", "mod", vdst, vsrc1, vsrc2);
#endif
        SET_REGISTER_FLOAT(vdst,
            fmodf(GET_REGISTER_FLOAT(vsrc1), GET_REGISTER_FLOAT(vsrc2)));
    }
    FINISH(2);
OP_END

/* File: c/OP_ADD_DOUBLE.cpp */
HANDLE_OP_X_DOUBLE(OP_ADD_DOUBLE, "add", +)
OP_END

/* File: c/OP_SUB_DOUBLE.cpp */
HANDLE_OP_X_DOUBLE(OP_SUB_DOUBLE, "sub", -)
OP_END

/* File: c/OP_MUL_DOUBLE.cpp */
HANDLE_OP_X_DOUBLE(OP_MUL_DOUBLE, "mul", *)
OP_END

/* File: c/OP_DIV_DOUBLE.cpp */
HANDLE_OP_X_DOUBLE(OP_DIV_DOUBLE, "div", /)
OP_END

/* File: c/OP_REM_DOUBLE.cpp */
HANDLE_OPCODE(OP_REM_DOUBLE /*vAA, vBB, vCC*/)
    {
        u2 srcRegs;
        vdst = INST_AA(inst);
        srcRegs = FETCH(1);
        vsrc1 = srcRegs & 0xff;
        vsrc2 = srcRegs >> 8;
#if __NIX__
        ILOGV("|%s-double v%d,v%d,v%d", "mod", vdst, vsrc1, vsrc2);
#endif
        SET_REGISTER_DOUBLE(vdst,
            fmod(GET_REGISTER_DOUBLE(vsrc1), GET_REGISTER_DOUBLE(vsrc2)));
    }
    FINISH(2);
OP_END

/* File: c/OP_ADD_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_ADD_INT_2ADDR, "add", +, 0)
OP_END

/* File: c/OP_SUB_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_SUB_INT_2ADDR, "sub", -, 0)
OP_END

/* File: c/OP_MUL_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_MUL_INT_2ADDR, "mul", *, 0)
OP_END

/* File: c/OP_DIV_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_DIV_INT_2ADDR, "div", /, 1)
OP_END

/* File: c/OP_REM_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_REM_INT_2ADDR, "rem", %, 2)
OP_END

/* File: c/OP_AND_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_AND_INT_2ADDR, "and", &, 0)
OP_END

/* File: c/OP_OR_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_OR_INT_2ADDR,  "or", |, 0)
OP_END

/* File: c/OP_XOR_INT_2ADDR.cpp */
HANDLE_OP_X_INT_2ADDR(OP_XOR_INT_2ADDR, "xor", ^, 0)
OP_END

/* File: c/OP_SHL_INT_2ADDR.cpp */
HANDLE_OP_SHX_INT_2ADDR(OP_SHL_INT_2ADDR, "shl", (s4), <<)
OP_END

/* File: c/OP_SHR_INT_2ADDR.cpp */
HANDLE_OP_SHX_INT_2ADDR(OP_SHR_INT_2ADDR, "shr", (s4), >>)
OP_END

/* File: c/OP_USHR_INT_2ADDR.cpp */
HANDLE_OP_SHX_INT_2ADDR(OP_USHR_INT_2ADDR, "ushr", (u4), >>)
OP_END

/* File: c/OP_ADD_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_ADD_LONG_2ADDR, "add", +, 0)
OP_END

/* File: c/OP_SUB_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_SUB_LONG_2ADDR, "sub", -, 0)
OP_END

/* File: c/OP_MUL_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_MUL_LONG_2ADDR, "mul", *, 0)
OP_END

/* File: c/OP_DIV_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_DIV_LONG_2ADDR, "div", /, 1)
OP_END

/* File: c/OP_REM_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_REM_LONG_2ADDR, "rem", %, 2)
OP_END

/* File: c/OP_AND_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_AND_LONG_2ADDR, "and", &, 0)
OP_END

/* File: c/OP_OR_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_OR_LONG_2ADDR,  "or", |, 0)
OP_END

/* File: c/OP_XOR_LONG_2ADDR.cpp */
HANDLE_OP_X_LONG_2ADDR(OP_XOR_LONG_2ADDR, "xor", ^, 0)
OP_END

/* File: c/OP_SHL_LONG_2ADDR.cpp */
HANDLE_OP_SHX_LONG_2ADDR(OP_SHL_LONG_2ADDR, "shl", (s8), <<)
OP_END

/* File: c/OP_SHR_LONG_2ADDR.cpp */
HANDLE_OP_SHX_LONG_2ADDR(OP_SHR_LONG_2ADDR, "shr", (s8), >>)
OP_END

/* File: c/OP_USHR_LONG_2ADDR.cpp */
HANDLE_OP_SHX_LONG_2ADDR(OP_USHR_LONG_2ADDR, "ushr", (u8), >>)
OP_END

/* File: c/OP_ADD_FLOAT_2ADDR.cpp */
HANDLE_OP_X_FLOAT_2ADDR(OP_ADD_FLOAT_2ADDR, "add", +)
OP_END

/* File: c/OP_SUB_FLOAT_2ADDR.cpp */
HANDLE_OP_X_FLOAT_2ADDR(OP_SUB_FLOAT_2ADDR, "sub", -)
OP_END

/* File: c/OP_MUL_FLOAT_2ADDR.cpp */
HANDLE_OP_X_FLOAT_2ADDR(OP_MUL_FLOAT_2ADDR, "mul", *)
OP_END

/* File: c/OP_DIV_FLOAT_2ADDR.cpp */
HANDLE_OP_X_FLOAT_2ADDR(OP_DIV_FLOAT_2ADDR, "div", /)
OP_END

/* File: c/OP_REM_FLOAT_2ADDR.cpp */
HANDLE_OPCODE(OP_REM_FLOAT_2ADDR /*vA, vB*/)
    vdst = INST_A(inst);
    vsrc1 = INST_B(inst);
#if __NIX__
    ILOGV("|%s-float-2addr v%d,v%d", "mod", vdst, vsrc1);
#endif
    SET_REGISTER_FLOAT(vdst,
        fmodf(GET_REGISTER_FLOAT(vdst), GET_REGISTER_FLOAT(vsrc1)));
    FINISH(1);
OP_END

/* File: c/OP_ADD_DOUBLE_2ADDR.cpp */
HANDLE_OP_X_DOUBLE_2ADDR(OP_ADD_DOUBLE_2ADDR, "add", +)
OP_END

/* File: c/OP_SUB_DOUBLE_2ADDR.cpp */
HANDLE_OP_X_DOUBLE_2ADDR(OP_SUB_DOUBLE_2ADDR, "sub", -)
OP_END

/* File: c/OP_MUL_DOUBLE_2ADDR.cpp */
HANDLE_OP_X_DOUBLE_2ADDR(OP_MUL_DOUBLE_2ADDR, "mul", *)
OP_END

/* File: c/OP_DIV_DOUBLE_2ADDR.cpp */
HANDLE_OP_X_DOUBLE_2ADDR(OP_DIV_DOUBLE_2ADDR, "div", /)
OP_END

/* File: c/OP_REM_DOUBLE_2ADDR.cpp */
HANDLE_OPCODE(OP_REM_DOUBLE_2ADDR /*vA, vB*/)
    vdst = INST_A(inst);
    vsrc1 = INST_B(inst);
#if __NIX__
    ILOGV("|%s-double-2addr v%d,v%d", "mod", vdst, vsrc1);
#endif
    SET_REGISTER_DOUBLE(vdst,
        fmod(GET_REGISTER_DOUBLE(vdst), GET_REGISTER_DOUBLE(vsrc1)));
    FINISH(1);
OP_END

/* File: c/OP_ADD_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_ADD_INT_LIT16, "add", +, 0)
OP_END

/* File: c/OP_RSUB_INT.cpp */
HANDLE_OPCODE(OP_RSUB_INT /*vA, vB, #+CCCC*/)
    {
        vdst = INST_A(inst);
        vsrc1 = INST_B(inst);
        vsrc2 = FETCH(1);
#if __NIX__
        ILOGV("|rsub-int v%d,v%d,#+0x%04x", vdst, vsrc1, vsrc2);
#endif
        SET_REGISTER(vdst, (s2) vsrc2 - (s4) GET_REGISTER(vsrc1));
    }
    FINISH(2);
OP_END

/* File: c/OP_MUL_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_MUL_INT_LIT16, "mul", *, 0)
OP_END

/* File: c/OP_DIV_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_DIV_INT_LIT16, "div", /, 1)
OP_END

/* File: c/OP_REM_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_REM_INT_LIT16, "rem", %, 2)
OP_END

/* File: c/OP_AND_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_AND_INT_LIT16, "and", &, 0)
OP_END

/* File: c/OP_OR_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_OR_INT_LIT16,  "or",  |, 0)
OP_END

/* File: c/OP_XOR_INT_LIT16.cpp */
HANDLE_OP_X_INT_LIT16(OP_XOR_INT_LIT16, "xor", ^, 0)
OP_END

/* File: c/OP_ADD_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_ADD_INT_LIT8,   "add", +, 0)
OP_END

/* File: c/OP_RSUB_INT_LIT8.cpp */
HANDLE_OPCODE(OP_RSUB_INT_LIT8 /*vAA, vBB, #+CC*/)
    {
        u2 litInfo;
        vdst = INST_AA(inst);
        litInfo = FETCH(1);
        vsrc1 = litInfo & 0xff;
        vsrc2 = litInfo >> 8;
#if __NIX__
        ILOGV("|%s-int/lit8 v%d,v%d,#+0x%02x", "rsub", vdst, vsrc1, vsrc2);
#endif
        SET_REGISTER(vdst, (s1) vsrc2 - (s4) GET_REGISTER(vsrc1));
    }
    FINISH(2);
OP_END

/* File: c/OP_MUL_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_MUL_INT_LIT8,   "mul", *, 0)
OP_END

/* File: c/OP_DIV_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_DIV_INT_LIT8,   "div", /, 1)
OP_END

/* File: c/OP_REM_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_REM_INT_LIT8,   "rem", %, 2)
OP_END

/* File: c/OP_AND_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_AND_INT_LIT8,   "and", &, 0)
OP_END

/* File: c/OP_OR_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_OR_INT_LIT8,    "or",  |, 0)
OP_END

/* File: c/OP_XOR_INT_LIT8.cpp */
HANDLE_OP_X_INT_LIT8(OP_XOR_INT_LIT8,   "xor", ^, 0)
OP_END

/* File: c/OP_SHL_INT_LIT8.cpp */
HANDLE_OP_SHX_INT_LIT8(OP_SHL_INT_LIT8,   "shl", (s4), <<)
OP_END

/* File: c/OP_SHR_INT_LIT8.cpp */
HANDLE_OP_SHX_INT_LIT8(OP_SHR_INT_LIT8,   "shr", (s4), >>)
OP_END

/* File: c/OP_USHR_INT_LIT8.cpp */
HANDLE_OP_SHX_INT_LIT8(OP_USHR_INT_LIT8,  "ushr", (u4), >>)
OP_END

/* File: c/OP_IGET_VOLATILE.cpp */
HANDLE_IGET_X(OP_IGET_VOLATILE,         "-volatile", IntVolatile, )
OP_END

/* File: c/OP_IPUT_VOLATILE.cpp */
HANDLE_IPUT_X(OP_IPUT_VOLATILE,         "-volatile", IntVolatile, )
OP_END

/* File: c/OP_SGET_VOLATILE.cpp */
HANDLE_SGET_X(OP_SGET_VOLATILE,         "-volatile", IntVolatile, )
OP_END

/* File: c/OP_SPUT_VOLATILE.cpp */
HANDLE_SPUT_X(OP_SPUT_VOLATILE,         "-volatile", IntVolatile, )
OP_END

/* File: c/OP_IGET_OBJECT_VOLATILE.cpp */
HANDLE_IGET_X(OP_IGET_OBJECT_VOLATILE,  "-object-volatile", ObjectVolatile, _AS_OBJECT)
OP_END

/* File: c/OP_IGET_WIDE_VOLATILE.cpp */
HANDLE_IGET_X(OP_IGET_WIDE_VOLATILE,    "-wide-volatile", LongVolatile, _WIDE)
OP_END

/* File: c/OP_IPUT_WIDE_VOLATILE.cpp */
HANDLE_IPUT_X(OP_IPUT_WIDE_VOLATILE,    "-wide-volatile", LongVolatile, _WIDE)
OP_END

/* File: c/OP_SGET_WIDE_VOLATILE.cpp */
HANDLE_SGET_X(OP_SGET_WIDE_VOLATILE,    "-wide-volatile", LongVolatile, _WIDE)
OP_END

/* File: c/OP_SPUT_WIDE_VOLATILE.cpp */
HANDLE_SPUT_X(OP_SPUT_WIDE_VOLATILE,    "-wide-volatile", LongVolatile, _WIDE)
OP_END

/* File: c/OP_BREAKPOINT.cpp */
HANDLE_OPCODE(OP_BREAKPOINT)
    {
        /*
         * Restart this instruction with the original opcode.  We do
         * this by simply jumping to the handler.
         *
         * It's probably not necessary to update "inst", but we do it
         * for the sake of anything that needs to do disambiguation in a
         * common handler with INST_INST.
         *
         * The breakpoint itself is handled over in updateDebugger(),
         * because we need to detect other events (method entry, single
         * step) and report them in the same event packet, and we're not
         * yet handling those through breakpoint instructions.  By the
         * time we get here, the breakpoint has already been handled and
         * the thread resumed.
         */
        u1 originalOpcode = dvmGetOriginalOpcode(pc);
        LOGV("+++ break 0x%02x (0x%04x -> 0x%04x)", originalOpcode, inst,
            INST_REPLACE_OP(inst, originalOpcode));
        inst = INST_REPLACE_OP(inst, originalOpcode);
        FINISH_BKPT(originalOpcode);
    }
OP_END

/* File: c/OP_THROW_VERIFICATION_ERROR.cpp */
HANDLE_OPCODE(OP_THROW_VERIFICATION_ERROR)
    EXPORT_PC();
    vsrc1 = INST_AA(inst);
    ref = FETCH(1);             /* class/field/method ref */
    dvmThrowVerificationError(curMethod, vsrc1, ref);
    GOTO_exceptionThrown();
OP_END

/* File: c/OP_EXECUTE_INLINE.cpp */
HANDLE_OPCODE(OP_EXECUTE_INLINE /*vB, {vD, vE, vF, vG}, inline@CCCC*/)
    {
        /*
         * This has the same form as other method calls, but we ignore
         * the 5th argument (vA).  This is chiefly because the first four
         * arguments to a function on ARM are in registers.
         *
         * We only set the arguments that are actually used, leaving
         * the rest uninitialized.  We're assuming that, if the method
         * needs them, they'll be specified in the call.
         *
         * However, this annoys gcc when optimizations are enabled,
         * causing a "may be used uninitialized" warning.  Quieting
         * the warnings incurs a slight penalty (5%: 373ns vs. 393ns
         * on empty method).  Note that valgrind is perfectly happy
         * either way as the uninitialiezd values are never actually
         * used.
         */
        u4 arg0, arg1, arg2, arg3;
        arg0 = arg1 = arg2 = arg3 = 0;

        EXPORT_PC();

        vsrc1 = INST_B(inst);       /* #of args */
        ref = FETCH(1);             /* inline call "ref" */
        vdst = FETCH(2);            /* 0-4 register indices */
#if __NIX__
        ILOGV("|execute-inline args=%d @%d {regs=0x%04x}",
            vsrc1, ref, vdst);
#endif
        assert((vdst >> 16) == 0);  // 16-bit type -or- high 16 bits clear
        assert(vsrc1 <= 4);

        switch (vsrc1) {
        case 4:
            arg3 = GET_REGISTER(vdst >> 12);
            /* fall through */
        case 3:
            arg2 = GET_REGISTER((vdst & 0x0f00) >> 8);
            /* fall through */
        case 2:
            arg1 = GET_REGISTER((vdst & 0x00f0) >> 4);
            /* fall through */
        case 1:
            arg0 = GET_REGISTER(vdst & 0x0f);
            /* fall through */
        default:        // case 0
            ;
        }

        if (self->interpBreak.ctl.subMode & kSubModeDebuggerActive) {
            if (!dvmPerformInlineOp4Dbg(arg0, arg1, arg2, arg3, &retval, ref))
                GOTO_exceptionThrown();
        } else {
            if (!dvmPerformInlineOp4Std(arg0, arg1, arg2, arg3, &retval, ref))
                GOTO_exceptionThrown();
        }
    }
    FINISH(3);
OP_END

/* File: c/OP_EXECUTE_INLINE_RANGE.cpp */
HANDLE_OPCODE(OP_EXECUTE_INLINE_RANGE /*{vCCCC..v(CCCC+AA-1)}, inline@BBBB*/)
    {
        u4 arg0, arg1, arg2, arg3;
        arg0 = arg1 = arg2 = arg3 = 0;      /* placate gcc */

        EXPORT_PC();

        vsrc1 = INST_AA(inst);      /* #of args */
        ref = FETCH(1);             /* inline call "ref" */
        vdst = FETCH(2);            /* range base */
#if __NIX__
        ILOGV("|execute-inline-range args=%d @%d {regs=v%d-v%d}",
            vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
        assert((vdst >> 16) == 0);  // 16-bit type -or- high 16 bits clear
        assert(vsrc1 <= 4);

        switch (vsrc1) {
        case 4:
            arg3 = GET_REGISTER(vdst+3);
            /* fall through */
        case 3:
            arg2 = GET_REGISTER(vdst+2);
            /* fall through */
        case 2:
            arg1 = GET_REGISTER(vdst+1);
            /* fall through */
        case 1:
            arg0 = GET_REGISTER(vdst+0);
            /* fall through */
        default:        // case 0
            ;
        }

        if (self->interpBreak.ctl.subMode & kSubModeDebuggerActive) {
            if (!dvmPerformInlineOp4Dbg(arg0, arg1, arg2, arg3, &retval, ref))
                GOTO_exceptionThrown();
        } else {
            if (!dvmPerformInlineOp4Std(arg0, arg1, arg2, arg3, &retval, ref))
                GOTO_exceptionThrown();
        }
    }
    FINISH(3);
OP_END

/* File: c/OP_INVOKE_OBJECT_INIT_RANGE.cpp */
HANDLE_OPCODE(OP_INVOKE_OBJECT_INIT_RANGE /*{vCCCC..v(CCCC+AA-1)}, meth@BBBB*/)
    {
        Object* obj;

        vsrc1 = FETCH(2);               /* reg number of "this" pointer */
        obj = GET_REGISTER_AS_OBJECT(vsrc1);

        if (!checkForNullExportPC(obj, fp, pc))
            GOTO_exceptionThrown();

        /*
         * The object should be marked "finalizable" when Object.<init>
         * completes normally.  We're going to assume it does complete
         * (by virtue of being nothing but a return-void) and set it now.
         */
        if (IS_CLASS_FLAG_SET(obj->clazz, CLASS_ISFINALIZABLE)) {
            EXPORT_PC();
            dvmSetFinalizable(obj);
            if (dvmGetException(self))
                GOTO_exceptionThrown();
        }

        if (self->interpBreak.ctl.subMode & kSubModeDebuggerActive) {
            /* behave like OP_INVOKE_DIRECT_RANGE */
            GOTO_invoke(invokeDirect, true, false);
        }
        FINISH(3);
    }
OP_END

/* File: c/OP_RETURN_VOID_BARRIER.cpp */
HANDLE_OPCODE(OP_RETURN_VOID_BARRIER /**/)
#if __NIX__
    ILOGV("|return-void");
#endif
#ifndef NDEBUG
    retval.j = 0x0;//0xafafabab ULL;   /* placate valgrind */
#endif
    //ANDROID_MEMBAR_STORE();
    GOTO_returnFromMethod();
OP_END

/* File: c/OP_IGET_QUICK.cpp */
HANDLE_IGET_X_QUICK(OP_IGET_QUICK,          "", Int, )
OP_END

/* File: c/OP_IGET_WIDE_QUICK.cpp */
HANDLE_IGET_X_QUICK(OP_IGET_WIDE_QUICK,     "-wide", Long, _WIDE)
OP_END

/* File: c/OP_IGET_OBJECT_QUICK.cpp */
HANDLE_IGET_X_QUICK(OP_IGET_OBJECT_QUICK,   "-object", Object, _AS_OBJECT)
OP_END

/* File: c/OP_IPUT_QUICK.cpp */
HANDLE_IPUT_X_QUICK(OP_IPUT_QUICK,          "", Int, )
OP_END

/* File: c/OP_IPUT_WIDE_QUICK.cpp */
HANDLE_IPUT_X_QUICK(OP_IPUT_WIDE_QUICK,     "-wide", Long, _WIDE)
OP_END

/* File: c/OP_IPUT_OBJECT_QUICK.cpp */
HANDLE_IPUT_X_QUICK(OP_IPUT_OBJECT_QUICK,   "-object", Object, _AS_OBJECT)
OP_END

/* File: c/OP_INVOKE_VIRTUAL_QUICK.cpp */
HANDLE_OPCODE(OP_INVOKE_VIRTUAL_QUICK /*vB, {vD, vE, vF, vG, vA}, meth@CCCC*/)
    GOTO_invoke(invokeVirtualQuick, false, false);
OP_END

/* File: c/OP_INVOKE_VIRTUAL_QUICK_RANGE.cpp */
HANDLE_OPCODE(OP_INVOKE_VIRTUAL_QUICK_RANGE/*{vCCCC..v(CCCC+AA-1)}, meth@BBBB*/)
    GOTO_invoke(invokeVirtualQuick, true, false);
OP_END

/* File: c/OP_INVOKE_SUPER_QUICK.cpp */
HANDLE_OPCODE(OP_INVOKE_SUPER_QUICK /*vB, {vD, vE, vF, vG, vA}, meth@CCCC*/)
    GOTO_invoke(invokeSuperQuick, false, false);
OP_END

/* File: c/OP_INVOKE_SUPER_QUICK_RANGE.cpp */
HANDLE_OPCODE(OP_INVOKE_SUPER_QUICK_RANGE /*{vCCCC..v(CCCC+AA-1)}, meth@BBBB*/)
    GOTO_invoke(invokeSuperQuick, true, false);
OP_END

/* File: c/OP_IPUT_OBJECT_VOLATILE.cpp */
HANDLE_IPUT_X(OP_IPUT_OBJECT_VOLATILE,  "-object-volatile", ObjectVolatile, _AS_OBJECT)
OP_END

/* File: c/OP_SGET_OBJECT_VOLATILE.cpp */
HANDLE_SGET_X(OP_SGET_OBJECT_VOLATILE,  "-object-volatile", ObjectVolatile, _AS_OBJECT)
OP_END

/* File: c/OP_SPUT_OBJECT_VOLATILE.cpp */
HANDLE_SPUT_X(OP_SPUT_OBJECT_VOLATILE,  "-object-volatile", ObjectVolatile, _AS_OBJECT)
OP_END

/* File: c/OP_DISPATCH_FF.cpp */
HANDLE_OPCODE(OP_DISPATCH_FF)
    /*
     * Indicates extended opcode.  Use next 8 bits to choose where to branch.
     */
    DISPATCH_EXTENDED(INST_AA(inst));
OP_END

/* File: c/OP_CONST_CLASS_JUMBO.cpp */
HANDLE_OPCODE(OP_CONST_CLASS_JUMBO /*vBBBB, class@AAAAAAAA*/)
    {
        ClassObject* clazz;

        ref = FETCH(1) | (u4)FETCH(2) << 16;
        vdst = FETCH(3);
#if __NIX__
        ILOGV("|const-class/jumbo v%d class@0x%08x", vdst, ref);
#endif
        clazz = dvmDexGetResolvedClass(methodClassDex, ref);
        if (clazz == NULL) {
            EXPORT_PC();
            clazz = dvmResolveClass(curMethod->clazz, ref, true);
            if (clazz == NULL)
                GOTO_exceptionThrown();
        }
        SET_REGISTER(vdst, (u4) clazz);
    }
    FINISH(4);
OP_END

/* File: c/OP_CHECK_CAST_JUMBO.cpp */
HANDLE_OPCODE(OP_CHECK_CAST_JUMBO /*vBBBB, class@AAAAAAAA*/)
    {
        ClassObject* clazz;
        Object* obj;

        EXPORT_PC();

        ref = FETCH(1) | (u4)FETCH(2) << 16;     /* class to check against */
        vsrc1 = FETCH(3);
#if __NIX__
        ILOGV("|check-cast/jumbo v%d,class@0x%08x", vsrc1, ref);
#endif
        obj = (Object*)GET_REGISTER(vsrc1);
        if (obj != NULL) {
#if defined(WITH_EXTRA_OBJECT_VALIDATION)
            if (!checkForNull(obj))
                GOTO_exceptionThrown();
#endif
            clazz = dvmDexGetResolvedClass(methodClassDex, ref);
            if (clazz == NULL) {
                clazz = dvmResolveClass(curMethod->clazz, ref, false);
                if (clazz == NULL)
                    GOTO_exceptionThrown();
            }
            if (!dvmInstanceof(obj->clazz, clazz)) {
                dvmThrowClassCastException(obj->clazz, clazz);
                GOTO_exceptionThrown();
            }
        }
    }
    FINISH(4);
OP_END

/* File: c/OP_INSTANCE_OF_JUMBO.cpp */
HANDLE_OPCODE(OP_INSTANCE_OF_JUMBO /*vBBBB, vCCCC, class@AAAAAAAA*/)
    {
        ClassObject* clazz;
        Object* obj;

        ref = FETCH(1) | (u4)FETCH(2) << 16;     /* class to check against */
        vdst = FETCH(3);
        vsrc1 = FETCH(4);   /* object to check */
#if __NIX__
        ILOGV("|instance-of/jumbo v%d,v%d,class@0x%08x", vdst, vsrc1, ref);
#endif

        obj = (Object*)GET_REGISTER(vsrc1);
        if (obj == NULL) {
            SET_REGISTER(vdst, 0);
        } else {
#if defined(WITH_EXTRA_OBJECT_VALIDATION)
            if (!checkForNullExportPC(obj, fp, pc))
                GOTO_exceptionThrown();
#endif
            clazz = dvmDexGetResolvedClass(methodClassDex, ref);
            if (clazz == NULL) {
                EXPORT_PC();
                clazz = dvmResolveClass(curMethod->clazz, ref, true);
                if (clazz == NULL)
                    GOTO_exceptionThrown();
            }
            SET_REGISTER(vdst, dvmInstanceof(obj->clazz, clazz));
        }
    }
    FINISH(5);
OP_END

/* File: c/OP_NEW_INSTANCE_JUMBO.cpp */
HANDLE_OPCODE(OP_NEW_INSTANCE_JUMBO /*vBBBB, class@AAAAAAAA*/)
    {
        ClassObject* clazz;
        Object* newObj;

        EXPORT_PC();

        ref = FETCH(1) | (u4)FETCH(2) << 16;
        vdst = FETCH(3);
#if __NIX__
        ILOGV("|new-instance/jumbo v%d,class@0x%08x", vdst, ref);
#endif
        clazz = dvmDexGetResolvedClass(methodClassDex, ref);
        if (clazz == NULL) {
            clazz = dvmResolveClass(curMethod->clazz, ref, false);
            if (clazz == NULL)
                GOTO_exceptionThrown();
        }

        if (!dvmIsClassInitialized(clazz) && !dvmInitClass(clazz))
            GOTO_exceptionThrown();

#if defined(WITH_JIT)
        /*
         * The JIT needs dvmDexGetResolvedClass() to return non-null.
         * Since we use the portable interpreter to build the trace, this extra
         * check is not needed for mterp.
         */
        if ((self->interpBreak.ctl.subMode & kSubModeJitTraceBuild) &&
            (!dvmDexGetResolvedClass(methodClassDex, ref))) {
            /* Class initialization is still ongoing - end the trace */
            dvmJitEndTraceSelect(self,pc);
        }
#endif

        /*
         * Verifier now tests for interface/abstract class.
         */
        //if (dvmIsInterfaceClass(clazz) || dvmIsAbstractClass(clazz)) {
        //    dvmThrowExceptionWithClassMessage(gDvm.exInstantiationError,
        //        clazz->descriptor);
        //    GOTO_exceptionThrown();
        //}
        newObj = dvmAllocObject(clazz, ALLOC_DONT_TRACK);
        if (newObj == NULL)
            GOTO_exceptionThrown();
        SET_REGISTER(vdst, (u4) newObj);
    }
    FINISH(4);
OP_END

/* File: c/OP_NEW_ARRAY_JUMBO.cpp */
HANDLE_OPCODE(OP_NEW_ARRAY_JUMBO /*vBBBB, vCCCC, class@AAAAAAAA*/)
    {
        ClassObject* arrayClass;
        ArrayObject* newArray;
        s4 length;

        EXPORT_PC();

        ref = FETCH(1) | (u4)FETCH(2) << 16;
        vdst = FETCH(3);
        vsrc1 = FETCH(4);       /* length reg */
#if __NIX__
        ILOGV("|new-array/jumbo v%d,v%d,class@0x%08x  (%d elements)",
            vdst, vsrc1, ref, (s4) GET_REGISTER(vsrc1));
#endif
        length = (s4) GET_REGISTER(vsrc1);
        if (length < 0) {
            dvmThrowNegativeArraySizeException(length);
            GOTO_exceptionThrown();
        }
        arrayClass = dvmDexGetResolvedClass(methodClassDex, ref);
        if (arrayClass == NULL) {
            arrayClass = dvmResolveClass(curMethod->clazz, ref, false);
            if (arrayClass == NULL)
                GOTO_exceptionThrown();
        }
        /* verifier guarantees this is an array class */
        assert(dvmIsArrayClass(arrayClass));
        assert(dvmIsClassInitialized(arrayClass));

        newArray = dvmAllocArrayByClass(arrayClass, length, ALLOC_DONT_TRACK);
        if (newArray == NULL)
            GOTO_exceptionThrown();
        SET_REGISTER(vdst, (u4) newArray);
    }
    FINISH(5);
OP_END

/* File: c/OP_FILLED_NEW_ARRAY_JUMBO.cpp */
HANDLE_OPCODE(OP_FILLED_NEW_ARRAY_JUMBO /*{vCCCC..v(CCCC+BBBB-1)}, class@AAAAAAAA*/)
    GOTO_invoke(filledNewArray, true, true);
OP_END

/* File: c/OP_IGET_JUMBO.cpp */
HANDLE_IGET_X_JUMBO(OP_IGET_JUMBO,          "", Int, )
OP_END

/* File: c/OP_IGET_WIDE_JUMBO.cpp */
HANDLE_IGET_X_JUMBO(OP_IGET_WIDE_JUMBO,     "-wide", Long, _WIDE)
OP_END

/* File: c/OP_IGET_OBJECT_JUMBO.cpp */
HANDLE_IGET_X_JUMBO(OP_IGET_OBJECT_JUMBO,   "-object", Object, _AS_OBJECT)
OP_END

/* File: c/OP_IGET_BOOLEAN_JUMBO.cpp */
HANDLE_IGET_X_JUMBO(OP_IGET_BOOLEAN_JUMBO,  "", Int, )
OP_END

/* File: c/OP_IGET_BYTE_JUMBO.cpp */
HANDLE_IGET_X_JUMBO(OP_IGET_BYTE_JUMBO,     "", Int, )
OP_END

/* File: c/OP_IGET_CHAR_JUMBO.cpp */
HANDLE_IGET_X_JUMBO(OP_IGET_CHAR_JUMBO,     "", Int, )
OP_END

/* File: c/OP_IGET_SHORT_JUMBO.cpp */
HANDLE_IGET_X_JUMBO(OP_IGET_SHORT_JUMBO,    "", Int, )
OP_END

/* File: c/OP_IPUT_JUMBO.cpp */
HANDLE_IPUT_X_JUMBO(OP_IPUT_JUMBO,          "", Int, )
OP_END

/* File: c/OP_IPUT_WIDE_JUMBO.cpp */
HANDLE_IPUT_X_JUMBO(OP_IPUT_WIDE_JUMBO,     "-wide", Long, _WIDE)
OP_END

/* File: c/OP_IPUT_OBJECT_JUMBO.cpp */
/*
 * The VM spec says we should verify that the reference being stored into
 * the field is assignment compatible.  In practice, many popular VMs don't
 * do this because it slows down a very common operation.  It's not so bad
 * for us, since "dexopt" quickens it whenever possible, but it's still an
 * issue.
 *
 * To make this spec-complaint, we'd need to add a ClassObject pointer to
 * the Field struct, resolve the field's type descriptor at link or class
 * init time, and then verify the type here.
 */
HANDLE_IPUT_X_JUMBO(OP_IPUT_OBJECT_JUMBO,   "-object", Object, _AS_OBJECT)
OP_END

/* File: c/OP_IPUT_BOOLEAN_JUMBO.cpp */
HANDLE_IPUT_X_JUMBO(OP_IPUT_BOOLEAN_JUMBO,  "", Int, )
OP_END

/* File: c/OP_IPUT_BYTE_JUMBO.cpp */
HANDLE_IPUT_X_JUMBO(OP_IPUT_BYTE_JUMBO,     "", Int, )
OP_END

/* File: c/OP_IPUT_CHAR_JUMBO.cpp */
HANDLE_IPUT_X_JUMBO(OP_IPUT_CHAR_JUMBO,     "", Int, )
OP_END

/* File: c/OP_IPUT_SHORT_JUMBO.cpp */
HANDLE_IPUT_X_JUMBO(OP_IPUT_SHORT_JUMBO,    "", Int, )
OP_END

/* File: c/OP_SGET_JUMBO.cpp */
HANDLE_SGET_X_JUMBO(OP_SGET_JUMBO,          "", Int, )
OP_END

/* File: c/OP_SGET_WIDE_JUMBO.cpp */
HANDLE_SGET_X_JUMBO(OP_SGET_WIDE_JUMBO,     "-wide", Long, _WIDE)
OP_END

/* File: c/OP_SGET_OBJECT_JUMBO.cpp */
HANDLE_SGET_X_JUMBO(OP_SGET_OBJECT_JUMBO,   "-object", Object, _AS_OBJECT)
OP_END

/* File: c/OP_SGET_BOOLEAN_JUMBO.cpp */
HANDLE_SGET_X_JUMBO(OP_SGET_BOOLEAN_JUMBO,  "", Int, )
OP_END

/* File: c/OP_SGET_BYTE_JUMBO.cpp */
HANDLE_SGET_X_JUMBO(OP_SGET_BYTE_JUMBO,     "", Int, )
OP_END

/* File: c/OP_SGET_CHAR_JUMBO.cpp */
HANDLE_SGET_X_JUMBO(OP_SGET_CHAR_JUMBO,     "", Int, )
OP_END

/* File: c/OP_SGET_SHORT_JUMBO.cpp */
HANDLE_SGET_X_JUMBO(OP_SGET_SHORT_JUMBO,    "", Int, )
OP_END

/* File: c/OP_SPUT_JUMBO.cpp */
HANDLE_SPUT_X_JUMBO(OP_SPUT_JUMBO,          "", Int, )
OP_END

/* File: c/OP_SPUT_WIDE_JUMBO.cpp */
HANDLE_SPUT_X_JUMBO(OP_SPUT_WIDE_JUMBO,     "-wide", Long, _WIDE)
OP_END

/* File: c/OP_SPUT_OBJECT_JUMBO.cpp */
HANDLE_SPUT_X_JUMBO(OP_SPUT_OBJECT_JUMBO,   "-object", Object, _AS_OBJECT)
OP_END

/* File: c/OP_SPUT_BOOLEAN_JUMBO.cpp */
HANDLE_SPUT_X_JUMBO(OP_SPUT_BOOLEAN_JUMBO,          "", Int, )
OP_END

/* File: c/OP_SPUT_BYTE_JUMBO.cpp */
HANDLE_SPUT_X_JUMBO(OP_SPUT_BYTE_JUMBO,     "", Int, )
OP_END

/* File: c/OP_SPUT_CHAR_JUMBO.cpp */
HANDLE_SPUT_X_JUMBO(OP_SPUT_CHAR_JUMBO,     "", Int, )
OP_END

/* File: c/OP_SPUT_SHORT_JUMBO.cpp */
HANDLE_SPUT_X_JUMBO(OP_SPUT_SHORT_JUMBO,    "", Int, )
OP_END

/* File: c/OP_INVOKE_VIRTUAL_JUMBO.cpp */
HANDLE_OPCODE(OP_INVOKE_VIRTUAL_JUMBO /*{vCCCC..v(CCCC+BBBB-1)}, meth@AAAAAAAA*/)
    GOTO_invoke(invokeVirtual, true, true);
OP_END

/* File: c/OP_INVOKE_SUPER_JUMBO.cpp */
HANDLE_OPCODE(OP_INVOKE_SUPER_JUMBO /*{vCCCC..v(CCCC+BBBB-1)}, meth@AAAAAAAA*/)
    GOTO_invoke(invokeSuper, true, true);
OP_END

/* File: c/OP_INVOKE_DIRECT_JUMBO.cpp */
HANDLE_OPCODE(OP_INVOKE_DIRECT_JUMBO /*{vCCCC..v(CCCC+BBBB-1)}, meth@AAAAAAAA*/)
    GOTO_invoke(invokeDirect, true, true);
OP_END

/* File: c/OP_INVOKE_STATIC_JUMBO.cpp */
HANDLE_OPCODE(OP_INVOKE_STATIC_JUMBO /*{vCCCC..v(CCCC+BBBB-1)}, meth@AAAAAAAA*/)
    GOTO_invoke(invokeStatic, true, true);
OP_END

/* File: c/OP_INVOKE_INTERFACE_JUMBO.cpp */
HANDLE_OPCODE(OP_INVOKE_INTERFACE_JUMBO /*{vCCCC..v(CCCC+BBBB-1)}, meth@AAAAAAAA*/)
    GOTO_invoke(invokeInterface, true, true);
OP_END

/* File: c/OP_UNUSED_27FF.cpp */
HANDLE_OPCODE(OP_UNUSED_27FF)
OP_END

/* File: c/OP_UNUSED_28FF.cpp */
HANDLE_OPCODE(OP_UNUSED_28FF)
OP_END

/* File: c/OP_UNUSED_29FF.cpp */
HANDLE_OPCODE(OP_UNUSED_29FF)
OP_END

/* File: c/OP_UNUSED_2AFF.cpp */
HANDLE_OPCODE(OP_UNUSED_2AFF)
OP_END

/* File: c/OP_UNUSED_2BFF.cpp */
HANDLE_OPCODE(OP_UNUSED_2BFF)
OP_END

/* File: c/OP_UNUSED_2CFF.cpp */
HANDLE_OPCODE(OP_UNUSED_2CFF)
OP_END

/* File: c/OP_UNUSED_2DFF.cpp */
HANDLE_OPCODE(OP_UNUSED_2DFF)
OP_END

/* File: c/OP_UNUSED_2EFF.cpp */
HANDLE_OPCODE(OP_UNUSED_2EFF)
OP_END

/* File: c/OP_UNUSED_2FFF.cpp */
HANDLE_OPCODE(OP_UNUSED_2FFF)
OP_END

/* File: c/OP_UNUSED_30FF.cpp */
HANDLE_OPCODE(OP_UNUSED_30FF)
OP_END

/* File: c/OP_UNUSED_31FF.cpp */
HANDLE_OPCODE(OP_UNUSED_31FF)
OP_END

/* File: c/OP_UNUSED_32FF.cpp */
HANDLE_OPCODE(OP_UNUSED_32FF)
OP_END

/* File: c/OP_UNUSED_33FF.cpp */
HANDLE_OPCODE(OP_UNUSED_33FF)
OP_END

/* File: c/OP_UNUSED_34FF.cpp */
HANDLE_OPCODE(OP_UNUSED_34FF)
OP_END

/* File: c/OP_UNUSED_35FF.cpp */
HANDLE_OPCODE(OP_UNUSED_35FF)
OP_END

/* File: c/OP_UNUSED_36FF.cpp */
HANDLE_OPCODE(OP_UNUSED_36FF)
OP_END

/* File: c/OP_UNUSED_37FF.cpp */
HANDLE_OPCODE(OP_UNUSED_37FF)
OP_END

/* File: c/OP_UNUSED_38FF.cpp */
HANDLE_OPCODE(OP_UNUSED_38FF)
OP_END

/* File: c/OP_UNUSED_39FF.cpp */
HANDLE_OPCODE(OP_UNUSED_39FF)
OP_END

/* File: c/OP_UNUSED_3AFF.cpp */
HANDLE_OPCODE(OP_UNUSED_3AFF)
OP_END

/* File: c/OP_UNUSED_3BFF.cpp */
HANDLE_OPCODE(OP_UNUSED_3BFF)
OP_END

/* File: c/OP_UNUSED_3CFF.cpp */
HANDLE_OPCODE(OP_UNUSED_3CFF)
OP_END

/* File: c/OP_UNUSED_3DFF.cpp */
HANDLE_OPCODE(OP_UNUSED_3DFF)
OP_END

/* File: c/OP_UNUSED_3EFF.cpp */
HANDLE_OPCODE(OP_UNUSED_3EFF)
OP_END

/* File: c/OP_UNUSED_3FFF.cpp */
HANDLE_OPCODE(OP_UNUSED_3FFF)
OP_END

/* File: c/OP_UNUSED_40FF.cpp */
HANDLE_OPCODE(OP_UNUSED_40FF)
OP_END

/* File: c/OP_UNUSED_41FF.cpp */
HANDLE_OPCODE(OP_UNUSED_41FF)
OP_END

/* File: c/OP_UNUSED_42FF.cpp */
HANDLE_OPCODE(OP_UNUSED_42FF)
OP_END

/* File: c/OP_UNUSED_43FF.cpp */
HANDLE_OPCODE(OP_UNUSED_43FF)
OP_END

/* File: c/OP_UNUSED_44FF.cpp */
HANDLE_OPCODE(OP_UNUSED_44FF)
OP_END

/* File: c/OP_UNUSED_45FF.cpp */
HANDLE_OPCODE(OP_UNUSED_45FF)
OP_END

/* File: c/OP_UNUSED_46FF.cpp */
HANDLE_OPCODE(OP_UNUSED_46FF)
OP_END

/* File: c/OP_UNUSED_47FF.cpp */
HANDLE_OPCODE(OP_UNUSED_47FF)
OP_END

/* File: c/OP_UNUSED_48FF.cpp */
HANDLE_OPCODE(OP_UNUSED_48FF)
OP_END

/* File: c/OP_UNUSED_49FF.cpp */
HANDLE_OPCODE(OP_UNUSED_49FF)
OP_END

/* File: c/OP_UNUSED_4AFF.cpp */
HANDLE_OPCODE(OP_UNUSED_4AFF)
OP_END

/* File: c/OP_UNUSED_4BFF.cpp */
HANDLE_OPCODE(OP_UNUSED_4BFF)
OP_END

/* File: c/OP_UNUSED_4CFF.cpp */
HANDLE_OPCODE(OP_UNUSED_4CFF)
OP_END

/* File: c/OP_UNUSED_4DFF.cpp */
HANDLE_OPCODE(OP_UNUSED_4DFF)
OP_END

/* File: c/OP_UNUSED_4EFF.cpp */
HANDLE_OPCODE(OP_UNUSED_4EFF)
OP_END

/* File: c/OP_UNUSED_4FFF.cpp */
HANDLE_OPCODE(OP_UNUSED_4FFF)
OP_END

/* File: c/OP_UNUSED_50FF.cpp */
HANDLE_OPCODE(OP_UNUSED_50FF)
OP_END

/* File: c/OP_UNUSED_51FF.cpp */
HANDLE_OPCODE(OP_UNUSED_51FF)
OP_END

/* File: c/OP_UNUSED_52FF.cpp */
HANDLE_OPCODE(OP_UNUSED_52FF)
OP_END

/* File: c/OP_UNUSED_53FF.cpp */
HANDLE_OPCODE(OP_UNUSED_53FF)
OP_END

/* File: c/OP_UNUSED_54FF.cpp */
HANDLE_OPCODE(OP_UNUSED_54FF)
OP_END

/* File: c/OP_UNUSED_55FF.cpp */
HANDLE_OPCODE(OP_UNUSED_55FF)
OP_END

/* File: c/OP_UNUSED_56FF.cpp */
HANDLE_OPCODE(OP_UNUSED_56FF)
OP_END

/* File: c/OP_UNUSED_57FF.cpp */
HANDLE_OPCODE(OP_UNUSED_57FF)
OP_END

/* File: c/OP_UNUSED_58FF.cpp */
HANDLE_OPCODE(OP_UNUSED_58FF)
OP_END

/* File: c/OP_UNUSED_59FF.cpp */
HANDLE_OPCODE(OP_UNUSED_59FF)
OP_END

/* File: c/OP_UNUSED_5AFF.cpp */
HANDLE_OPCODE(OP_UNUSED_5AFF)
OP_END

/* File: c/OP_UNUSED_5BFF.cpp */
HANDLE_OPCODE(OP_UNUSED_5BFF)
OP_END

/* File: c/OP_UNUSED_5CFF.cpp */
HANDLE_OPCODE(OP_UNUSED_5CFF)
OP_END

/* File: c/OP_UNUSED_5DFF.cpp */
HANDLE_OPCODE(OP_UNUSED_5DFF)
OP_END

/* File: c/OP_UNUSED_5EFF.cpp */
HANDLE_OPCODE(OP_UNUSED_5EFF)
OP_END

/* File: c/OP_UNUSED_5FFF.cpp */
HANDLE_OPCODE(OP_UNUSED_5FFF)
OP_END

/* File: c/OP_UNUSED_60FF.cpp */
HANDLE_OPCODE(OP_UNUSED_60FF)
OP_END

/* File: c/OP_UNUSED_61FF.cpp */
HANDLE_OPCODE(OP_UNUSED_61FF)
OP_END

/* File: c/OP_UNUSED_62FF.cpp */
HANDLE_OPCODE(OP_UNUSED_62FF)
OP_END

/* File: c/OP_UNUSED_63FF.cpp */
HANDLE_OPCODE(OP_UNUSED_63FF)
OP_END

/* File: c/OP_UNUSED_64FF.cpp */
HANDLE_OPCODE(OP_UNUSED_64FF)
OP_END

/* File: c/OP_UNUSED_65FF.cpp */
HANDLE_OPCODE(OP_UNUSED_65FF)
OP_END

/* File: c/OP_UNUSED_66FF.cpp */
HANDLE_OPCODE(OP_UNUSED_66FF)
OP_END

/* File: c/OP_UNUSED_67FF.cpp */
HANDLE_OPCODE(OP_UNUSED_67FF)
OP_END

/* File: c/OP_UNUSED_68FF.cpp */
HANDLE_OPCODE(OP_UNUSED_68FF)
OP_END

/* File: c/OP_UNUSED_69FF.cpp */
HANDLE_OPCODE(OP_UNUSED_69FF)
OP_END

/* File: c/OP_UNUSED_6AFF.cpp */
HANDLE_OPCODE(OP_UNUSED_6AFF)
OP_END

/* File: c/OP_UNUSED_6BFF.cpp */
HANDLE_OPCODE(OP_UNUSED_6BFF)
OP_END

/* File: c/OP_UNUSED_6CFF.cpp */
HANDLE_OPCODE(OP_UNUSED_6CFF)
OP_END

/* File: c/OP_UNUSED_6DFF.cpp */
HANDLE_OPCODE(OP_UNUSED_6DFF)
OP_END

/* File: c/OP_UNUSED_6EFF.cpp */
HANDLE_OPCODE(OP_UNUSED_6EFF)
OP_END

/* File: c/OP_UNUSED_6FFF.cpp */
HANDLE_OPCODE(OP_UNUSED_6FFF)
OP_END

/* File: c/OP_UNUSED_70FF.cpp */
HANDLE_OPCODE(OP_UNUSED_70FF)
OP_END

/* File: c/OP_UNUSED_71FF.cpp */
HANDLE_OPCODE(OP_UNUSED_71FF)
OP_END

/* File: c/OP_UNUSED_72FF.cpp */
HANDLE_OPCODE(OP_UNUSED_72FF)
OP_END

/* File: c/OP_UNUSED_73FF.cpp */
HANDLE_OPCODE(OP_UNUSED_73FF)
OP_END

/* File: c/OP_UNUSED_74FF.cpp */
HANDLE_OPCODE(OP_UNUSED_74FF)
OP_END

/* File: c/OP_UNUSED_75FF.cpp */
HANDLE_OPCODE(OP_UNUSED_75FF)
OP_END

/* File: c/OP_UNUSED_76FF.cpp */
HANDLE_OPCODE(OP_UNUSED_76FF)
OP_END

/* File: c/OP_UNUSED_77FF.cpp */
HANDLE_OPCODE(OP_UNUSED_77FF)
OP_END

/* File: c/OP_UNUSED_78FF.cpp */
HANDLE_OPCODE(OP_UNUSED_78FF)
OP_END

/* File: c/OP_UNUSED_79FF.cpp */
HANDLE_OPCODE(OP_UNUSED_79FF)
OP_END

/* File: c/OP_UNUSED_7AFF.cpp */
HANDLE_OPCODE(OP_UNUSED_7AFF)
OP_END

/* File: c/OP_UNUSED_7BFF.cpp */
HANDLE_OPCODE(OP_UNUSED_7BFF)
OP_END

/* File: c/OP_UNUSED_7CFF.cpp */
HANDLE_OPCODE(OP_UNUSED_7CFF)
OP_END

/* File: c/OP_UNUSED_7DFF.cpp */
HANDLE_OPCODE(OP_UNUSED_7DFF)
OP_END

/* File: c/OP_UNUSED_7EFF.cpp */
HANDLE_OPCODE(OP_UNUSED_7EFF)
OP_END

/* File: c/OP_UNUSED_7FFF.cpp */
HANDLE_OPCODE(OP_UNUSED_7FFF)
OP_END

/* File: c/OP_UNUSED_80FF.cpp */
HANDLE_OPCODE(OP_UNUSED_80FF)
OP_END

/* File: c/OP_UNUSED_81FF.cpp */
HANDLE_OPCODE(OP_UNUSED_81FF)
OP_END

/* File: c/OP_UNUSED_82FF.cpp */
HANDLE_OPCODE(OP_UNUSED_82FF)
OP_END

/* File: c/OP_UNUSED_83FF.cpp */
HANDLE_OPCODE(OP_UNUSED_83FF)
OP_END

/* File: c/OP_UNUSED_84FF.cpp */
HANDLE_OPCODE(OP_UNUSED_84FF)
OP_END

/* File: c/OP_UNUSED_85FF.cpp */
HANDLE_OPCODE(OP_UNUSED_85FF)
OP_END

/* File: c/OP_UNUSED_86FF.cpp */
HANDLE_OPCODE(OP_UNUSED_86FF)
OP_END

/* File: c/OP_UNUSED_87FF.cpp */
HANDLE_OPCODE(OP_UNUSED_87FF)
OP_END

/* File: c/OP_UNUSED_88FF.cpp */
HANDLE_OPCODE(OP_UNUSED_88FF)
OP_END

/* File: c/OP_UNUSED_89FF.cpp */
HANDLE_OPCODE(OP_UNUSED_89FF)
OP_END

/* File: c/OP_UNUSED_8AFF.cpp */
HANDLE_OPCODE(OP_UNUSED_8AFF)
OP_END

/* File: c/OP_UNUSED_8BFF.cpp */
HANDLE_OPCODE(OP_UNUSED_8BFF)
OP_END

/* File: c/OP_UNUSED_8CFF.cpp */
HANDLE_OPCODE(OP_UNUSED_8CFF)
OP_END

/* File: c/OP_UNUSED_8DFF.cpp */
HANDLE_OPCODE(OP_UNUSED_8DFF)
OP_END

/* File: c/OP_UNUSED_8EFF.cpp */
HANDLE_OPCODE(OP_UNUSED_8EFF)
OP_END

/* File: c/OP_UNUSED_8FFF.cpp */
HANDLE_OPCODE(OP_UNUSED_8FFF)
OP_END

/* File: c/OP_UNUSED_90FF.cpp */
HANDLE_OPCODE(OP_UNUSED_90FF)
OP_END

/* File: c/OP_UNUSED_91FF.cpp */
HANDLE_OPCODE(OP_UNUSED_91FF)
OP_END

/* File: c/OP_UNUSED_92FF.cpp */
HANDLE_OPCODE(OP_UNUSED_92FF)
OP_END

/* File: c/OP_UNUSED_93FF.cpp */
HANDLE_OPCODE(OP_UNUSED_93FF)
OP_END

/* File: c/OP_UNUSED_94FF.cpp */
HANDLE_OPCODE(OP_UNUSED_94FF)
OP_END

/* File: c/OP_UNUSED_95FF.cpp */
HANDLE_OPCODE(OP_UNUSED_95FF)
OP_END

/* File: c/OP_UNUSED_96FF.cpp */
HANDLE_OPCODE(OP_UNUSED_96FF)
OP_END

/* File: c/OP_UNUSED_97FF.cpp */
HANDLE_OPCODE(OP_UNUSED_97FF)
OP_END

/* File: c/OP_UNUSED_98FF.cpp */
HANDLE_OPCODE(OP_UNUSED_98FF)
OP_END

/* File: c/OP_UNUSED_99FF.cpp */
HANDLE_OPCODE(OP_UNUSED_99FF)
OP_END

/* File: c/OP_UNUSED_9AFF.cpp */
HANDLE_OPCODE(OP_UNUSED_9AFF)
OP_END

/* File: c/OP_UNUSED_9BFF.cpp */
HANDLE_OPCODE(OP_UNUSED_9BFF)
OP_END

/* File: c/OP_UNUSED_9CFF.cpp */
HANDLE_OPCODE(OP_UNUSED_9CFF)
OP_END

/* File: c/OP_UNUSED_9DFF.cpp */
HANDLE_OPCODE(OP_UNUSED_9DFF)
OP_END

/* File: c/OP_UNUSED_9EFF.cpp */
HANDLE_OPCODE(OP_UNUSED_9EFF)
OP_END

/* File: c/OP_UNUSED_9FFF.cpp */
HANDLE_OPCODE(OP_UNUSED_9FFF)
OP_END

/* File: c/OP_UNUSED_A0FF.cpp */
HANDLE_OPCODE(OP_UNUSED_A0FF)
OP_END

/* File: c/OP_UNUSED_A1FF.cpp */
HANDLE_OPCODE(OP_UNUSED_A1FF)
OP_END

/* File: c/OP_UNUSED_A2FF.cpp */
HANDLE_OPCODE(OP_UNUSED_A2FF)
OP_END

/* File: c/OP_UNUSED_A3FF.cpp */
HANDLE_OPCODE(OP_UNUSED_A3FF)
OP_END

/* File: c/OP_UNUSED_A4FF.cpp */
HANDLE_OPCODE(OP_UNUSED_A4FF)
OP_END

/* File: c/OP_UNUSED_A5FF.cpp */
HANDLE_OPCODE(OP_UNUSED_A5FF)
OP_END

/* File: c/OP_UNUSED_A6FF.cpp */
HANDLE_OPCODE(OP_UNUSED_A6FF)
OP_END

/* File: c/OP_UNUSED_A7FF.cpp */
HANDLE_OPCODE(OP_UNUSED_A7FF)
OP_END

/* File: c/OP_UNUSED_A8FF.cpp */
HANDLE_OPCODE(OP_UNUSED_A8FF)
OP_END

/* File: c/OP_UNUSED_A9FF.cpp */
HANDLE_OPCODE(OP_UNUSED_A9FF)
OP_END

/* File: c/OP_UNUSED_AAFF.cpp */
HANDLE_OPCODE(OP_UNUSED_AAFF)
OP_END

/* File: c/OP_UNUSED_ABFF.cpp */
HANDLE_OPCODE(OP_UNUSED_ABFF)
OP_END

/* File: c/OP_UNUSED_ACFF.cpp */
HANDLE_OPCODE(OP_UNUSED_ACFF)
OP_END

/* File: c/OP_UNUSED_ADFF.cpp */
HANDLE_OPCODE(OP_UNUSED_ADFF)
OP_END

/* File: c/OP_UNUSED_AEFF.cpp */
HANDLE_OPCODE(OP_UNUSED_AEFF)
OP_END

/* File: c/OP_UNUSED_AFFF.cpp */
HANDLE_OPCODE(OP_UNUSED_AFFF)
OP_END

/* File: c/OP_UNUSED_B0FF.cpp */
HANDLE_OPCODE(OP_UNUSED_B0FF)
OP_END

/* File: c/OP_UNUSED_B1FF.cpp */
HANDLE_OPCODE(OP_UNUSED_B1FF)
OP_END

/* File: c/OP_UNUSED_B2FF.cpp */
HANDLE_OPCODE(OP_UNUSED_B2FF)
OP_END

/* File: c/OP_UNUSED_B3FF.cpp */
HANDLE_OPCODE(OP_UNUSED_B3FF)
OP_END

/* File: c/OP_UNUSED_B4FF.cpp */
HANDLE_OPCODE(OP_UNUSED_B4FF)
OP_END

/* File: c/OP_UNUSED_B5FF.cpp */
HANDLE_OPCODE(OP_UNUSED_B5FF)
OP_END

/* File: c/OP_UNUSED_B6FF.cpp */
HANDLE_OPCODE(OP_UNUSED_B6FF)
OP_END

/* File: c/OP_UNUSED_B7FF.cpp */
HANDLE_OPCODE(OP_UNUSED_B7FF)
OP_END

/* File: c/OP_UNUSED_B8FF.cpp */
HANDLE_OPCODE(OP_UNUSED_B8FF)
OP_END

/* File: c/OP_UNUSED_B9FF.cpp */
HANDLE_OPCODE(OP_UNUSED_B9FF)
OP_END

/* File: c/OP_UNUSED_BAFF.cpp */
HANDLE_OPCODE(OP_UNUSED_BAFF)
OP_END

/* File: c/OP_UNUSED_BBFF.cpp */
HANDLE_OPCODE(OP_UNUSED_BBFF)
OP_END

/* File: c/OP_UNUSED_BCFF.cpp */
HANDLE_OPCODE(OP_UNUSED_BCFF)
OP_END

/* File: c/OP_UNUSED_BDFF.cpp */
HANDLE_OPCODE(OP_UNUSED_BDFF)
OP_END

/* File: c/OP_UNUSED_BEFF.cpp */
HANDLE_OPCODE(OP_UNUSED_BEFF)
OP_END

/* File: c/OP_UNUSED_BFFF.cpp */
HANDLE_OPCODE(OP_UNUSED_BFFF)
OP_END

/* File: c/OP_UNUSED_C0FF.cpp */
HANDLE_OPCODE(OP_UNUSED_C0FF)
OP_END

/* File: c/OP_UNUSED_C1FF.cpp */
HANDLE_OPCODE(OP_UNUSED_C1FF)
OP_END

/* File: c/OP_UNUSED_C2FF.cpp */
HANDLE_OPCODE(OP_UNUSED_C2FF)
OP_END

/* File: c/OP_UNUSED_C3FF.cpp */
HANDLE_OPCODE(OP_UNUSED_C3FF)
OP_END

/* File: c/OP_UNUSED_C4FF.cpp */
HANDLE_OPCODE(OP_UNUSED_C4FF)
OP_END

/* File: c/OP_UNUSED_C5FF.cpp */
HANDLE_OPCODE(OP_UNUSED_C5FF)
OP_END

/* File: c/OP_UNUSED_C6FF.cpp */
HANDLE_OPCODE(OP_UNUSED_C6FF)
OP_END

/* File: c/OP_UNUSED_C7FF.cpp */
HANDLE_OPCODE(OP_UNUSED_C7FF)
OP_END

/* File: c/OP_UNUSED_C8FF.cpp */
HANDLE_OPCODE(OP_UNUSED_C8FF)
OP_END

/* File: c/OP_UNUSED_C9FF.cpp */
HANDLE_OPCODE(OP_UNUSED_C9FF)
OP_END

/* File: c/OP_UNUSED_CAFF.cpp */
HANDLE_OPCODE(OP_UNUSED_CAFF)
OP_END

/* File: c/OP_UNUSED_CBFF.cpp */
HANDLE_OPCODE(OP_UNUSED_CBFF)
OP_END

/* File: c/OP_UNUSED_CCFF.cpp */
HANDLE_OPCODE(OP_UNUSED_CCFF)
OP_END

/* File: c/OP_UNUSED_CDFF.cpp */
HANDLE_OPCODE(OP_UNUSED_CDFF)
OP_END

/* File: c/OP_UNUSED_CEFF.cpp */
HANDLE_OPCODE(OP_UNUSED_CEFF)
OP_END

/* File: c/OP_UNUSED_CFFF.cpp */
HANDLE_OPCODE(OP_UNUSED_CFFF)
OP_END

/* File: c/OP_UNUSED_D0FF.cpp */
HANDLE_OPCODE(OP_UNUSED_D0FF)
OP_END

/* File: c/OP_UNUSED_D1FF.cpp */
HANDLE_OPCODE(OP_UNUSED_D1FF)
OP_END

/* File: c/OP_UNUSED_D2FF.cpp */
HANDLE_OPCODE(OP_UNUSED_D2FF)
OP_END

/* File: c/OP_UNUSED_D3FF.cpp */
HANDLE_OPCODE(OP_UNUSED_D3FF)
OP_END

/* File: c/OP_UNUSED_D4FF.cpp */
HANDLE_OPCODE(OP_UNUSED_D4FF)
OP_END

/* File: c/OP_UNUSED_D5FF.cpp */
HANDLE_OPCODE(OP_UNUSED_D5FF)
OP_END

/* File: c/OP_UNUSED_D6FF.cpp */
HANDLE_OPCODE(OP_UNUSED_D6FF)
OP_END

/* File: c/OP_UNUSED_D7FF.cpp */
HANDLE_OPCODE(OP_UNUSED_D7FF)
OP_END

/* File: c/OP_UNUSED_D8FF.cpp */
HANDLE_OPCODE(OP_UNUSED_D8FF)
OP_END

/* File: c/OP_UNUSED_D9FF.cpp */
HANDLE_OPCODE(OP_UNUSED_D9FF)
OP_END

/* File: c/OP_UNUSED_DAFF.cpp */
HANDLE_OPCODE(OP_UNUSED_DAFF)
OP_END

/* File: c/OP_UNUSED_DBFF.cpp */
HANDLE_OPCODE(OP_UNUSED_DBFF)
OP_END

/* File: c/OP_UNUSED_DCFF.cpp */
HANDLE_OPCODE(OP_UNUSED_DCFF)
OP_END

/* File: c/OP_UNUSED_DDFF.cpp */
HANDLE_OPCODE(OP_UNUSED_DDFF)
OP_END

/* File: c/OP_UNUSED_DEFF.cpp */
HANDLE_OPCODE(OP_UNUSED_DEFF)
OP_END

/* File: c/OP_UNUSED_DFFF.cpp */
HANDLE_OPCODE(OP_UNUSED_DFFF)
OP_END

/* File: c/OP_UNUSED_E0FF.cpp */
HANDLE_OPCODE(OP_UNUSED_E0FF)
OP_END

/* File: c/OP_UNUSED_E1FF.cpp */
HANDLE_OPCODE(OP_UNUSED_E1FF)
OP_END

/* File: c/OP_UNUSED_E2FF.cpp */
HANDLE_OPCODE(OP_UNUSED_E2FF)
OP_END

/* File: c/OP_UNUSED_E3FF.cpp */
HANDLE_OPCODE(OP_UNUSED_E3FF)
OP_END

/* File: c/OP_UNUSED_E4FF.cpp */
HANDLE_OPCODE(OP_UNUSED_E4FF)
OP_END

/* File: c/OP_UNUSED_E5FF.cpp */
HANDLE_OPCODE(OP_UNUSED_E5FF)
OP_END

/* File: c/OP_UNUSED_E6FF.cpp */
HANDLE_OPCODE(OP_UNUSED_E6FF)
OP_END

/* File: c/OP_UNUSED_E7FF.cpp */
HANDLE_OPCODE(OP_UNUSED_E7FF)
OP_END

/* File: c/OP_UNUSED_E8FF.cpp */
HANDLE_OPCODE(OP_UNUSED_E8FF)
OP_END

/* File: c/OP_UNUSED_E9FF.cpp */
HANDLE_OPCODE(OP_UNUSED_E9FF)
OP_END

/* File: c/OP_UNUSED_EAFF.cpp */
HANDLE_OPCODE(OP_UNUSED_EAFF)
OP_END

/* File: c/OP_UNUSED_EBFF.cpp */
HANDLE_OPCODE(OP_UNUSED_EBFF)
OP_END

/* File: c/OP_UNUSED_ECFF.cpp */
HANDLE_OPCODE(OP_UNUSED_ECFF)
OP_END

/* File: c/OP_UNUSED_EDFF.cpp */
HANDLE_OPCODE(OP_UNUSED_EDFF)
OP_END

/* File: c/OP_UNUSED_EEFF.cpp */
HANDLE_OPCODE(OP_UNUSED_EEFF)
OP_END

/* File: c/OP_UNUSED_EFFF.cpp */
HANDLE_OPCODE(OP_UNUSED_EFFF)
OP_END

/* File: c/OP_UNUSED_F0FF.cpp */
HANDLE_OPCODE(OP_UNUSED_F0FF)
OP_END

/* File: c/OP_UNUSED_F1FF.cpp */
HANDLE_OPCODE(OP_UNUSED_F1FF)
    /*
     * In portable interp, most unused opcodes will fall through to here.
     */
    LOGE("unknown opcode 0x%04x", inst);
    dvmAbort();
    FINISH(1);
OP_END

/* File: c/OP_INVOKE_OBJECT_INIT_JUMBO.cpp */
HANDLE_OPCODE(OP_INVOKE_OBJECT_INIT_JUMBO /*{vCCCC..vNNNN}, meth@AAAAAAAA*/)
    {
        Object* obj;

        vsrc1 = FETCH(4);               /* reg number of "this" pointer */
        obj = GET_REGISTER_AS_OBJECT(vsrc1);

        if (!checkForNullExportPC(obj, fp, pc))
            GOTO_exceptionThrown();

        /*
         * The object should be marked "finalizable" when Object.<init>
         * completes normally.  We're going to assume it does complete
         * (by virtue of being nothing but a return-void) and set it now.
         */
        if (IS_CLASS_FLAG_SET(obj->clazz, CLASS_ISFINALIZABLE)) {
            EXPORT_PC();
            dvmSetFinalizable(obj);
            if (dvmGetException(self))
                GOTO_exceptionThrown();
        }

        if (self->interpBreak.ctl.subMode & kSubModeDebuggerActive) {
            /* behave like OP_INVOKE_DIRECT_RANGE */
            GOTO_invoke(invokeDirect, true, true);
        }
        FINISH(5);
    }
OP_END

/* File: c/OP_IGET_VOLATILE_JUMBO.cpp */
HANDLE_IGET_X_JUMBO(OP_IGET_VOLATILE_JUMBO, "-volatile/jumbo", IntVolatile, )
OP_END

/* File: c/OP_IGET_WIDE_VOLATILE_JUMBO.cpp */
HANDLE_IGET_X_JUMBO(OP_IGET_WIDE_VOLATILE_JUMBO, "-wide-volatile/jumbo", LongVolatile, _WIDE)
OP_END

/* File: c/OP_IGET_OBJECT_VOLATILE_JUMBO.cpp */
HANDLE_IGET_X_JUMBO(OP_IGET_OBJECT_VOLATILE_JUMBO, "-object-volatile/jumbo", ObjectVolatile, _AS_OBJECT)
OP_END

/* File: c/OP_IPUT_VOLATILE_JUMBO.cpp */
HANDLE_IPUT_X_JUMBO(OP_IPUT_VOLATILE_JUMBO, "-volatile/jumbo", IntVolatile, )
OP_END

/* File: c/OP_IPUT_WIDE_VOLATILE_JUMBO.cpp */
HANDLE_IPUT_X_JUMBO(OP_IPUT_WIDE_VOLATILE_JUMBO, "-wide-volatile/jumbo", LongVolatile, _WIDE)
OP_END

/* File: c/OP_IPUT_OBJECT_VOLATILE_JUMBO.cpp */
HANDLE_IPUT_X_JUMBO(OP_IPUT_OBJECT_VOLATILE_JUMBO, "-object-volatile/jumbo", ObjectVolatile, _AS_OBJECT)
OP_END

/* File: c/OP_SGET_VOLATILE_JUMBO.cpp */
HANDLE_SGET_X_JUMBO(OP_SGET_VOLATILE_JUMBO, "-volatile/jumbo", IntVolatile, )
OP_END

/* File: c/OP_SGET_WIDE_VOLATILE_JUMBO.cpp */
HANDLE_SGET_X_JUMBO(OP_SGET_WIDE_VOLATILE_JUMBO, "-wide-volatile/jumbo", LongVolatile, _WIDE)
OP_END

/* File: c/OP_SGET_OBJECT_VOLATILE_JUMBO.cpp */
HANDLE_SGET_X_JUMBO(OP_SGET_OBJECT_VOLATILE_JUMBO, "-object-volatile/jumbo", ObjectVolatile, _AS_OBJECT)
OP_END

/* File: c/OP_SPUT_VOLATILE_JUMBO.cpp */
HANDLE_SPUT_X_JUMBO(OP_SPUT_VOLATILE_JUMBO, "-volatile", IntVolatile, )
OP_END

/* File: c/OP_SPUT_WIDE_VOLATILE_JUMBO.cpp */
HANDLE_SPUT_X_JUMBO(OP_SPUT_WIDE_VOLATILE_JUMBO, "-wide-volatile/jumbo", LongVolatile, _WIDE)
OP_END

/* File: c/OP_SPUT_OBJECT_VOLATILE_JUMBO.cpp */
HANDLE_SPUT_X_JUMBO(OP_SPUT_OBJECT_VOLATILE_JUMBO, "-object-volatile/jumbo", ObjectVolatile, _AS_OBJECT)
OP_END

/* File: c/OP_THROW_VERIFICATION_ERROR_JUMBO.cpp */
HANDLE_OPCODE(OP_THROW_VERIFICATION_ERROR_JUMBO)
    EXPORT_PC();
    vsrc1 = FETCH(3);
    ref = FETCH(1) | (u4)FETCH(2) << 16;      /* class/field/method ref */
    dvmThrowVerificationError(curMethod, vsrc1, ref);
    GOTO_exceptionThrown();
OP_END

/* File: c/gotoTargets.cpp */
/*
 * C footer.  This has some common code shared by the various targets.
 */

/*
 * Everything from here on is a "goto target".  In the basic interpreter
 * we jump into these targets and then jump directly to the handler for
 * next instruction.  Here, these are subroutines that return to the caller.
 */

GOTO_TARGET(filledNewArray)
    {
        ClassObject* arrayClass;
        ArrayObject* newArray;
        u4* contents;
        char typeCh;
        int i;
        u4 arg5;

        EXPORT_PC();

        if (jumboFormat) {
            ref = FETCH(1) | (u4)FETCH(2) << 16;  /* class ref */
            vsrc1 = FETCH(3);                     /* #of elements */
            vdst = FETCH(4);                      /* range base */
            arg5 = -1;                            /* silence compiler warning */
#if __NIX__
            ILOGV("|filled-new-array/jumbo args=%d @0x%08x {regs=v%d-v%d}",
                vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
        } else {
            ref = FETCH(1);             /* class ref */
            vdst = FETCH(2);            /* first 4 regs -or- range base */

            if (methodCallRange) {
                vsrc1 = INST_AA(inst);  /* #of elements */
                arg5 = -1;              /* silence compiler warning */
#if __NIX__
                ILOGV("|filled-new-array-range args=%d @0x%04x {regs=v%d-v%d}",
                    vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
            } else {
                arg5 = INST_A(inst);
                vsrc1 = INST_B(inst);   /* #of elements */
#if __NIX__
                ILOGV("|filled-new-array args=%d @0x%04x {regs=0x%04x %x}",
                   vsrc1, ref, vdst, arg5);
#endif
            }
        }

        /*
         * Resolve the array class.
         */
        arrayClass = dvmDexGetResolvedClass(methodClassDex, ref);
        if (arrayClass == NULL) {
            arrayClass = dvmResolveClass(curMethod->clazz, ref, false);
            if (arrayClass == NULL)
                GOTO_exceptionThrown();
        }
        /*
        if (!dvmIsArrayClass(arrayClass)) {
            dvmThrowRuntimeException(
                "filled-new-array needs array class");
            GOTO_exceptionThrown();
        }
        */
        /* verifier guarantees this is an array class */
        assert(dvmIsArrayClass(arrayClass));
        assert(dvmIsClassInitialized(arrayClass));

        /*
         * Create an array of the specified type.
         */
#if __NIX__         
        LOGVV("+++ filled-new-array type is '%s'", arrayClass->descriptor);
#endif
        typeCh = arrayClass->descriptor[1];
        if (typeCh == 'D' || typeCh == 'J') {
            /* category 2 primitives not allowed */
            dvmThrowRuntimeException("bad filled array req");
            GOTO_exceptionThrown();
        } else if (typeCh != 'L' && typeCh != '[' && typeCh != 'I') {
            /* TODO: requires multiple "fill in" loops with different widths */
            LOGE("non-int primitives not implemented");
            dvmThrowInternalError(
                "filled-new-array not implemented for anything but 'int'");
            GOTO_exceptionThrown();
        }

        newArray = dvmAllocArrayByClass(arrayClass, vsrc1, ALLOC_DONT_TRACK);
        if (newArray == NULL)
            GOTO_exceptionThrown();

        /*
         * Fill in the elements.  It's legal for vsrc1 to be zero.
         */
        contents = (u4*)(void*)newArray->contents;
        if (methodCallRange) {
            for (i = 0; i < vsrc1; i++)
                contents[i] = GET_REGISTER(vdst+i);
        } else {
            assert(vsrc1 <= 5);
            if (vsrc1 == 5) {
                contents[4] = GET_REGISTER(arg5);
                vsrc1--;
            }
            for (i = 0; i < vsrc1; i++) {
                contents[i] = GET_REGISTER(vdst & 0x0f);
                vdst >>= 4;
            }
        }
        if (typeCh == 'L' || typeCh == '[') {
            dvmWriteBarrierArray(newArray, 0, newArray->length);
        }

        retval.l = (Object*)newArray;
    }
    if (jumboFormat) {
        FINISH(5);
    } else {
        FINISH(3);
    }
GOTO_TARGET_END


GOTO_TARGET(invokeVirtual)
    {
        Method* baseMethod;
        Object* thisPtr;

        EXPORT_PC();

        if (jumboFormat) {
            ref = FETCH(1) | (u4)FETCH(2) << 16;  /* method ref */
            vsrc1 = FETCH(3);                     /* count */
            vdst = FETCH(4);                      /* first reg */
            ADJUST_PC(2);     /* advance pc partially to make returns easier */
#if __NIX__
            ILOGV("|invoke-virtual/jumbo args=%d @0x%08x {regs=v%d-v%d}",
                vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
            thisPtr = (Object*) GET_REGISTER(vdst);
        } else {
            vsrc1 = INST_AA(inst);      /* AA (count) or BA (count + arg 5) */
            ref = FETCH(1);             /* method ref */
            vdst = FETCH(2);            /* 4 regs -or- first reg */

            /*
             * The object against which we are executing a method is always
             * in the first argument.
             */
            if (methodCallRange) {
                assert(vsrc1 > 0);
#if __NIX__
                ILOGV("|invoke-virtual-range args=%d @0x%04x {regs=v%d-v%d}",
                    vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
                thisPtr = (Object*) GET_REGISTER(vdst);
            } else {
                assert((vsrc1>>4) > 0);
#if __NIX__
                ILOGV("|invoke-virtual args=%d @0x%04x {regs=0x%04x %x}",
                    vsrc1 >> 4, ref, vdst, vsrc1 & 0x0f);
#endif
                thisPtr = (Object*) GET_REGISTER(vdst & 0x0f);
            }
        }

        if (!checkForNull(thisPtr))
            GOTO_exceptionThrown();

        /*
         * Resolve the method.  This is the correct method for the static
         * type of the object.  We also verify access permissions here.
         */
        baseMethod = dvmDexGetResolvedMethod(methodClassDex, ref);
        if (baseMethod == NULL) {
            baseMethod = dvmResolveMethod(curMethod->clazz, ref,METHOD_VIRTUAL);
            if (baseMethod == NULL) {
#if __NIX__
                ILOGV("+ unknown method or access denied");
#endif
                GOTO_exceptionThrown();
            }
        }

        /*
         * Combine the object we found with the vtable offset in the
         * method.
         */
        assert(baseMethod->methodIndex < thisPtr->clazz->vtableCount);
        methodToCall = thisPtr->clazz->vtable[baseMethod->methodIndex];

#if defined(WITH_JIT) && defined(MTERP_STUB)
        self->methodToCall = methodToCall;
        self->callsiteClass = thisPtr->clazz;
#endif

#if 0
        if (dvmIsAbstractMethod(methodToCall)) {
            /*
             * This can happen if you create two classes, Base and Sub, where
             * Sub is a sub-class of Base.  Declare a protected abstract
             * method foo() in Base, and invoke foo() from a method in Base.
             * Base is an "abstract base class" and is never instantiated
             * directly.  Now, Override foo() in Sub, and use Sub.  This
             * Works fine unless Sub stops providing an implementation of
             * the method.
             */
            dvmThrowAbstractMethodError("abstract method not implemented");
            GOTO_exceptionThrown();
        }
#else
		#if __NIX__
        assert(!dvmIsAbstractMethod(methodToCall) || methodToCall->nativeFunc != NULL);
        #endif
#endif
#if __NIX__
        LOGVV("+++ base=%s.%s virtual[%d]=%s.%s",
            baseMethod->clazz->descriptor, baseMethod->name,
            (u4) baseMethod->methodIndex,
            methodToCall->clazz->descriptor, methodToCall->name);
#endif
        assert(methodToCall != NULL);

#if 0
        if (vsrc1 != methodToCall->insSize) {
            LOGW("WRONG METHOD: base=%s.%s virtual[%d]=%s.%s",
                baseMethod->clazz->descriptor, baseMethod->name,
                (u4) baseMethod->methodIndex,
                methodToCall->clazz->descriptor, methodToCall->name);
            //dvmDumpClass(baseMethod->clazz);
            //dvmDumpClass(methodToCall->clazz);
            dvmDumpAllClasses(0);
        }
#endif

        GOTO_invokeMethod(methodCallRange, methodToCall, vsrc1, vdst);
    }
GOTO_TARGET_END

GOTO_TARGET(invokeSuper)
    {
        Method* baseMethod;
        u2 thisReg;

        EXPORT_PC();

        if (jumboFormat) {
            ref = FETCH(1) | (u4)FETCH(2) << 16;  /* method ref */
            vsrc1 = FETCH(3);                     /* count */
            vdst = FETCH(4);                      /* first reg */
            ADJUST_PC(2);     /* advance pc partially to make returns easier */
#if __NIX__
            ILOGV("|invoke-super/jumbo args=%d @0x%08x {regs=v%d-v%d}",
                vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
            thisReg = vdst;
        } else {
            vsrc1 = INST_AA(inst);      /* AA (count) or BA (count + arg 5) */
            ref = FETCH(1);             /* method ref */
            vdst = FETCH(2);            /* 4 regs -or- first reg */

            if (methodCallRange) {
#if __NIX__
                ILOGV("|invoke-super-range args=%d @0x%04x {regs=v%d-v%d}",
                    vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
                thisReg = vdst;
            } else {
#if __NIX__
                ILOGV("|invoke-super args=%d @0x%04x {regs=0x%04x %x}",
                    vsrc1 >> 4, ref, vdst, vsrc1 & 0x0f);
#endif
                thisReg = vdst & 0x0f;
            }
        }

        /* impossible in well-formed code, but we must check nevertheless */
        if (!checkForNull((Object*) GET_REGISTER(thisReg)))
            GOTO_exceptionThrown();

        /*
         * Resolve the method.  This is the correct method for the static
         * type of the object.  We also verify access permissions here.
         * The first arg to dvmResolveMethod() is just the referring class
         * (used for class loaders and such), so we don't want to pass
         * the superclass into the resolution call.
         */
        baseMethod = dvmDexGetResolvedMethod(methodClassDex, ref);
        if (baseMethod == NULL) {
            baseMethod = dvmResolveMethod(curMethod->clazz, ref,METHOD_VIRTUAL);
            if (baseMethod == NULL) {
#if __NIX__
                ILOGV("+ unknown method or access denied");
#endif
                GOTO_exceptionThrown();
            }
        }

        /*
         * Combine the object we found with the vtable offset in the
         * method's class.
         *
         * We're using the current method's class' superclass, not the
         * superclass of "this".  This is because we might be executing
         * in a method inherited from a superclass, and we want to run
         * in that class' superclass.
         */
        if (baseMethod->methodIndex >= curMethod->clazz->super->vtableCount) {
            /*
             * Method does not exist in the superclass.  Could happen if
             * superclass gets updated.
             */
            dvmThrowNoSuchMethodError(baseMethod->name);
            GOTO_exceptionThrown();
        }
        methodToCall = curMethod->clazz->super->vtable[baseMethod->methodIndex];

#if 0
        if (dvmIsAbstractMethod(methodToCall)) {
            dvmThrowAbstractMethodError("abstract method not implemented");
            GOTO_exceptionThrown();
        }
#else
	#if __NIX__
        assert(!dvmIsAbstractMethod(methodToCall) ||
            methodToCall->nativeFunc != NULL);
	#endif
#endif
#if __NIX__
        LOGVV("+++ base=%s.%s super-virtual=%s.%s",
            baseMethod->clazz->descriptor, baseMethod->name,
            methodToCall->clazz->descriptor, methodToCall->name);
#endif            
        assert(methodToCall != NULL);

        GOTO_invokeMethod(methodCallRange, methodToCall, vsrc1, vdst);
    }
GOTO_TARGET_END

GOTO_TARGET(invokeInterface)
    {
        Object* thisPtr;
        ClassObject* thisClass;

        EXPORT_PC();

        if (jumboFormat) {
            ref = FETCH(1) | (u4)FETCH(2) << 16;  /* method ref */
            vsrc1 = FETCH(3);                     /* count */
            vdst = FETCH(4);                      /* first reg */
            ADJUST_PC(2);     /* advance pc partially to make returns easier */
#if __NIX__
            ILOGV("|invoke-interface/jumbo args=%d @0x%08x {regs=v%d-v%d}",
                vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
            thisPtr = (Object*) GET_REGISTER(vdst);
        } else {
            vsrc1 = INST_AA(inst);      /* AA (count) or BA (count + arg 5) */
            ref = FETCH(1);             /* method ref */
            vdst = FETCH(2);            /* 4 regs -or- first reg */

            /*
             * The object against which we are executing a method is always
             * in the first argument.
             */
            if (methodCallRange) {
                assert(vsrc1 > 0);
#if __NIX__
                ILOGV("|invoke-interface-range args=%d @0x%04x {regs=v%d-v%d}",
                    vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
                thisPtr = (Object*) GET_REGISTER(vdst);
            } else {
                assert((vsrc1>>4) > 0);
#if __NIX__
                ILOGV("|invoke-interface args=%d @0x%04x {regs=0x%04x %x}",
                    vsrc1 >> 4, ref, vdst, vsrc1 & 0x0f);
#endif
                thisPtr = (Object*) GET_REGISTER(vdst & 0x0f);
            }
        }

        if (!checkForNull(thisPtr))
            GOTO_exceptionThrown();

        thisClass = thisPtr->clazz;


        /*
         * Given a class and a method index, find the Method* with the
         * actual code we want to execute.
         */
        methodToCall = dvmFindInterfaceMethodInCache(thisClass, ref, curMethod,
                        methodClassDex);
#if defined(WITH_JIT) && defined(MTERP_STUB)
        self->callsiteClass = thisClass;
        self->methodToCall = methodToCall;
#endif
        if (methodToCall == NULL) {
            assert(dvmCheckException(self));
            GOTO_exceptionThrown();
        }

        GOTO_invokeMethod(methodCallRange, methodToCall, vsrc1, vdst);
    }
GOTO_TARGET_END

GOTO_TARGET(invokeDirect)
    {
        u2 thisReg;

        EXPORT_PC();

        if (jumboFormat) {
            ref = FETCH(1) | (u4)FETCH(2) << 16;  /* method ref */
            vsrc1 = FETCH(3);                     /* count */
            vdst = FETCH(4);                      /* first reg */
            ADJUST_PC(2);     /* advance pc partially to make returns easier */
#if __NIX__
            ILOGV("|invoke-direct/jumbo args=%d @0x%08x {regs=v%d-v%d}",
                vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
            thisReg = vdst;
        } else {
            vsrc1 = INST_AA(inst);      /* AA (count) or BA (count + arg 5) */
            ref = FETCH(1);             /* method ref */
            vdst = FETCH(2);            /* 4 regs -or- first reg */

            if (methodCallRange) {
#if __NIX__
                ILOGV("|invoke-direct-range args=%d @0x%04x {regs=v%d-v%d}",
                    vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
                thisReg = vdst;
            } else {
#if __NIX__
                ILOGV("|invoke-direct args=%d @0x%04x {regs=0x%04x %x}",
                    vsrc1 >> 4, ref, vdst, vsrc1 & 0x0f);
#endif
                thisReg = vdst & 0x0f;
            }
        }

        if (!checkForNull((Object*) GET_REGISTER(thisReg)))
            GOTO_exceptionThrown();

        methodToCall = dvmDexGetResolvedMethod(methodClassDex, ref);
        if (methodToCall == NULL) {
            methodToCall = dvmResolveMethod(curMethod->clazz, ref,
                            METHOD_DIRECT);
            if (methodToCall == NULL) {
#if __NIX__
                ILOGV("+ unknown direct method");     // should be impossible
#endif
                GOTO_exceptionThrown();
            }
        }
        GOTO_invokeMethod(methodCallRange, methodToCall, vsrc1, vdst);
    }
GOTO_TARGET_END

GOTO_TARGET(invokeStatic)
    EXPORT_PC();

    if (jumboFormat) {
        ref = FETCH(1) | (u4)FETCH(2) << 16;  /* method ref */
        vsrc1 = FETCH(3);                     /* count */
        vdst = FETCH(4);                      /* first reg */
        ADJUST_PC(2);     /* advance pc partially to make returns easier */
#if __NIX__
        ILOGV("|invoke-static/jumbo args=%d @0x%08x {regs=v%d-v%d}",
            vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
    } else {
        vsrc1 = INST_AA(inst);      /* AA (count) or BA (count + arg 5) */
        ref = FETCH(1);             /* method ref */
        vdst = FETCH(2);            /* 4 regs -or- first reg */
#if __NIX__
        if (methodCallRange)
            ILOGV("|invoke-static-range args=%d @0x%04x {regs=v%d-v%d}",
                vsrc1, ref, vdst, vdst+vsrc1-1);
        else
            ILOGV("|invoke-static args=%d @0x%04x {regs=0x%04x %x}",
                vsrc1 >> 4, ref, vdst, vsrc1 & 0x0f);
#endif
    }

    methodToCall = dvmDexGetResolvedMethod(methodClassDex, ref);
    if (methodToCall == NULL) {
        methodToCall = dvmResolveMethod(curMethod->clazz, ref, METHOD_STATIC);
        if (methodToCall == NULL) {
#if __NIX__
            ILOGV("+ unknown method");
#endif
            GOTO_exceptionThrown();
        }

#if defined(WITH_JIT) && defined(MTERP_STUB)
        /*
         * The JIT needs dvmDexGetResolvedMethod() to return non-null.
         * Include the check if this code is being used as a stub
         * called from the assembly interpreter.
         */
        if ((self->interpBreak.ctl.subMode & kSubModeJitTraceBuild) &&
            (dvmDexGetResolvedMethod(methodClassDex, ref) == NULL)) {
            /* Class initialization is still ongoing */
            dvmJitEndTraceSelect(self,pc);
        }
#endif
    }
    GOTO_invokeMethod(methodCallRange, methodToCall, vsrc1, vdst);
GOTO_TARGET_END

GOTO_TARGET(invokeVirtualQuick)
    {
        Object* thisPtr;

        EXPORT_PC();

        vsrc1 = INST_AA(inst);      /* AA (count) or BA (count + arg 5) */
        ref = FETCH(1);             /* vtable index */
        vdst = FETCH(2);            /* 4 regs -or- first reg */

        /*
         * The object against which we are executing a method is always
         * in the first argument.
         */
        if (methodCallRange) {
            assert(vsrc1 > 0);
#if __NIX__
            ILOGV("|invoke-virtual-quick-range args=%d @0x%04x {regs=v%d-v%d}",
                vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
            thisPtr = (Object*) GET_REGISTER(vdst);
        } else {
            assert((vsrc1>>4) > 0);
#if __NIX__
            ILOGV("|invoke-virtual-quick args=%d @0x%04x {regs=0x%04x %x}",
                vsrc1 >> 4, ref, vdst, vsrc1 & 0x0f);
#endif
            thisPtr = (Object*) GET_REGISTER(vdst & 0x0f);
        }

        if (!checkForNull(thisPtr))
            GOTO_exceptionThrown();


        /*
         * Combine the object we found with the vtable offset in the
         * method.
         */
        assert(ref < (unsigned int) thisPtr->clazz->vtableCount);
        methodToCall = thisPtr->clazz->vtable[ref];
#if defined(WITH_JIT) && defined(MTERP_STUB)
        self->callsiteClass = thisPtr->clazz;
        self->methodToCall = methodToCall;
#endif

#if 0
        if (dvmIsAbstractMethod(methodToCall)) {
            dvmThrowAbstractMethodError("abstract method not implemented");
            GOTO_exceptionThrown();
        }
#else
	#if __NIX__
        assert(!dvmIsAbstractMethod(methodToCall) ||
            methodToCall->nativeFunc != NULL);
	#endif
#endif
#if __NIX__
        LOGVV("+++ virtual[%d]=%s.%s",
            ref, methodToCall->clazz->descriptor, methodToCall->name);
#endif            
        assert(methodToCall != NULL);

        GOTO_invokeMethod(methodCallRange, methodToCall, vsrc1, vdst);
    }
GOTO_TARGET_END

GOTO_TARGET(invokeSuperQuick)
    {
        u2 thisReg;

        EXPORT_PC();

        vsrc1 = INST_AA(inst);      /* AA (count) or BA (count + arg 5) */
        ref = FETCH(1);             /* vtable index */
        vdst = FETCH(2);            /* 4 regs -or- first reg */

        if (methodCallRange) {
#if __NIX__
            ILOGV("|invoke-super-quick-range args=%d @0x%04x {regs=v%d-v%d}",
                vsrc1, ref, vdst, vdst+vsrc1-1);
#endif
            thisReg = vdst;
        } else {
#if __NIX__
            ILOGV("|invoke-super-quick args=%d @0x%04x {regs=0x%04x %x}",
                vsrc1 >> 4, ref, vdst, vsrc1 & 0x0f);
#endif
            thisReg = vdst & 0x0f;
        }
        /* impossible in well-formed code, but we must check nevertheless */
        if (!checkForNull((Object*) GET_REGISTER(thisReg)))
            GOTO_exceptionThrown();

#if 0   /* impossible in optimized + verified code */
        if (ref >= curMethod->clazz->super->vtableCount) {
            dvmThrowNoSuchMethodError(NULL);
            GOTO_exceptionThrown();
        }
#else
        assert(ref < (unsigned int) curMethod->clazz->super->vtableCount);
#endif

        /*
         * Combine the object we found with the vtable offset in the
         * method's class.
         *
         * We're using the current method's class' superclass, not the
         * superclass of "this".  This is because we might be executing
         * in a method inherited from a superclass, and we want to run
         * in the method's class' superclass.
         */
        methodToCall = curMethod->clazz->super->vtable[ref];

#if 0
        if (dvmIsAbstractMethod(methodToCall)) {
            dvmThrowAbstractMethodError("abstract method not implemented");
            GOTO_exceptionThrown();
        }
#else
	#if __NIX__
        assert(!dvmIsAbstractMethod(methodToCall) ||
            methodToCall->nativeFunc != NULL);
	#endif
#endif

#if __NIX__
        LOGVV("+++ super-virtual[%d]=%s.%s",
            ref, methodToCall->clazz->descriptor, methodToCall->name);
#endif            
        assert(methodToCall != NULL);
        GOTO_invokeMethod(methodCallRange, methodToCall, vsrc1, vdst);
    }
GOTO_TARGET_END


    /*
     * General handling for return-void, return, and return-wide.  Put the
     * return value in "retval" before jumping here.
     */
GOTO_TARGET(returnFromMethod)
    {
        StackSaveArea* saveArea;

        /*
         * We must do this BEFORE we pop the previous stack frame off, so
         * that the GC can see the return value (if any) in the local vars.
         *
         * Since this is now an interpreter switch point, we must do it before
         * we do anything at all.
         */
        PERIODIC_CHECKS(0);
#if __NIX__
        ILOGV("> retval=0x%llx (leaving %s.%s %s)",
            retval.j, curMethod->clazz->descriptor, curMethod->name,
            curMethod->shorty);
#endif
        //DUMP_REGS(curMethod, fp);

        saveArea = SAVEAREA_FROM_FP(fp);

#ifdef EASY_GDB
        debugSaveArea = saveArea;
#endif

        /* back up to previous frame and see if we hit a break */
        fp = (u4*)saveArea->prevFrame;
        assert(fp != NULL);

        /* Handle any special subMode requirements */
        if (self->interpBreak.ctl.subMode != 0) {
            PC_FP_TO_SELF();
            dvmReportReturn(self);
        }

        if (dvmIsBreakFrame(fp)) {
            /* bail without popping the method frame from stack */
#if __NIX__            
            LOGVV("+++ returned into break frame");
#endif            

			/*nix add here,but don't know wether it's right!*/
			//magic number stands for exc over!
			retval.j = 0xacacacac ULL;
            GOTO_bail();
        }

        /* update thread FP, and reset local variables */
        self->interpSave.curFrame = fp;
        curMethod = SAVEAREA_FROM_FP(fp)->method;
        self->interpSave.method = curMethod;
        //methodClass = curMethod->clazz;
        methodClassDex = curMethod->clazz->pDvmDex;
        pc = saveArea->savedPc;
#if __NIX__     
        ILOGD("> (return to %s.%s %s)", curMethod->clazz->descriptor,
            curMethod->name, curMethod->shorty);
#endif
        /* use FINISH on the caller's invoke instruction */
        //u2 invokeInstr = INST_INST(FETCH(0));
        if (true /*invokeInstr >= OP_INVOKE_VIRTUAL &&
            invokeInstr <= OP_INVOKE_INTERFACE*/)
        {
            FINISH(3);
        } else {
            //LOGE("Unknown invoke instr %02x at %d",
            //    invokeInstr, (int) (pc - curMethod->insns));
            assert(false);
        }
    }
GOTO_TARGET_END


    /*
     * Jump here when the code throws an exception.
     *
     * By the time we get here, the Throwable has been created and the stack
     * trace has been saved off.
     */
GOTO_TARGET(exceptionThrown)
    {
        Object* exception;
        int catchRelPc;

        PERIODIC_CHECKS(0);

        /*
         * We save off the exception and clear the exception status.  While
         * processing the exception we might need to load some Throwable
         * classes, and we don't want class loader exceptions to get
         * confused with this one.
         */

//MUST FIX HERE why not find exception
#if 0
		assert(dvmCheckException(self));
#else
		self->exception = gDvm.internalErrorObj;
#endif
        exception = dvmGetException(self);
        dvmAddTrackedAlloc(exception, self);
        dvmClearException(self);

        LOGV("Handling exception %s at %s:%d",
            exception->clazz->descriptor, curMethod->name,
            dvmLineNumFromPC(curMethod, pc - curMethod->insns));

        /*
         * Report the exception throw to any "subMode" watchers.
         *
         * TODO: if the exception was thrown by interpreted code, control
         * fell through native, and then back to us, we will report the
         * exception at the point of the throw and again here.  We can avoid
         * this by not reporting exceptions when we jump here directly from
         * the native call code above, but then we won't report exceptions
         * that were thrown *from* the JNI code (as opposed to *through* it).
         *
         * The correct solution is probably to ignore from-native exceptions
         * here, and have the JNI exception code do the reporting to the
         * debugger.
         */
        if (self->interpBreak.ctl.subMode != 0) {
            PC_FP_TO_SELF();
            dvmReportExceptionThrow(self, exception);
        }

        /*
         * We need to unroll to the catch block or the nearest "break"
         * frame.
         *
         * A break frame could indicate that we have reached an intermediate
         * native call, or have gone off the top of the stack and the thread
         * needs to exit.  Either way, we return from here, leaving the
         * exception raised.
         *
         * If we do find a catch block, we want to transfer execution to
         * that point.
         *
         * Note this can cause an exception while resolving classes in
         * the "catch" blocks.
         */
        catchRelPc = dvmFindCatchBlock(self, pc - curMethod->insns,
                    exception, false, (void**)(void*)&fp);

        /*
         * Restore the stack bounds after an overflow.  This isn't going to
         * be correct in all circumstances, e.g. if JNI code devours the
         * exception this won't happen until some other exception gets
         * thrown.  If the code keeps pushing the stack bounds we'll end
         * up aborting the VM.
         *
         * Note we want to do this *after* the call to dvmFindCatchBlock,
         * because that may need extra stack space to resolve exception
         * classes (e.g. through a class loader).
         *
         * It's possible for the stack overflow handling to cause an
         * exception (specifically, class resolution in a "catch" block
         * during the call above), so we could see the thread's overflow
         * flag raised but actually be running in a "nested" interpreter
         * frame.  We don't allow doubled-up StackOverflowErrors, so
         * we can check for this by just looking at the exception type
         * in the cleanup function.  Also, we won't unroll past the SOE
         * point because the more-recent exception will hit a break frame
         * as it unrolls to here.
         */
        if (self->stackOverflowed)
            dvmCleanupStackOverflow(self, exception);

        if (catchRelPc < 0) {
            /* falling through to JNI code or off the bottom of the stack */
#if DVM_SHOW_EXCEPTION >= 2
            LOGD("Exception %s from %s:%d not caught locally",
                exception->clazz->descriptor, dvmGetMethodSourceFile(curMethod),
                dvmLineNumFromPC(curMethod, pc - curMethod->insns));
#endif
            dvmSetException(self, exception);
            dvmReleaseTrackedAlloc(exception, self);
            GOTO_bail();
        }

#if DVM_SHOW_EXCEPTION >= 3
        {
            const Method* catchMethod = SAVEAREA_FROM_FP(fp)->method;
            LOGD("Exception %s thrown from %s:%d to %s:%d",
                exception->clazz->descriptor, dvmGetMethodSourceFile(curMethod),
                dvmLineNumFromPC(curMethod, pc - curMethod->insns),
                dvmGetMethodSourceFile(catchMethod),
                dvmLineNumFromPC(catchMethod, catchRelPc));
        }
#endif

        /*
         * Adjust local variables to match self->interpSave.curFrame and the
         * updated PC.
         */
        //fp = (u4*) self->interpSave.curFrame;
        curMethod = SAVEAREA_FROM_FP(fp)->method;
        self->interpSave.method = curMethod;
        //methodClass = curMethod->clazz;
        methodClassDex = curMethod->clazz->pDvmDex;
        pc = curMethod->insns + catchRelPc;
#if __NIX__
        ILOGV("> pc <-- %s.%s %s", curMethod->clazz->descriptor,
            curMethod->name, curMethod->shorty);
#endif
        DUMP_REGS(curMethod, fp, false);            // show all regs

        /*
         * Restore the exception if the handler wants it.
         *
         * The Dalvik spec mandates that, if an exception handler wants to
         * do something with the exception, the first instruction executed
         * must be "move-exception".  We can pass the exception along
         * through the thread struct, and let the move-exception instruction
         * clear it for us.
         *
         * If the handler doesn't call move-exception, we don't want to
         * finish here with an exception still pending.
         */
        if (INST_INST(FETCH(0)) == OP_MOVE_EXCEPTION)
            dvmSetException(self, exception);

        dvmReleaseTrackedAlloc(exception, self);
        FINISH(0);
    }
GOTO_TARGET_END



    /*
     * General handling for invoke-{virtual,super,direct,static,interface},
     * including "quick" variants.
     *
     * Set "methodToCall" to the Method we're calling, and "methodCallRange"
     * depending on whether this is a "/range" instruction.
     *
     * For a range call:
     *  "vsrc1" holds the argument count (8 bits)
     *  "vdst" holds the first argument in the range
     * For a non-range call:
     *  "vsrc1" holds the argument count (4 bits) and the 5th argument index
     *  "vdst" holds four 4-bit register indices
     *
     * The caller must EXPORT_PC before jumping here, because any method
     * call can throw a stack overflow exception.
     */
GOTO_TARGET(invokeMethod)
    {
        //printf("range=%d call=%p count=%d regs=0x%04x\n",
        //    methodCallRange, methodToCall, count, regs);
        //printf(" --> %s.%s %s\n", methodToCall->clazz->descriptor,
        //    methodToCall->name, methodToCall->shorty);

        u4* outs;
        int i;

        /*
         * Copy args.  This may corrupt vsrc1/vdst.
         */
        if (methodCallRange) {
            // could use memcpy or a "Duff's device"; most functions have
            // so few args it won't matter much
            assert(vsrc1 <= curMethod->outsSize);
            assert(vsrc1 == methodToCall->insSize);
            outs = OUTS_FROM_FP(fp, vsrc1);
            for (i = 0; i < vsrc1; i++)
                outs[i] = GET_REGISTER(vdst+i);
        } else {
            u4 count = vsrc1 >> 4;

            assert(count <= curMethod->outsSize);
            assert(count == methodToCall->insSize);
            assert(count <= 5);

            outs = OUTS_FROM_FP(fp, count);
#if 0
            if (count == 5) {
                outs[4] = GET_REGISTER(vsrc1 & 0x0f);
                count--;
            }
            for (i = 0; i < (int) count; i++) {
                outs[i] = GET_REGISTER(vdst & 0x0f);
                vdst >>= 4;
            }
#else
            // This version executes fewer instructions but is larger
            // overall.  Seems to be a teensy bit faster.
            assert((vdst >> 16) == 0);  // 16 bits -or- high 16 bits clear
            switch (count) {
            case 5:
                outs[4] = GET_REGISTER(vsrc1 & 0x0f);
            case 4:
                outs[3] = GET_REGISTER(vdst >> 12);
            case 3:
                outs[2] = GET_REGISTER((vdst & 0x0f00) >> 8);
            case 2:
                outs[1] = GET_REGISTER((vdst & 0x00f0) >> 4);
            case 1:
                outs[0] = GET_REGISTER(vdst & 0x0f);
            default:
                ;
            }
#endif
        }
    }

    /*
     * (This was originally a "goto" target; I've kept it separate from the
     * stuff above in case we want to refactor things again.)
     *
     * At this point, we have the arguments stored in the "outs" area of
     * the current method's stack frame, and the method to call in
     * "methodToCall".  Push a new stack frame.
     */
    {
        StackSaveArea* newSaveArea;
        u4* newFp;
#if __NIX__
        ILOGV("> %s%s.%s %s",
            dvmIsNativeMethod(methodToCall) ? "(NATIVE) " : "",
            methodToCall->clazz->descriptor, methodToCall->name,
            methodToCall->shorty);
#endif
        newFp = (u4*) SAVEAREA_FROM_FP(fp) - methodToCall->registersSize;
        newSaveArea = SAVEAREA_FROM_FP(newFp);

        /* verify that we have enough space */
        if (true) {
            u1* bottom;
            bottom = (u1*) newSaveArea - methodToCall->outsSize * sizeof(u4);
            if (bottom < self->interpStackEnd) {
                /* stack overflow */
                LOGV("Stack overflow on method call (start=%p end=%p newBot=%p(%d) size=%d '%s')",
                    self->interpStackStart, self->interpStackEnd, bottom,
                    (u1*) fp - bottom, self->interpStackSize,
                    methodToCall->name);
                dvmHandleStackOverflow(self, methodToCall);
                assert(dvmCheckException(self));
                GOTO_exceptionThrown();
            }
            //LOGD("+++ fp=%p newFp=%p newSave=%p bottom=%p",
            //    fp, newFp, newSaveArea, bottom);
        }

#ifdef LOG_INSTR
        if (methodToCall->registersSize > methodToCall->insSize) {
            /*
             * This makes valgrind quiet when we print registers that
             * haven't been initialized.  Turn it off when the debug
             * messages are disabled -- we want valgrind to report any
             * used-before-initialized issues.
             */
            memset(newFp, 0xcc,
                (methodToCall->registersSize - methodToCall->insSize) * 4);
        }
#endif

#ifdef EASY_GDB
        newSaveArea->prevSave = SAVEAREA_FROM_FP(fp);
#endif
        newSaveArea->prevFrame = fp;
        newSaveArea->savedPc = pc;
#if defined(WITH_JIT) && defined(MTERP_STUB)
        newSaveArea->returnAddr = 0;
#endif
        newSaveArea->method = methodToCall;

        if (self->interpBreak.ctl.subMode != 0) {
            /*
             * We mark ENTER here for both native and non-native
             * calls.  For native calls, we'll mark EXIT on return.
             * For non-native calls, EXIT is marked in the RETURN op.
             */
            PC_TO_SELF();
            dvmReportInvoke(self, methodToCall);
        }

        if (!dvmIsNativeMethod(methodToCall)) {
            /*
             * "Call" interpreted code.  Reposition the PC, update the
             * frame pointer and other local state, and continue.
             */
            curMethod = methodToCall;
            self->interpSave.method = curMethod;
            methodClassDex = curMethod->clazz->pDvmDex;
            pc = methodToCall->insns;
            self->interpSave.curFrame = fp = newFp;
#ifdef EASY_GDB
            debugSaveArea = SAVEAREA_FROM_FP(newFp);
#endif
            self->debugIsMethodEntry = true;        // profiling, debugging
#if __NIX__
            ILOGD("> pc <-- %s.%s %s", curMethod->clazz->descriptor,
                curMethod->name, curMethod->shorty);
#endif
            DUMP_REGS(curMethod, fp, true);         // show input args
            FINISH(0);                              // jump to method start
        } else {
			KniFunc nativefunc = NULL;
			Object * thisClz = NULL;   //if static: class ptr;if non static :instance this ptr
            /* set this up for JNI locals, even if not a JNI native */
#if __MAY_ERROR__ == 2
            newSaveArea->xtra.localRefCookie = self->jniLocalRefTable.segmentState.all;
#endif
            self->interpSave.curFrame = newFp;

            DUMP_REGS(methodToCall, newFp, true);   // show input args

            if (self->interpBreak.ctl.subMode != 0) {
#if __MAY_ERROR__            
                dvmReportPreNativeInvoke(methodToCall, self, fp);
#endif                
            }
#if __NIX__
            ILOGD("> native <-- %s.%s %s", methodToCall->clazz->descriptor,
                  methodToCall->name, methodToCall->shorty);
#endif
            /*
             * Jump through native call bridge.  Because we leave no
             * space for locals on native calls, "newFp" points directly
             * to the method arguments.
             */
			/*
			if(dvmIsStaticMethod(methodToCall))
			{
				//*nix: maybe error!
				thisClz = (Object*) methodToCall->clazz;
			}
			else
			{
				thisClz = ((Object*) newFp)->clazz ;  //the first param of Fp is "this ptr"
			}
			nativefunc = Kni_findFuncPtr(methodToCall);
			DVM_ASSERT(nativefunc != NULL);
			nativefunc(thisClz);
			*/
			dvmInterpretMakeNativeCall(newFp, &retval, methodToCall, self);
			// call back -- nix
            //(*methodToCall->nativeFunc)(newFp, &retval, methodToCall, self);

            if (self->interpBreak.ctl.subMode != 0) {
#if __MAY_ERROR__            
                dvmReportPostNativeInvoke(methodToCall, self, fp);
#endif
            }

            /* pop frame off */
#if __MAY_ERROR__            
            dvmPopJniLocals(self, newSaveArea);
#endif            
            self->interpSave.curFrame = fp;

            /*
             * If the native code threw an exception, or interpreted code
             * invoked by the native call threw one and nobody has cleared
             * it, jump to our local exception handling.
             */
            if (dvmCheckException(self)) {
                LOGV("Exception thrown by/below native code");
                GOTO_exceptionThrown();
            }
#if __NIX__
            ILOGD("> retval=0x%llx (leaving native)", retval.j);
            ILOGD("> (return from native %s.%s to %s.%s %s)",
                methodToCall->clazz->descriptor, methodToCall->name,
                curMethod->clazz->descriptor, curMethod->name,
                curMethod->shorty);
#endif
            //u2 invokeInstr = INST_INST(FETCH(0));
            if (true /*invokeInstr >= OP_INVOKE_VIRTUAL &&
                invokeInstr <= OP_INVOKE_INTERFACE*/)
            {
                FINISH(3);
            } else {
                //LOGE("Unknown invoke instr %02x at %d",
                //    invokeInstr, (int) (pc - curMethod->insns));
                assert(false);
            }
        }
    }
    assert(false);      // should not get here
GOTO_TARGET_END

/* File: portable/enddefs.cpp */
/*--- end of opcodes ---*/

#if defined(ARCH_ARM_SPD)
OP_GOTO_TABLE:
    switch(g_goto_opcode)
    {
        case OP_NOP                            : goto op_OP_NOP                            ;
        case OP_MOVE                           : goto op_OP_MOVE                           ;
        case OP_MOVE_FROM16                    : goto op_OP_MOVE_FROM16                    ;
        case OP_MOVE_16                        : goto op_OP_MOVE_16                        ;
        case OP_MOVE_WIDE                      : goto op_OP_MOVE_WIDE                      ;
        case OP_MOVE_WIDE_FROM16               : goto op_OP_MOVE_WIDE_FROM16               ;
        case OP_MOVE_WIDE_16                   : goto op_OP_MOVE_WIDE_16                   ;
        case OP_MOVE_OBJECT                    : goto op_OP_MOVE_OBJECT                    ;
        case OP_MOVE_OBJECT_FROM16             : goto op_OP_MOVE_OBJECT_FROM16             ;
        case OP_MOVE_OBJECT_16                 : goto op_OP_MOVE_OBJECT_16                 ;
        case OP_MOVE_RESULT                    : goto op_OP_MOVE_RESULT                    ;
        case OP_MOVE_RESULT_WIDE               : goto op_OP_MOVE_RESULT_WIDE               ;
        case OP_MOVE_RESULT_OBJECT             : goto op_OP_MOVE_RESULT_OBJECT             ;
        case OP_MOVE_EXCEPTION                 : goto op_OP_MOVE_EXCEPTION                 ;
        case OP_RETURN_VOID                    : goto op_OP_RETURN_VOID                    ;
        case OP_RETURN                         : goto op_OP_RETURN                         ;
        case OP_RETURN_WIDE                    : goto op_OP_RETURN_WIDE                    ;
        case OP_RETURN_OBJECT                  : goto op_OP_RETURN_OBJECT                  ;
        case OP_CONST_4                        : goto op_OP_CONST_4                        ;
        case OP_CONST_16                       : goto op_OP_CONST_16                       ;
        case OP_CONST                          : goto op_OP_CONST                          ;
        case OP_CONST_HIGH16                   : goto op_OP_CONST_HIGH16                   ;
        case OP_CONST_WIDE_16                  : goto op_OP_CONST_WIDE_16                  ;
        case OP_CONST_WIDE_32                  : goto op_OP_CONST_WIDE_32                  ;
        case OP_CONST_WIDE                     : goto op_OP_CONST_WIDE                     ;
        case OP_CONST_WIDE_HIGH16              : goto op_OP_CONST_WIDE_HIGH16              ;
        case OP_CONST_STRING                   : goto op_OP_CONST_STRING                   ;
        case OP_CONST_STRING_JUMBO             : goto op_OP_CONST_STRING_JUMBO             ;
        case OP_CONST_CLASS                    : goto op_OP_CONST_CLASS                    ;
        case OP_MONITOR_ENTER                  : goto op_OP_MONITOR_ENTER                  ;
        case OP_MONITOR_EXIT                   : goto op_OP_MONITOR_EXIT                   ;
        case OP_CHECK_CAST                     : goto op_OP_CHECK_CAST                     ;
        case OP_INSTANCE_OF                    : goto op_OP_INSTANCE_OF                    ;
        case OP_ARRAY_LENGTH                   : goto op_OP_ARRAY_LENGTH                   ;
        case OP_NEW_INSTANCE                   : goto op_OP_NEW_INSTANCE                   ;
        case OP_NEW_ARRAY                      : goto op_OP_NEW_ARRAY                      ;
        case OP_FILLED_NEW_ARRAY               : goto op_OP_FILLED_NEW_ARRAY               ;
        case OP_FILLED_NEW_ARRAY_RANGE         : goto op_OP_FILLED_NEW_ARRAY_RANGE         ;
        case OP_FILL_ARRAY_DATA                : goto op_OP_FILL_ARRAY_DATA                ;
        case OP_THROW                          : goto op_OP_THROW                          ;
        case OP_GOTO                           : goto op_OP_GOTO                           ;
        case OP_GOTO_16                        : goto op_OP_GOTO_16                        ;
        case OP_GOTO_32                        : goto op_OP_GOTO_32                        ;
        case OP_PACKED_SWITCH                  : goto op_OP_PACKED_SWITCH                  ;
        case OP_SPARSE_SWITCH                  : goto op_OP_SPARSE_SWITCH                  ;
        case OP_CMPL_FLOAT                     : goto op_OP_CMPL_FLOAT                     ;
        case OP_CMPG_FLOAT                     : goto op_OP_CMPG_FLOAT                     ;
        case OP_CMPL_DOUBLE                    : goto op_OP_CMPL_DOUBLE                    ;
        case OP_CMPG_DOUBLE                    : goto op_OP_CMPG_DOUBLE                    ;
        case OP_CMP_LONG                       : goto op_OP_CMP_LONG                       ;
        case OP_IF_EQ                          : goto op_OP_IF_EQ                          ;
        case OP_IF_NE                          : goto op_OP_IF_NE                          ;
        case OP_IF_LT                          : goto op_OP_IF_LT                          ;
        case OP_IF_GE                          : goto op_OP_IF_GE                          ;
        case OP_IF_GT                          : goto op_OP_IF_GT                          ;
        case OP_IF_LE                          : goto op_OP_IF_LE                          ;
        case OP_IF_EQZ                         : goto op_OP_IF_EQZ                         ;
        case OP_IF_NEZ                         : goto op_OP_IF_NEZ                         ;
        case OP_IF_LTZ                         : goto op_OP_IF_LTZ                         ;
        case OP_IF_GEZ                         : goto op_OP_IF_GEZ                         ;
        case OP_IF_GTZ                         : goto op_OP_IF_GTZ                         ;
        case OP_IF_LEZ                         : goto op_OP_IF_LEZ                         ;
        case OP_UNUSED_3E                      : goto op_OP_UNUSED_3E                      ;
        case OP_UNUSED_3F                      : goto op_OP_UNUSED_3F                      ;
        case OP_UNUSED_40                      : goto op_OP_UNUSED_40                      ;
        case OP_UNUSED_41                      : goto op_OP_UNUSED_41                      ;
        case OP_UNUSED_42                      : goto op_OP_UNUSED_42                      ;
        case OP_UNUSED_43                      : goto op_OP_UNUSED_43                      ;
        case OP_AGET                           : goto op_OP_AGET                           ;
        case OP_AGET_WIDE                      : goto op_OP_AGET_WIDE                      ;
        case OP_AGET_OBJECT                    : goto op_OP_AGET_OBJECT                    ;
        case OP_AGET_BOOLEAN                   : goto op_OP_AGET_BOOLEAN                   ;
        case OP_AGET_BYTE                      : goto op_OP_AGET_BYTE                      ;
        case OP_AGET_CHAR                      : goto op_OP_AGET_CHAR                      ;
        case OP_AGET_SHORT                     : goto op_OP_AGET_SHORT                     ;
        case OP_APUT                           : goto op_OP_APUT                           ;
        case OP_APUT_WIDE                      : goto op_OP_APUT_WIDE                      ;
        case OP_APUT_OBJECT                    : goto op_OP_APUT_OBJECT                    ;
        case OP_APUT_BOOLEAN                   : goto op_OP_APUT_BOOLEAN                   ;
        case OP_APUT_BYTE                      : goto op_OP_APUT_BYTE                      ;
        case OP_APUT_CHAR                      : goto op_OP_APUT_CHAR                      ;
        case OP_APUT_SHORT                     : goto op_OP_APUT_SHORT                     ;
        case OP_IGET                           : goto op_OP_IGET                           ;
        case OP_IGET_WIDE                      : goto op_OP_IGET_WIDE                      ;
        case OP_IGET_OBJECT                    : goto op_OP_IGET_OBJECT                    ;
        case OP_IGET_BOOLEAN                   : goto op_OP_IGET_BOOLEAN                   ;
        case OP_IGET_BYTE                      : goto op_OP_IGET_BYTE                      ;
        case OP_IGET_CHAR                      : goto op_OP_IGET_CHAR                      ;
        case OP_IGET_SHORT                     : goto op_OP_IGET_SHORT                     ;
        case OP_IPUT                           : goto op_OP_IPUT                           ;
        case OP_IPUT_WIDE                      : goto op_OP_IPUT_WIDE                      ;
        case OP_IPUT_OBJECT                    : goto op_OP_IPUT_OBJECT                    ;
        case OP_IPUT_BOOLEAN                   : goto op_OP_IPUT_BOOLEAN                   ;
        case OP_IPUT_BYTE                      : goto op_OP_IPUT_BYTE                      ;
        case OP_IPUT_CHAR                      : goto op_OP_IPUT_CHAR                      ;
        case OP_IPUT_SHORT                     : goto op_OP_IPUT_SHORT                     ;
        case OP_SGET                           : goto op_OP_SGET                           ;
        case OP_SGET_WIDE                      : goto op_OP_SGET_WIDE                      ;
        case OP_SGET_OBJECT                    : goto op_OP_SGET_OBJECT                    ;
        case OP_SGET_BOOLEAN                   : goto op_OP_SGET_BOOLEAN                   ;
        case OP_SGET_BYTE                      : goto op_OP_SGET_BYTE                      ;
        case OP_SGET_CHAR                      : goto op_OP_SGET_CHAR                      ;
        case OP_SGET_SHORT                     : goto op_OP_SGET_SHORT                     ;
        case OP_SPUT                           : goto op_OP_SPUT                           ;
        case OP_SPUT_WIDE                      : goto op_OP_SPUT_WIDE                      ;
        case OP_SPUT_OBJECT                    : goto op_OP_SPUT_OBJECT                    ;
        case OP_SPUT_BOOLEAN                   : goto op_OP_SPUT_BOOLEAN                   ;
        case OP_SPUT_BYTE                      : goto op_OP_SPUT_BYTE                      ;
        case OP_SPUT_CHAR                      : goto op_OP_SPUT_CHAR                      ;
        case OP_SPUT_SHORT                     : goto op_OP_SPUT_SHORT                     ;
        case OP_INVOKE_VIRTUAL                 : goto op_OP_INVOKE_VIRTUAL                 ;
        case OP_INVOKE_SUPER                   : goto op_OP_INVOKE_SUPER                   ;
        case OP_INVOKE_DIRECT                  : goto op_OP_INVOKE_DIRECT                  ;
        case OP_INVOKE_STATIC                  : goto op_OP_INVOKE_STATIC                  ;
        case OP_INVOKE_INTERFACE               : goto op_OP_INVOKE_INTERFACE               ;
        case OP_UNUSED_73                      : goto op_OP_UNUSED_73                      ;
        case OP_INVOKE_VIRTUAL_RANGE           : goto op_OP_INVOKE_VIRTUAL_RANGE           ;
        case OP_INVOKE_SUPER_RANGE             : goto op_OP_INVOKE_SUPER_RANGE             ;
        case OP_INVOKE_DIRECT_RANGE            : goto op_OP_INVOKE_DIRECT_RANGE            ;
        case OP_INVOKE_STATIC_RANGE            : goto op_OP_INVOKE_STATIC_RANGE            ;
        case OP_INVOKE_INTERFACE_RANGE         : goto op_OP_INVOKE_INTERFACE_RANGE         ;
        case OP_UNUSED_79                      : goto op_OP_UNUSED_79                      ;
        case OP_UNUSED_7A                      : goto op_OP_UNUSED_7A                      ;
        case OP_NEG_INT                        : goto op_OP_NEG_INT                        ;
        case OP_NOT_INT                        : goto op_OP_NOT_INT                        ;
        case OP_NEG_LONG                       : goto op_OP_NEG_LONG                       ;
        case OP_NOT_LONG                       : goto op_OP_NOT_LONG                       ;
        case OP_NEG_FLOAT                      : goto op_OP_NEG_FLOAT                      ;
        case OP_NEG_DOUBLE                     : goto op_OP_NEG_DOUBLE                     ;
        case OP_INT_TO_LONG                    : goto op_OP_INT_TO_LONG                    ;
        case OP_INT_TO_FLOAT                   : goto op_OP_INT_TO_FLOAT                   ;
        case OP_INT_TO_DOUBLE                  : goto op_OP_INT_TO_DOUBLE                  ;
        case OP_LONG_TO_INT                    : goto op_OP_LONG_TO_INT                    ;
        case OP_LONG_TO_FLOAT                  : goto op_OP_LONG_TO_FLOAT                  ;
        case OP_LONG_TO_DOUBLE                 : goto op_OP_LONG_TO_DOUBLE                 ;
        case OP_FLOAT_TO_INT                   : goto op_OP_FLOAT_TO_INT                   ;
        case OP_FLOAT_TO_LONG                  : goto op_OP_FLOAT_TO_LONG                  ;
        case OP_FLOAT_TO_DOUBLE                : goto op_OP_FLOAT_TO_DOUBLE                ;
        case OP_DOUBLE_TO_INT                  : goto op_OP_DOUBLE_TO_INT                  ;
        case OP_DOUBLE_TO_LONG                 : goto op_OP_DOUBLE_TO_LONG                 ;
        case OP_DOUBLE_TO_FLOAT                : goto op_OP_DOUBLE_TO_FLOAT                ;
        case OP_INT_TO_BYTE                    : goto op_OP_INT_TO_BYTE                    ;
        case OP_INT_TO_CHAR                    : goto op_OP_INT_TO_CHAR                    ;
        case OP_INT_TO_SHORT                   : goto op_OP_INT_TO_SHORT                   ;
        case OP_ADD_INT                        : goto op_OP_ADD_INT                        ;
        case OP_SUB_INT                        : goto op_OP_SUB_INT                        ;
        case OP_MUL_INT                        : goto op_OP_MUL_INT                        ;
        case OP_DIV_INT                        : goto op_OP_DIV_INT                        ;
        case OP_REM_INT                        : goto op_OP_REM_INT                        ;
        case OP_AND_INT                        : goto op_OP_AND_INT                        ;
        case OP_OR_INT                         : goto op_OP_OR_INT                         ;
        case OP_XOR_INT                        : goto op_OP_XOR_INT                        ;
        case OP_SHL_INT                        : goto op_OP_SHL_INT                        ;
        case OP_SHR_INT                        : goto op_OP_SHR_INT                        ;
        case OP_USHR_INT                       : goto op_OP_USHR_INT                       ;
        case OP_ADD_LONG                       : goto op_OP_ADD_LONG                       ;
        case OP_SUB_LONG                       : goto op_OP_SUB_LONG                       ;
        case OP_MUL_LONG                       : goto op_OP_MUL_LONG                       ;
        case OP_DIV_LONG                       : goto op_OP_DIV_LONG                       ;
        case OP_REM_LONG                       : goto op_OP_REM_LONG                       ;
        case OP_AND_LONG                       : goto op_OP_AND_LONG                       ;
        case OP_OR_LONG                        : goto op_OP_OR_LONG                        ;
        case OP_XOR_LONG                       : goto op_OP_XOR_LONG                       ;
        case OP_SHL_LONG                       : goto op_OP_SHL_LONG                       ;
        case OP_SHR_LONG                       : goto op_OP_SHR_LONG                       ;
        case OP_USHR_LONG                      : goto op_OP_USHR_LONG                      ;
        case OP_ADD_FLOAT                      : goto op_OP_ADD_FLOAT                      ;
        case OP_SUB_FLOAT                      : goto op_OP_SUB_FLOAT                      ;
        case OP_MUL_FLOAT                      : goto op_OP_MUL_FLOAT                      ;
        case OP_DIV_FLOAT                      : goto op_OP_DIV_FLOAT                      ;
        case OP_REM_FLOAT                      : goto op_OP_REM_FLOAT                      ;
        case OP_ADD_DOUBLE                     : goto op_OP_ADD_DOUBLE                     ;
        case OP_SUB_DOUBLE                     : goto op_OP_SUB_DOUBLE                     ;
        case OP_MUL_DOUBLE                     : goto op_OP_MUL_DOUBLE                     ;
        case OP_DIV_DOUBLE                     : goto op_OP_DIV_DOUBLE                     ;
        case OP_REM_DOUBLE                     : goto op_OP_REM_DOUBLE                     ;
        case OP_ADD_INT_2ADDR                  : goto op_OP_ADD_INT_2ADDR                  ;
        case OP_SUB_INT_2ADDR                  : goto op_OP_SUB_INT_2ADDR                  ;
        case OP_MUL_INT_2ADDR                  : goto op_OP_MUL_INT_2ADDR                  ;
        case OP_DIV_INT_2ADDR                  : goto op_OP_DIV_INT_2ADDR                  ;
        case OP_REM_INT_2ADDR                  : goto op_OP_REM_INT_2ADDR                  ;
        case OP_AND_INT_2ADDR                  : goto op_OP_AND_INT_2ADDR                  ;
        case OP_OR_INT_2ADDR                   : goto op_OP_OR_INT_2ADDR                   ;
        case OP_XOR_INT_2ADDR                  : goto op_OP_XOR_INT_2ADDR                  ;
        case OP_SHL_INT_2ADDR                  : goto op_OP_SHL_INT_2ADDR                  ;
        case OP_SHR_INT_2ADDR                  : goto op_OP_SHR_INT_2ADDR                  ;
        case OP_USHR_INT_2ADDR                 : goto op_OP_USHR_INT_2ADDR                 ;
        case OP_ADD_LONG_2ADDR                 : goto op_OP_ADD_LONG_2ADDR                 ;
        case OP_SUB_LONG_2ADDR                 : goto op_OP_SUB_LONG_2ADDR                 ;
        case OP_MUL_LONG_2ADDR                 : goto op_OP_MUL_LONG_2ADDR                 ;
        case OP_DIV_LONG_2ADDR                 : goto op_OP_DIV_LONG_2ADDR                 ;
        case OP_REM_LONG_2ADDR                 : goto op_OP_REM_LONG_2ADDR                 ;
        case OP_AND_LONG_2ADDR                 : goto op_OP_AND_LONG_2ADDR                 ;
        case OP_OR_LONG_2ADDR                  : goto op_OP_OR_LONG_2ADDR                  ;
        case OP_XOR_LONG_2ADDR                 : goto op_OP_XOR_LONG_2ADDR                 ;
        case OP_SHL_LONG_2ADDR                 : goto op_OP_SHL_LONG_2ADDR                 ;
        case OP_SHR_LONG_2ADDR                 : goto op_OP_SHR_LONG_2ADDR                 ;
        case OP_USHR_LONG_2ADDR                : goto op_OP_USHR_LONG_2ADDR                ;
        case OP_ADD_FLOAT_2ADDR                : goto op_OP_ADD_FLOAT_2ADDR                ;
        case OP_SUB_FLOAT_2ADDR                : goto op_OP_SUB_FLOAT_2ADDR                ;
        case OP_MUL_FLOAT_2ADDR                : goto op_OP_MUL_FLOAT_2ADDR                ;
        case OP_DIV_FLOAT_2ADDR                : goto op_OP_DIV_FLOAT_2ADDR                ;
        case OP_REM_FLOAT_2ADDR                : goto op_OP_REM_FLOAT_2ADDR                ;
        case OP_ADD_DOUBLE_2ADDR               : goto op_OP_ADD_DOUBLE_2ADDR               ;
        case OP_SUB_DOUBLE_2ADDR               : goto op_OP_SUB_DOUBLE_2ADDR               ;
        case OP_MUL_DOUBLE_2ADDR               : goto op_OP_MUL_DOUBLE_2ADDR               ;
        case OP_DIV_DOUBLE_2ADDR               : goto op_OP_DIV_DOUBLE_2ADDR               ;
        case OP_REM_DOUBLE_2ADDR               : goto op_OP_REM_DOUBLE_2ADDR               ;
        case OP_ADD_INT_LIT16                  : goto op_OP_ADD_INT_LIT16                  ;
        case OP_RSUB_INT                       : goto op_OP_RSUB_INT                       ;
        case OP_MUL_INT_LIT16                  : goto op_OP_MUL_INT_LIT16                  ;
        case OP_DIV_INT_LIT16                  : goto op_OP_DIV_INT_LIT16                  ;
        case OP_REM_INT_LIT16                  : goto op_OP_REM_INT_LIT16                  ;
        case OP_AND_INT_LIT16                  : goto op_OP_AND_INT_LIT16                  ;
        case OP_OR_INT_LIT16                   : goto op_OP_OR_INT_LIT16                   ;
        case OP_XOR_INT_LIT16                  : goto op_OP_XOR_INT_LIT16                  ;
        case OP_ADD_INT_LIT8                   : goto op_OP_ADD_INT_LIT8                   ;
        case OP_RSUB_INT_LIT8                  : goto op_OP_RSUB_INT_LIT8                  ;
        case OP_MUL_INT_LIT8                   : goto op_OP_MUL_INT_LIT8                   ;
        case OP_DIV_INT_LIT8                   : goto op_OP_DIV_INT_LIT8                   ;
        case OP_REM_INT_LIT8                   : goto op_OP_REM_INT_LIT8                   ;
        case OP_AND_INT_LIT8                   : goto op_OP_AND_INT_LIT8                   ;
        case OP_OR_INT_LIT8                    : goto op_OP_OR_INT_LIT8                    ;
        case OP_XOR_INT_LIT8                   : goto op_OP_XOR_INT_LIT8                   ;
        case OP_SHL_INT_LIT8                   : goto op_OP_SHL_INT_LIT8                   ;
        case OP_SHR_INT_LIT8                   : goto op_OP_SHR_INT_LIT8                   ;
        case OP_USHR_INT_LIT8                  : goto op_OP_USHR_INT_LIT8                  ;
        case OP_IGET_VOLATILE                  : goto op_OP_IGET_VOLATILE                  ;
        case OP_IPUT_VOLATILE                  : goto op_OP_IPUT_VOLATILE                  ;
        case OP_SGET_VOLATILE                  : goto op_OP_SGET_VOLATILE                  ;
        case OP_SPUT_VOLATILE                  : goto op_OP_SPUT_VOLATILE                  ;
        case OP_IGET_OBJECT_VOLATILE           : goto op_OP_IGET_OBJECT_VOLATILE           ;
        case OP_IGET_WIDE_VOLATILE             : goto op_OP_IGET_WIDE_VOLATILE             ;
        case OP_IPUT_WIDE_VOLATILE             : goto op_OP_IPUT_WIDE_VOLATILE             ;
        case OP_SGET_WIDE_VOLATILE             : goto op_OP_SGET_WIDE_VOLATILE             ;
        case OP_SPUT_WIDE_VOLATILE             : goto op_OP_SPUT_WIDE_VOLATILE             ;
        case OP_BREAKPOINT                     : goto op_OP_BREAKPOINT                     ;
        case OP_THROW_VERIFICATION_ERROR       : goto op_OP_THROW_VERIFICATION_ERROR       ;
        case OP_EXECUTE_INLINE                 : goto op_OP_EXECUTE_INLINE                 ;
        case OP_EXECUTE_INLINE_RANGE           : goto op_OP_EXECUTE_INLINE_RANGE           ;
        case OP_INVOKE_OBJECT_INIT_RANGE       : goto op_OP_INVOKE_OBJECT_INIT_RANGE       ;
        case OP_RETURN_VOID_BARRIER            : goto op_OP_RETURN_VOID_BARRIER            ;
        case OP_IGET_QUICK                     : goto op_OP_IGET_QUICK                     ;
        case OP_IGET_WIDE_QUICK                : goto op_OP_IGET_WIDE_QUICK                ;
        case OP_IGET_OBJECT_QUICK              : goto op_OP_IGET_OBJECT_QUICK              ;
        case OP_IPUT_QUICK                     : goto op_OP_IPUT_QUICK                     ;
        case OP_IPUT_WIDE_QUICK                : goto op_OP_IPUT_WIDE_QUICK                ;
        case OP_IPUT_OBJECT_QUICK              : goto op_OP_IPUT_OBJECT_QUICK              ;
        case OP_INVOKE_VIRTUAL_QUICK           : goto op_OP_INVOKE_VIRTUAL_QUICK           ;
        case OP_INVOKE_VIRTUAL_QUICK_RANGE     : goto op_OP_INVOKE_VIRTUAL_QUICK_RANGE     ;
        case OP_INVOKE_SUPER_QUICK             : goto op_OP_INVOKE_SUPER_QUICK             ;
        case OP_INVOKE_SUPER_QUICK_RANGE       : goto op_OP_INVOKE_SUPER_QUICK_RANGE       ;
        case OP_IPUT_OBJECT_VOLATILE           : goto op_OP_IPUT_OBJECT_VOLATILE           ;
        case OP_SGET_OBJECT_VOLATILE           : goto op_OP_SGET_OBJECT_VOLATILE           ;
        case OP_SPUT_OBJECT_VOLATILE           : goto op_OP_SPUT_OBJECT_VOLATILE           ;
        case OP_DISPATCH_FF                    : goto op_OP_DISPATCH_FF                    ;
        case OP_CONST_CLASS_JUMBO              : goto op_OP_CONST_CLASS_JUMBO              ;
        case OP_CHECK_CAST_JUMBO               : goto op_OP_CHECK_CAST_JUMBO               ;
        case OP_INSTANCE_OF_JUMBO              : goto op_OP_INSTANCE_OF_JUMBO              ;
        case OP_NEW_INSTANCE_JUMBO             : goto op_OP_NEW_INSTANCE_JUMBO             ;
        case OP_NEW_ARRAY_JUMBO                : goto op_OP_NEW_ARRAY_JUMBO                ;
        case OP_FILLED_NEW_ARRAY_JUMBO         : goto op_OP_FILLED_NEW_ARRAY_JUMBO         ;
        case OP_IGET_JUMBO                     : goto op_OP_IGET_JUMBO                     ;
        case OP_IGET_WIDE_JUMBO                : goto op_OP_IGET_WIDE_JUMBO                ;
        case OP_IGET_OBJECT_JUMBO              : goto op_OP_IGET_OBJECT_JUMBO              ;
        case OP_IGET_BOOLEAN_JUMBO             : goto op_OP_IGET_BOOLEAN_JUMBO             ;
        case OP_IGET_BYTE_JUMBO                : goto op_OP_IGET_BYTE_JUMBO                ;
        case OP_IGET_CHAR_JUMBO                : goto op_OP_IGET_CHAR_JUMBO                ;
        case OP_IGET_SHORT_JUMBO               : goto op_OP_IGET_SHORT_JUMBO               ;
        case OP_IPUT_JUMBO                     : goto op_OP_IPUT_JUMBO                     ;
        case OP_IPUT_WIDE_JUMBO                : goto op_OP_IPUT_WIDE_JUMBO                ;
        case OP_IPUT_OBJECT_JUMBO              : goto op_OP_IPUT_OBJECT_JUMBO              ;
        case OP_IPUT_BOOLEAN_JUMBO             : goto op_OP_IPUT_BOOLEAN_JUMBO             ;
        case OP_IPUT_BYTE_JUMBO                : goto op_OP_IPUT_BYTE_JUMBO                ;
        case OP_IPUT_CHAR_JUMBO                : goto op_OP_IPUT_CHAR_JUMBO                ;
        case OP_IPUT_SHORT_JUMBO               : goto op_OP_IPUT_SHORT_JUMBO               ;
        case OP_SGET_JUMBO                     : goto op_OP_SGET_JUMBO                     ;
        case OP_SGET_WIDE_JUMBO                : goto op_OP_SGET_WIDE_JUMBO                ;
        case OP_SGET_OBJECT_JUMBO              : goto op_OP_SGET_OBJECT_JUMBO              ;
        case OP_SGET_BOOLEAN_JUMBO             : goto op_OP_SGET_BOOLEAN_JUMBO             ;
        case OP_SGET_BYTE_JUMBO                : goto op_OP_SGET_BYTE_JUMBO                ;
        case OP_SGET_CHAR_JUMBO                : goto op_OP_SGET_CHAR_JUMBO                ;
        case OP_SGET_SHORT_JUMBO               : goto op_OP_SGET_SHORT_JUMBO               ;
        case OP_SPUT_JUMBO                     : goto op_OP_SPUT_JUMBO                     ;
        case OP_SPUT_WIDE_JUMBO                : goto op_OP_SPUT_WIDE_JUMBO                ;
        case OP_SPUT_OBJECT_JUMBO              : goto op_OP_SPUT_OBJECT_JUMBO              ;
        case OP_SPUT_BOOLEAN_JUMBO             : goto op_OP_SPUT_BOOLEAN_JUMBO             ;
        case OP_SPUT_BYTE_JUMBO                : goto op_OP_SPUT_BYTE_JUMBO                ;
        case OP_SPUT_CHAR_JUMBO                : goto op_OP_SPUT_CHAR_JUMBO                ;
        case OP_SPUT_SHORT_JUMBO               : goto op_OP_SPUT_SHORT_JUMBO               ;
        case OP_INVOKE_VIRTUAL_JUMBO           : goto op_OP_INVOKE_VIRTUAL_JUMBO           ;
        case OP_INVOKE_SUPER_JUMBO             : goto op_OP_INVOKE_SUPER_JUMBO             ;
        case OP_INVOKE_DIRECT_JUMBO            : goto op_OP_INVOKE_DIRECT_JUMBO            ;
        case OP_INVOKE_STATIC_JUMBO            : goto op_OP_INVOKE_STATIC_JUMBO            ;
        case OP_INVOKE_INTERFACE_JUMBO         : goto op_OP_INVOKE_INTERFACE_JUMBO         ;
        case OP_UNUSED_27FF                    : goto op_OP_UNUSED_27FF                    ;
        case OP_UNUSED_28FF                    : goto op_OP_UNUSED_28FF                    ;
        case OP_UNUSED_29FF                    : goto op_OP_UNUSED_29FF                    ;
        case OP_UNUSED_2AFF                    : goto op_OP_UNUSED_2AFF                    ;
        case OP_UNUSED_2BFF                    : goto op_OP_UNUSED_2BFF                    ;
        case OP_UNUSED_2CFF                    : goto op_OP_UNUSED_2CFF                    ;
        case OP_UNUSED_2DFF                    : goto op_OP_UNUSED_2DFF                    ;
        case OP_UNUSED_2EFF                    : goto op_OP_UNUSED_2EFF                    ;
        case OP_UNUSED_2FFF                    : goto op_OP_UNUSED_2FFF                    ;
        case OP_UNUSED_30FF                    : goto op_OP_UNUSED_30FF                    ;
        case OP_UNUSED_31FF                    : goto op_OP_UNUSED_31FF                    ;
        case OP_UNUSED_32FF                    : goto op_OP_UNUSED_32FF                    ;
        case OP_UNUSED_33FF                    : goto op_OP_UNUSED_33FF                    ;
        case OP_UNUSED_34FF                    : goto op_OP_UNUSED_34FF                    ;
        case OP_UNUSED_35FF                    : goto op_OP_UNUSED_35FF                    ;
        case OP_UNUSED_36FF                    : goto op_OP_UNUSED_36FF                    ;
        case OP_UNUSED_37FF                    : goto op_OP_UNUSED_37FF                    ;
        case OP_UNUSED_38FF                    : goto op_OP_UNUSED_38FF                    ;
        case OP_UNUSED_39FF                    : goto op_OP_UNUSED_39FF                    ;
        case OP_UNUSED_3AFF                    : goto op_OP_UNUSED_3AFF                    ;
        case OP_UNUSED_3BFF                    : goto op_OP_UNUSED_3BFF                    ;
        case OP_UNUSED_3CFF                    : goto op_OP_UNUSED_3CFF                    ;
        case OP_UNUSED_3DFF                    : goto op_OP_UNUSED_3DFF                    ;
        case OP_UNUSED_3EFF                    : goto op_OP_UNUSED_3EFF                    ;
        case OP_UNUSED_3FFF                    : goto op_OP_UNUSED_3FFF                    ;
        case OP_UNUSED_40FF                    : goto op_OP_UNUSED_40FF                    ;
        case OP_UNUSED_41FF                    : goto op_OP_UNUSED_41FF                    ;
        case OP_UNUSED_42FF                    : goto op_OP_UNUSED_42FF                    ;
        case OP_UNUSED_43FF                    : goto op_OP_UNUSED_43FF                    ;
        case OP_UNUSED_44FF                    : goto op_OP_UNUSED_44FF                    ;
        case OP_UNUSED_45FF                    : goto op_OP_UNUSED_45FF                    ;
        case OP_UNUSED_46FF                    : goto op_OP_UNUSED_46FF                    ;
        case OP_UNUSED_47FF                    : goto op_OP_UNUSED_47FF                    ;
        case OP_UNUSED_48FF                    : goto op_OP_UNUSED_48FF                    ;
        case OP_UNUSED_49FF                    : goto op_OP_UNUSED_49FF                    ;
        case OP_UNUSED_4AFF                    : goto op_OP_UNUSED_4AFF                    ;
        case OP_UNUSED_4BFF                    : goto op_OP_UNUSED_4BFF                    ;
        case OP_UNUSED_4CFF                    : goto op_OP_UNUSED_4CFF                    ;
        case OP_UNUSED_4DFF                    : goto op_OP_UNUSED_4DFF                    ;
        case OP_UNUSED_4EFF                    : goto op_OP_UNUSED_4EFF                    ;
        case OP_UNUSED_4FFF                    : goto op_OP_UNUSED_4FFF                    ;
        case OP_UNUSED_50FF                    : goto op_OP_UNUSED_50FF                    ;
        case OP_UNUSED_51FF                    : goto op_OP_UNUSED_51FF                    ;
        case OP_UNUSED_52FF                    : goto op_OP_UNUSED_52FF                    ;
        case OP_UNUSED_53FF                    : goto op_OP_UNUSED_53FF                    ;
        case OP_UNUSED_54FF                    : goto op_OP_UNUSED_54FF                    ;
        case OP_UNUSED_55FF                    : goto op_OP_UNUSED_55FF                    ;
        case OP_UNUSED_56FF                    : goto op_OP_UNUSED_56FF                    ;
        case OP_UNUSED_57FF                    : goto op_OP_UNUSED_57FF                    ;
        case OP_UNUSED_58FF                    : goto op_OP_UNUSED_58FF                    ;
        case OP_UNUSED_59FF                    : goto op_OP_UNUSED_59FF                    ;
        case OP_UNUSED_5AFF                    : goto op_OP_UNUSED_5AFF                    ;
        case OP_UNUSED_5BFF                    : goto op_OP_UNUSED_5BFF                    ;
        case OP_UNUSED_5CFF                    : goto op_OP_UNUSED_5CFF                    ;
        case OP_UNUSED_5DFF                    : goto op_OP_UNUSED_5DFF                    ;
        case OP_UNUSED_5EFF                    : goto op_OP_UNUSED_5EFF                    ;
        case OP_UNUSED_5FFF                    : goto op_OP_UNUSED_5FFF                    ;
        case OP_UNUSED_60FF                    : goto op_OP_UNUSED_60FF                    ;
        case OP_UNUSED_61FF                    : goto op_OP_UNUSED_61FF                    ;
        case OP_UNUSED_62FF                    : goto op_OP_UNUSED_62FF                    ;
        case OP_UNUSED_63FF                    : goto op_OP_UNUSED_63FF                    ;
        case OP_UNUSED_64FF                    : goto op_OP_UNUSED_64FF                    ;
        case OP_UNUSED_65FF                    : goto op_OP_UNUSED_65FF                    ;
        case OP_UNUSED_66FF                    : goto op_OP_UNUSED_66FF                    ;
        case OP_UNUSED_67FF                    : goto op_OP_UNUSED_67FF                    ;
        case OP_UNUSED_68FF                    : goto op_OP_UNUSED_68FF                    ;
        case OP_UNUSED_69FF                    : goto op_OP_UNUSED_69FF                    ;
        case OP_UNUSED_6AFF                    : goto op_OP_UNUSED_6AFF                    ;
        case OP_UNUSED_6BFF                    : goto op_OP_UNUSED_6BFF                    ;
        case OP_UNUSED_6CFF                    : goto op_OP_UNUSED_6CFF                    ;
        case OP_UNUSED_6DFF                    : goto op_OP_UNUSED_6DFF                    ;
        case OP_UNUSED_6EFF                    : goto op_OP_UNUSED_6EFF                    ;
        case OP_UNUSED_6FFF                    : goto op_OP_UNUSED_6FFF                    ;
        case OP_UNUSED_70FF                    : goto op_OP_UNUSED_70FF                    ;
        case OP_UNUSED_71FF                    : goto op_OP_UNUSED_71FF                    ;
        case OP_UNUSED_72FF                    : goto op_OP_UNUSED_72FF                    ;
        case OP_UNUSED_73FF                    : goto op_OP_UNUSED_73FF                    ;
        case OP_UNUSED_74FF                    : goto op_OP_UNUSED_74FF                    ;
        case OP_UNUSED_75FF                    : goto op_OP_UNUSED_75FF                    ;
        case OP_UNUSED_76FF                    : goto op_OP_UNUSED_76FF                    ;
        case OP_UNUSED_77FF                    : goto op_OP_UNUSED_77FF                    ;
        case OP_UNUSED_78FF                    : goto op_OP_UNUSED_78FF                    ;
        case OP_UNUSED_79FF                    : goto op_OP_UNUSED_79FF                    ;
        case OP_UNUSED_7AFF                    : goto op_OP_UNUSED_7AFF                    ;
        case OP_UNUSED_7BFF                    : goto op_OP_UNUSED_7BFF                    ;
        case OP_UNUSED_7CFF                    : goto op_OP_UNUSED_7CFF                    ;
        case OP_UNUSED_7DFF                    : goto op_OP_UNUSED_7DFF                    ;
        case OP_UNUSED_7EFF                    : goto op_OP_UNUSED_7EFF                    ;
        case OP_UNUSED_7FFF                    : goto op_OP_UNUSED_7FFF                    ;
        case OP_UNUSED_80FF                    : goto op_OP_UNUSED_80FF                    ;
        case OP_UNUSED_81FF                    : goto op_OP_UNUSED_81FF                    ;
        case OP_UNUSED_82FF                    : goto op_OP_UNUSED_82FF                    ;
        case OP_UNUSED_83FF                    : goto op_OP_UNUSED_83FF                    ;
        case OP_UNUSED_84FF                    : goto op_OP_UNUSED_84FF                    ;
        case OP_UNUSED_85FF                    : goto op_OP_UNUSED_85FF                    ;
        case OP_UNUSED_86FF                    : goto op_OP_UNUSED_86FF                    ;
        case OP_UNUSED_87FF                    : goto op_OP_UNUSED_87FF                    ;
        case OP_UNUSED_88FF                    : goto op_OP_UNUSED_88FF                    ;
        case OP_UNUSED_89FF                    : goto op_OP_UNUSED_89FF                    ;
        case OP_UNUSED_8AFF                    : goto op_OP_UNUSED_8AFF                    ;
        case OP_UNUSED_8BFF                    : goto op_OP_UNUSED_8BFF                    ;
        case OP_UNUSED_8CFF                    : goto op_OP_UNUSED_8CFF                    ;
        case OP_UNUSED_8DFF                    : goto op_OP_UNUSED_8DFF                    ;
        case OP_UNUSED_8EFF                    : goto op_OP_UNUSED_8EFF                    ;
        case OP_UNUSED_8FFF                    : goto op_OP_UNUSED_8FFF                    ;
        case OP_UNUSED_90FF                    : goto op_OP_UNUSED_90FF                    ;
        case OP_UNUSED_91FF                    : goto op_OP_UNUSED_91FF                    ;
        case OP_UNUSED_92FF                    : goto op_OP_UNUSED_92FF                    ;
        case OP_UNUSED_93FF                    : goto op_OP_UNUSED_93FF                    ;
        case OP_UNUSED_94FF                    : goto op_OP_UNUSED_94FF                    ;
        case OP_UNUSED_95FF                    : goto op_OP_UNUSED_95FF                    ;
        case OP_UNUSED_96FF                    : goto op_OP_UNUSED_96FF                    ;
        case OP_UNUSED_97FF                    : goto op_OP_UNUSED_97FF                    ;
        case OP_UNUSED_98FF                    : goto op_OP_UNUSED_98FF                    ;
        case OP_UNUSED_99FF                    : goto op_OP_UNUSED_99FF                    ;
        case OP_UNUSED_9AFF                    : goto op_OP_UNUSED_9AFF                    ;
        case OP_UNUSED_9BFF                    : goto op_OP_UNUSED_9BFF                    ;
        case OP_UNUSED_9CFF                    : goto op_OP_UNUSED_9CFF                    ;
        case OP_UNUSED_9DFF                    : goto op_OP_UNUSED_9DFF                    ;
        case OP_UNUSED_9EFF                    : goto op_OP_UNUSED_9EFF                    ;
        case OP_UNUSED_9FFF                    : goto op_OP_UNUSED_9FFF                    ;
        case OP_UNUSED_A0FF                    : goto op_OP_UNUSED_A0FF                    ;
        case OP_UNUSED_A1FF                    : goto op_OP_UNUSED_A1FF                    ;
        case OP_UNUSED_A2FF                    : goto op_OP_UNUSED_A2FF                    ;
        case OP_UNUSED_A3FF                    : goto op_OP_UNUSED_A3FF                    ;
        case OP_UNUSED_A4FF                    : goto op_OP_UNUSED_A4FF                    ;
        case OP_UNUSED_A5FF                    : goto op_OP_UNUSED_A5FF                    ;
        case OP_UNUSED_A6FF                    : goto op_OP_UNUSED_A6FF                    ;
        case OP_UNUSED_A7FF                    : goto op_OP_UNUSED_A7FF                    ;
        case OP_UNUSED_A8FF                    : goto op_OP_UNUSED_A8FF                    ;
        case OP_UNUSED_A9FF                    : goto op_OP_UNUSED_A9FF                    ;
        case OP_UNUSED_AAFF                    : goto op_OP_UNUSED_AAFF                    ;
        case OP_UNUSED_ABFF                    : goto op_OP_UNUSED_ABFF                    ;
        case OP_UNUSED_ACFF                    : goto op_OP_UNUSED_ACFF                    ;
        case OP_UNUSED_ADFF                    : goto op_OP_UNUSED_ADFF                    ;
        case OP_UNUSED_AEFF                    : goto op_OP_UNUSED_AEFF                    ;
        case OP_UNUSED_AFFF                    : goto op_OP_UNUSED_AFFF                    ;
        case OP_UNUSED_B0FF                    : goto op_OP_UNUSED_B0FF                    ;
        case OP_UNUSED_B1FF                    : goto op_OP_UNUSED_B1FF                    ;
        case OP_UNUSED_B2FF                    : goto op_OP_UNUSED_B2FF                    ;
        case OP_UNUSED_B3FF                    : goto op_OP_UNUSED_B3FF                    ;
        case OP_UNUSED_B4FF                    : goto op_OP_UNUSED_B4FF                    ;
        case OP_UNUSED_B5FF                    : goto op_OP_UNUSED_B5FF                    ;
        case OP_UNUSED_B6FF                    : goto op_OP_UNUSED_B6FF                    ;
        case OP_UNUSED_B7FF                    : goto op_OP_UNUSED_B7FF                    ;
        case OP_UNUSED_B8FF                    : goto op_OP_UNUSED_B8FF                    ;
        case OP_UNUSED_B9FF                    : goto op_OP_UNUSED_B9FF                    ;
        case OP_UNUSED_BAFF                    : goto op_OP_UNUSED_BAFF                    ;
        case OP_UNUSED_BBFF                    : goto op_OP_UNUSED_BBFF                    ;
        case OP_UNUSED_BCFF                    : goto op_OP_UNUSED_BCFF                    ;
        case OP_UNUSED_BDFF                    : goto op_OP_UNUSED_BDFF                    ;
        case OP_UNUSED_BEFF                    : goto op_OP_UNUSED_BEFF                    ;
        case OP_UNUSED_BFFF                    : goto op_OP_UNUSED_BFFF                    ;
        case OP_UNUSED_C0FF                    : goto op_OP_UNUSED_C0FF                    ;
        case OP_UNUSED_C1FF                    : goto op_OP_UNUSED_C1FF                    ;
        case OP_UNUSED_C2FF                    : goto op_OP_UNUSED_C2FF                    ;
        case OP_UNUSED_C3FF                    : goto op_OP_UNUSED_C3FF                    ;
        case OP_UNUSED_C4FF                    : goto op_OP_UNUSED_C4FF                    ;
        case OP_UNUSED_C5FF                    : goto op_OP_UNUSED_C5FF                    ;
        case OP_UNUSED_C6FF                    : goto op_OP_UNUSED_C6FF                    ;
        case OP_UNUSED_C7FF                    : goto op_OP_UNUSED_C7FF                    ;
        case OP_UNUSED_C8FF                    : goto op_OP_UNUSED_C8FF                    ;
        case OP_UNUSED_C9FF                    : goto op_OP_UNUSED_C9FF                    ;
        case OP_UNUSED_CAFF                    : goto op_OP_UNUSED_CAFF                    ;
        case OP_UNUSED_CBFF                    : goto op_OP_UNUSED_CBFF                    ;
        case OP_UNUSED_CCFF                    : goto op_OP_UNUSED_CCFF                    ;
        case OP_UNUSED_CDFF                    : goto op_OP_UNUSED_CDFF                    ;
        case OP_UNUSED_CEFF                    : goto op_OP_UNUSED_CEFF                    ;
        case OP_UNUSED_CFFF                    : goto op_OP_UNUSED_CFFF                    ;
        case OP_UNUSED_D0FF                    : goto op_OP_UNUSED_D0FF                    ;
        case OP_UNUSED_D1FF                    : goto op_OP_UNUSED_D1FF                    ;
        case OP_UNUSED_D2FF                    : goto op_OP_UNUSED_D2FF                    ;
        case OP_UNUSED_D3FF                    : goto op_OP_UNUSED_D3FF                    ;
        case OP_UNUSED_D4FF                    : goto op_OP_UNUSED_D4FF                    ;
        case OP_UNUSED_D5FF                    : goto op_OP_UNUSED_D5FF                    ;
        case OP_UNUSED_D6FF                    : goto op_OP_UNUSED_D6FF                    ;
        case OP_UNUSED_D7FF                    : goto op_OP_UNUSED_D7FF                    ;
        case OP_UNUSED_D8FF                    : goto op_OP_UNUSED_D8FF                    ;
        case OP_UNUSED_D9FF                    : goto op_OP_UNUSED_D9FF                    ;
        case OP_UNUSED_DAFF                    : goto op_OP_UNUSED_DAFF                    ;
        case OP_UNUSED_DBFF                    : goto op_OP_UNUSED_DBFF                    ;
        case OP_UNUSED_DCFF                    : goto op_OP_UNUSED_DCFF                    ;
        case OP_UNUSED_DDFF                    : goto op_OP_UNUSED_DDFF                    ;
        case OP_UNUSED_DEFF                    : goto op_OP_UNUSED_DEFF                    ;
        case OP_UNUSED_DFFF                    : goto op_OP_UNUSED_DFFF                    ;
        case OP_UNUSED_E0FF                    : goto op_OP_UNUSED_E0FF                    ;
        case OP_UNUSED_E1FF                    : goto op_OP_UNUSED_E1FF                    ;
        case OP_UNUSED_E2FF                    : goto op_OP_UNUSED_E2FF                    ;
        case OP_UNUSED_E3FF                    : goto op_OP_UNUSED_E3FF                    ;
        case OP_UNUSED_E4FF                    : goto op_OP_UNUSED_E4FF                    ;
        case OP_UNUSED_E5FF                    : goto op_OP_UNUSED_E5FF                    ;
        case OP_UNUSED_E6FF                    : goto op_OP_UNUSED_E6FF                    ;
        case OP_UNUSED_E7FF                    : goto op_OP_UNUSED_E7FF                    ;
        case OP_UNUSED_E8FF                    : goto op_OP_UNUSED_E8FF                    ;
        case OP_UNUSED_E9FF                    : goto op_OP_UNUSED_E9FF                    ;
        case OP_UNUSED_EAFF                    : goto op_OP_UNUSED_EAFF                    ;
        case OP_UNUSED_EBFF                    : goto op_OP_UNUSED_EBFF                    ;
        case OP_UNUSED_ECFF                    : goto op_OP_UNUSED_ECFF                    ;
        case OP_UNUSED_EDFF                    : goto op_OP_UNUSED_EDFF                    ;
        case OP_UNUSED_EEFF                    : goto op_OP_UNUSED_EEFF                    ;
        case OP_UNUSED_EFFF                    : goto op_OP_UNUSED_EFFF                    ;
        case OP_UNUSED_F0FF                    : goto op_OP_UNUSED_F0FF                    ;
        case OP_UNUSED_F1FF                    : goto op_OP_UNUSED_F1FF                    ;
        case OP_INVOKE_OBJECT_INIT_JUMBO       : goto op_OP_INVOKE_OBJECT_INIT_JUMBO       ;
        case OP_IGET_VOLATILE_JUMBO            : goto op_OP_IGET_VOLATILE_JUMBO            ;
        case OP_IGET_WIDE_VOLATILE_JUMBO       : goto op_OP_IGET_WIDE_VOLATILE_JUMBO       ;
        case OP_IGET_OBJECT_VOLATILE_JUMBO     : goto op_OP_IGET_OBJECT_VOLATILE_JUMBO     ;
        case OP_IPUT_VOLATILE_JUMBO            : goto op_OP_IPUT_VOLATILE_JUMBO            ;
        case OP_IPUT_WIDE_VOLATILE_JUMBO       : goto op_OP_IPUT_WIDE_VOLATILE_JUMBO       ;
        case OP_IPUT_OBJECT_VOLATILE_JUMBO     : goto op_OP_IPUT_OBJECT_VOLATILE_JUMBO     ;
        case OP_SGET_VOLATILE_JUMBO            : goto op_OP_SGET_VOLATILE_JUMBO            ;
        case OP_SGET_WIDE_VOLATILE_JUMBO       : goto op_OP_SGET_WIDE_VOLATILE_JUMBO       ;
        case OP_SGET_OBJECT_VOLATILE_JUMBO     : goto op_OP_SGET_OBJECT_VOLATILE_JUMBO     ;
        case OP_SPUT_VOLATILE_JUMBO            : goto op_OP_SPUT_VOLATILE_JUMBO            ;
        case OP_SPUT_WIDE_VOLATILE_JUMBO       : goto op_OP_SPUT_WIDE_VOLATILE_JUMBO       ;
        case OP_SPUT_OBJECT_VOLATILE_JUMBO     : goto op_OP_SPUT_OBJECT_VOLATILE_JUMBO     ;
        case OP_THROW_VERIFICATION_ERROR_JUMBO : goto op_OP_THROW_VERIFICATION_ERROR_JUMBO ;
        default: goto bail;
    }
#endif

bail:
#if __NIX__
    ILOGD("|-- Leaving interpreter loop");      // note "curMethod" may be NULL
#endif
    self->interpSave.retval = retval;
	return;
}
