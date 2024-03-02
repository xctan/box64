#ifndef __DYNAREC_LA64_HELPER_H__
#define __DYNAREC_LA64_HELPER_H__

// undef to get Close to SSE Float->int conversions
// #define PRECISE_CVT

#if STEP == 0
#include "dynarec_la64_pass0.h"
#elif STEP == 1
#include "dynarec_la64_pass1.h"
#elif STEP == 2
#include "dynarec_la64_pass2.h"
#elif STEP == 3
#include "dynarec_la64_pass3.h"
#endif

#include "debug.h"
#include "la64_emitter.h"
#include "../emu/x64primop.h"

#define F8      *(uint8_t*)(addr++)
#define F8S     *(int8_t*)(addr++)
#define F16     *(uint16_t*)(addr += 2, addr - 2)
#define F16S    *(int16_t*)(addr += 2, addr - 2)
#define F32     *(uint32_t*)(addr += 4, addr - 4)
#define F32S    *(int32_t*)(addr += 4, addr - 4)
#define F32S64  (uint64_t)(int64_t) F32S
#define F64     *(uint64_t*)(addr += 8, addr - 8)
#define PK(a)   *(uint8_t*)(addr + a)
#define PK16(a) *(uint16_t*)(addr + a)
#define PK32(a) *(uint32_t*)(addr + a)
#define PK64(a) *(uint64_t*)(addr + a)
#define PKip(a) *(uint8_t*)(ip + a)

// Strong mem emulation helpers
#define SMREAD_MIN  2
#define SMWRITE_MIN 1
// Sequence of Read will trigger a DMB on "first" read if strongmem is >= SMREAD_MIN
// Sequence of Write will trigger a DMB on "last" write if strongmem is >= 1
// All Write operation that might use a lock all have a memory barrier if strongmem is >= SMWRITE_MIN
// Opcode will read
#define SMREAD()                                                        \
    ;                                                                   \
    if ((dyn->smread == 0) && (box64_dynarec_strongmem > SMREAD_MIN)) { \
        SMDMB();                                                        \
    } else                                                              \
        dyn->smread = 1
// Opcode will read with option forced lock
#define SMREADLOCK(lock) \
    if ((lock) || ((dyn->smread == 0) && (box64_dynarec_strongmem > SMREAD_MIN))) { SMDMB(); }
// Opcode might read (depend on nextop)
#define SMMIGHTREAD() \
    if (!MODREG) { SMREAD(); }
// Opcode has wrote
#define SMWRITE() dyn->smwrite = 1
// Opcode has wrote (strongmem>1 only)
#define SMWRITE2() \
    if (box64_dynarec_strongmem > SMREAD_MIN) dyn->smwrite = 1
// Opcode has wrote with option forced lock
#define SMWRITELOCK(lock)                                  \
    if (lock || (box64_dynarec_strongmem > SMWRITE_MIN)) { \
        SMDMB();                                           \
    } else                                                 \
        dyn->smwrite = 1
// Opcode might have wrote (depend on nextop)
#define SMMIGHTWRITE() \
    if (!MODREG) { SMWRITE(); }
// Start of sequence
#define SMSTART() SMEND()
// End of sequence
#define SMEND()                                               \
    if (dyn->smwrite && box64_dynarec_strongmem) { DBAR(0); } \
    dyn->smwrite = 0;                                         \
    dyn->smread = 0;
// Force a Data memory barrier (for LOCK: prefix)
#define SMDMB()       \
    DBAR(0);          \
    dyn->smwrite = 0; \
    dyn->smread = 1

// LOCK_* define
#define LOCK_LOCK (int*)1

// GETGD    get x64 register in gd
#define GETGD gd = TO_LA64(((nextop & 0x38) >> 3) + (rex.r << 3));

// GETED can use r1 for ed, and r2 for wback. wback is 0 if ed is xEAX..xEDI
#define GETED(D)                                                                                \
    if (MODREG) {                                                                               \
        ed = TO_LA64((nextop & 7) + (rex.b << 3));                                              \
        wback = 0;                                                                              \
    } else {                                                                                    \
        SMREAD();                                                                               \
        addr = geted(dyn, addr, ninst, nextop, &wback, x2, x1, &fixedaddress, rex, NULL, 1, D); \
        LDxw(x1, wback, fixedaddress);                                                          \
        ed = x1;                                                                                \
    }

#define GETEDz(D)                                                                               \
    if (MODREG) {                                                                               \
        ed = TO_LA64((nextop & 7) + (rex.b << 3));                                              \
        wback = 0;                                                                              \
    } else {                                                                                    \
        SMREAD();                                                                               \
        addr = geted(dyn, addr, ninst, nextop, &wback, x2, x1, &fixedaddress, rex, NULL, 1, D); \
        LDz(x1, wback, fixedaddress);                                                           \
        ed = x1;                                                                                \
    }

// Write back ed in wback (if wback not 0)
#define WBACK                              \
    if (wback) {                           \
        if (rex.w)                         \
            ST_D(ed, wback, fixedaddress); \
        else                               \
            ST_W(ed, wback, fixedaddress); \
        SMWRITE();                         \
    }

// GETEB will use i for ed, and can use r3 for wback.
#define GETEB(i, D)                                                                             \
    if (MODREG) {                                                                               \
        if (rex.rex) {                                                                          \
            wback = TO_LA64((nextop & 7) + (rex.b << 3));                                       \
            wb2 = 0;                                                                            \
        } else {                                                                                \
            wback = (nextop & 7);                                                               \
            wb2 = (wback >> 2) * 8;                                                             \
            wback = TO_LA64((wback & 3));                                                       \
        }                                                                                       \
        if (wb2) {                                                                              \
            MV(i, wback);                                                                       \
            SRLI_D(i, i, wb2);                                                                  \
            ANDI(i, i, 0xff);                                                                   \
        } else                                                                                  \
            ANDI(i, wback, 0xff);                                                               \
        wb1 = 0;                                                                                \
        ed = i;                                                                                 \
    } else {                                                                                    \
        SMREAD();                                                                               \
        addr = geted(dyn, addr, ninst, nextop, &wback, x3, x2, &fixedaddress, rex, NULL, 1, D); \
        LD_BU(i, wback, fixedaddress);                                                          \
        wb1 = 1;                                                                                \
        ed = i;                                                                                 \
    }

// GETGB will use i for gd
#define GETGB(i)                                              \
    if (rex.rex) {                                            \
        gb1 = TO_LA64(((nextop & 0x38) >> 3) + (rex.r << 3)); \
        gb2 = 0;                                              \
    } else {                                                  \
        gd = (nextop & 0x38) >> 3;                            \
        gb2 = ((gd & 4) >> 2);                                \
        gb1 = TO_LA64((gd & 3));                              \
    }                                                         \
    gd = i;                                                   \
    if (gb2) {                                                \
        MV(gd, gb1);                                          \
        SRLI_D(gd, gd, 8);                                    \
        ANDI(gd, gd, 0xff);                                   \
    } else                                                    \
        ANDI(gd, gb1, 0xff);

// Write gb (gd) back to original register / memory, using s1 as scratch
#define GBBACK(s1)                        \
    if (gb2) {                            \
        MOV64x(s1, 0xffffffffffff00ffLL); \
        AND(gb1, gb1, s1);                \
        SLLI_D(s1, gd, 8);                \
        OR(gb1, gb1, s1);                 \
    } else {                              \
        ANDI(gb1, gb1, ~0xff);            \
        OR(gb1, gb1, gd);                 \
    }

// Write eb (ed) back to original register / memory, using s1 as scratch
#define EBBACK(s1, c)                     \
    if (wb1) {                            \
        SUB_D(ed, wback, fixedaddress);   \
        SMWRITE();                        \
    } else if (wb2) {                     \
        MOV64x(s1, 0xffffffffffff00ffLL); \
        AND(wback, wback, s1);            \
        if (c) { ANDI(ed, ed, 0xff); }    \
        SLLI_D(s1, ed, 8);                \
        OR(wback, wback, s1);             \
    } else {                              \
        ANDI(wback, wback, ~0xff);        \
        if (c) { ANDI(ed, ed, 0xff); }    \
        OR(wback, wback, ed);             \
    }

// CALL will use x6 for the call address. Return value can be put in ret (unless ret is -1)
// R0 will not be pushed/popd if ret is -2
#define CALL(F, ret) call_c(dyn, ninst, F, x6, ret, 1, 0)
// CALL_ will use x6 for the call address. Return value can be put in ret (unless ret is -1)
// R0 will not be pushed/popd if ret is -2
#define CALL_(F, ret, reg) call_c(dyn, ninst, F, x6, ret, 1, reg)
// CALL_S will use x6 for the call address. Return value can be put in ret (unless ret is -1)
// R0 will not be pushed/popd if ret is -2. Flags are not save/restored
#define CALL_S(F, ret) call_c(dyn, ninst, F, x6, ret, 0, 0)

#define MARKi(i)    dyn->insts[ninst].mark[i] = dyn->native_size
#define GETMARKi(i) dyn->insts[ninst].mark[i]
#define MARK        MARKi(0)
#define GETMARK     GETMARKi(0)
#define MARK2       MARKi(1)
#define GETMARK2    GETMARKi(1)
#define MARK3       MARKi(2)
#define GETMARK3    GETMARKi(2)

#define MARKFi(i)    dyn->insts[ninst].markf[i] = dyn->native_size
#define GETMARKFi(i) dyn->insts[ninst].markf[i]
#define MARKF        MARKFi(0)
#define GETMARKF     GETMARKFi(0)
#define MARKF2       MARKFi(1)
#define GETMARKF2    GETMARKFi(1)

#define MARKSEG     dyn->insts[ninst].markseg = dyn->native_size
#define GETMARKSEG  dyn->insts[ninst].markseg
#define MARKLOCK    dyn->insts[ninst].marklock = dyn->native_size
#define GETMARKLOCK dyn->insts[ninst].marklock

#define IFX(A)      if ((dyn->insts[ninst].x64.gen_flags & (A)))
#define IFX_PENDOR0 if ((dyn->insts[ninst].x64.gen_flags & (X_PEND) || !dyn->insts[ninst].x64.gen_flags))
#define IFXX(A)     if ((dyn->insts[ninst].x64.gen_flags == (A)))
#define IFX2X(A, B) if ((dyn->insts[ninst].x64.gen_flags == (A) || dyn->insts[ninst].x64.gen_flags == (B) || dyn->insts[ninst].x64.gen_flags == ((A) | (B))))
#define IFXN(A, B)  if ((dyn->insts[ninst].x64.gen_flags & (A) && !(dyn->insts[ninst].x64.gen_flags & (B))))

#define STORE_REG(A) ST_D(x##A, xEmu, offsetof(x64emu_t, regs[_##A]))
#define LOAD_REG(A)  LD_D(x##A, xEmu, offsetof(x64emu_t, regs[_##A]))

#define SET_DFNONE()                             \
    if (!dyn->f.dfnone) {                        \
        ST_W(xZR, xEmu, offsetof(x64emu_t, df)); \
        dyn->f.dfnone = 1;                       \
    }
#define SET_DF(S, N)                           \
    if ((N) != d_none) {                       \
        MOV32w(S, (N));                        \
        ST_W(S, xEmu, offsetof(x64emu_t, df)); \
        dyn->f.dfnone = 0;                     \
    } else                                     \
        SET_DFNONE()
#define SET_NODF() dyn->f.dfnone = 0
#define SET_DFOK() dyn->f.dfnone = 1

#define CLEAR_FLAGS(s) \
    IFX(X_ALL) { MOV64x(s, (1UL << F_AF) | (1UL << F_CF) | (1UL << F_OF) | (1UL << F_ZF) | (1UL << F_SF) | (1UL << F_PF)); ANDN(xFlags, xFlags, s); }

#define CALC_SUB_FLAGS(op1_, op2, res, scratch1, scratch2, width)     \
    IFX(X_AF | X_CF | X_OF)                                           \
    {                                                                 \
        /* calc borrow chain */                                       \
        /* bc = (res & (~op1 | op2)) | (~op1 & op2) */                \
        OR(scratch1, op1_, op2);                                      \
        AND(scratch2, res, scratch1);                                 \
        AND(op1_, op1_, op2);                                         \
        OR(scratch2, scratch2, op1_);                                 \
        IFX(X_AF)                                                     \
        {                                                             \
            /* af = bc & 0x8 */                                       \
            ANDI(scratch1, scratch2, 8);                              \
            BEQZ(scratch1, 8);                                        \
            ORI(xFlags, xFlags, 1 << F_AF);                           \
        }                                                             \
        IFX(X_CF)                                                     \
        {                                                             \
            /* cf = bc & (1<<(width-1)) */                            \
            if ((width) == 8) {                                       \
                ANDI(scratch1, scratch2, 0x80);                       \
            } else {                                                  \
                SRLI_D(scratch1, scratch2, (width)-1);                \
                if (width != 64) ANDI(scratch1, scratch1, 1);         \
            }                                                         \
            BEQZ(scratch1, 8);                                        \
            ORI(xFlags, xFlags, 1 << F_CF);                           \
        }                                                             \
        IFX(X_OF)                                                     \
        {                                                             \
            /* of = ((bc >> (width-2)) ^ (bc >> (width-1))) & 0x1; */ \
            SRLI_D(scratch1, scratch2, (width)-2);                    \
            SRLI_D(scratch2, scratch1, 1);                            \
            XOR(scratch1, scratch1, scratch2);                        \
            ANDI(scratch1, scratch1, 1);                              \
            BEQZ(scratch1, 8);                                        \
            ORI(xFlags, xFlags, 1 << F_OF);                           \
        }                                                             \
    }

#ifndef READFLAGS
#define READFLAGS(A)                                \
    if (((A) != X_PEND && dyn->f.pending != SF_SET) \
        && (dyn->f.pending != SF_SET_PENDING)) {    \
        if (dyn->f.pending != SF_PENDING) {         \
            LD_D(x3, xEmu, offsetof(x64emu_t, df)); \
            j64 = (GETMARKF) - (dyn->native_size);  \
            BEQ(x3, xZR, j64);                      \
        }                                           \
        CALL_(UpdateFlags, -1, 0);                  \
        MARKF;                                      \
        dyn->f.pending = SF_SET;                    \
        SET_DFOK();                                 \
    }
#endif

#ifndef SETFLAGS
#define SETFLAGS(A, B)                                                                                              \
    if (dyn->f.pending != SF_SET                                                                                    \
        && ((B) & SF_SUB)                                                                                           \
        && (dyn->insts[ninst].x64.gen_flags & (~(A))))                                                              \
        READFLAGS(((dyn->insts[ninst].x64.gen_flags & X_PEND) ? X_ALL : dyn->insts[ninst].x64.gen_flags) & (~(A))); \
    if (dyn->insts[ninst].x64.gen_flags) switch (B) {                                                               \
            case SF_SUBSET:                                                                                         \
            case SF_SET: dyn->f.pending = SF_SET; break;                                                            \
            case SF_PENDING: dyn->f.pending = SF_PENDING; break;                                                    \
            case SF_SUBSET_PENDING:                                                                                 \
            case SF_SET_PENDING:                                                                                    \
                dyn->f.pending = (dyn->insts[ninst].x64.gen_flags & X_PEND) ? SF_SET_PENDING : SF_SET;              \
                break;                                                                                              \
        }                                                                                                           \
    else                                                                                                            \
        dyn->f.pending = SF_SET
#endif
#ifndef JUMP
#define JUMP(A, C)
#endif
#ifndef BARRIER
#define BARRIER(A)
#endif
#ifndef SET_HASCALLRET
#define SET_HASCALLRET()
#endif
#ifndef DEFAULT
#define DEFAULT \
    *ok = -1;   \
    BARRIER(2)
#endif

#ifndef TABLE64
#define TABLE64(A, V)
#endif

#define ARCH_INIT()

#if STEP < 2
#define GETIP(A)
#define GETIP_(A)
#else
// put value in the Table64 even if not using it for now to avoid difference between Step2 and Step3. Needs to be optimized later...
#define GETIP(A)                                     \
    if (dyn->last_ip && ((A)-dyn->last_ip) < 2048) { \
        uint64_t _delta_ip = (A)-dyn->last_ip;       \
        dyn->last_ip += _delta_ip;                   \
        if (_delta_ip) {                             \
            ADDI_D(xRIP, xRIP, _delta_ip);           \
        }                                            \
    } else {                                         \
        dyn->last_ip = (A);                          \
        if (dyn->last_ip < 0xffffffff) {             \
            MOV64x(xRIP, dyn->last_ip);              \
        } else                                       \
            TABLE64(xRIP, dyn->last_ip);             \
    }
#define GETIP_(A)                                         \
    if (dyn->last_ip && ((A)-dyn->last_ip) < 2048) {      \
        int64_t _delta_ip = (A)-dyn->last_ip;             \
        if (_delta_ip) { ADDI_D(xRIP, xRIP, _delta_ip); } \
    } else {                                              \
        if ((A) < 0xffffffff) {                           \
            MOV64x(xRIP, (A));                            \
        } else                                            \
            TABLE64(xRIP, (A));                           \
    }
#endif
#define CLEARIP() dyn->last_ip = 0

#if STEP < 2
#define PASS2IF(A, B) if (A)
#elif STEP == 2
#define PASS2IF(A, B)                         \
    if (A) dyn->insts[ninst].pass2choice = B; \
    if (dyn->insts[ninst].pass2choice == B)
#else
#define PASS2IF(A, B) if (dyn->insts[ninst].pass2choice == B)
#endif

#define MODREG ((nextop & 0xC0) == 0xC0)

void la64_epilog(void);
void* la64_next(x64emu_t* emu, uintptr_t addr);

#ifndef STEPNAME
#define STEPNAME3(N, M) N##M
#define STEPNAME2(N, M) STEPNAME3(N, M)
#define STEPNAME(N)     STEPNAME2(N, STEP)
#endif

#define native_pass STEPNAME(native_pass)

#define dynarec64_00 STEPNAME(dynarec64_00)

#define geted          STEPNAME(geted)
#define geted32        STEPNAME(geted32)
#define jump_to_epilog STEPNAME(jump_to_epilog)
#define jump_to_next   STEPNAME(jump_to_next)
#define ret_to_epilog  STEPNAME(ret_to_epilog)
#define call_c         STEPNAME(call_c)
#define emit_test32    STEPNAME(emit_test32)
#define emit_add32     STEPNAME(emit_add32)
#define emit_add32c    STEPNAME(emit_add32c)
#define emit_add8      STEPNAME(emit_add8)
#define emit_add8c     STEPNAME(emit_add8c)
#define emit_sub32     STEPNAME(emit_sub32)
#define emit_sub32c    STEPNAME(emit_sub32c)
#define emit_sub8      STEPNAME(emit_sub8)
#define emit_sub8c     STEPNAME(emit_sub8c)

#define emit_pf STEPNAME(emit_pf)

#define fpu_pushcache       STEPNAME(fpu_pushcache)
#define fpu_popcache        STEPNAME(fpu_popcache)
#define fpu_reset_cache     STEPNAME(fpu_reset_cache)
#define fpu_propagate_stack STEPNAME(fpu_propagate_stack)
#define fpu_purgecache      STEPNAME(fpu_purgecache)
#define fpu_reflectcache    STEPNAME(fpu_reflectcache)
#define fpu_unreflectcache  STEPNAME(fpu_unreflectcache)

#define CacheTransform STEPNAME(CacheTransform)

/* setup r2 to address pointed by */
uintptr_t geted(dynarec_la64_t* dyn, uintptr_t addr, int ninst, uint8_t nextop, uint8_t* ed, uint8_t hint, uint8_t scratch, int64_t* fixaddress, rex_t rex, int* l, int i12, int delta);

/* setup r2 to address pointed by */
uintptr_t geted32(dynarec_la64_t* dyn, uintptr_t addr, int ninst, uint8_t nextop, uint8_t* ed, uint8_t hint, uint8_t scratch, int64_t* fixaddress, rex_t rex, int* l, int i12, int delta);

// generic x64 helper
void jump_to_epilog(dynarec_la64_t* dyn, uintptr_t ip, int reg, int ninst);
void jump_to_next(dynarec_la64_t* dyn, uintptr_t ip, int reg, int ninst, int is32bits);
void ret_to_epilog(dynarec_la64_t* dyn, int ninst, rex_t rex);
void call_c(dynarec_la64_t* dyn, int ninst, void* fnc, int reg, int ret, int saveflags, int save_reg);
void emit_test32(dynarec_la64_t* dyn, int ninst, rex_t rex, int s1, int s2, int s3, int s4, int s5);
void emit_add32(dynarec_la64_t* dyn, int ninst, rex_t rex, int s1, int s2, int s3, int s4, int s5);
void emit_add32c(dynarec_la64_t* dyn, int ninst, rex_t rex, int s1, int64_t c, int s2, int s3, int s4, int s5);
void emit_add8(dynarec_la64_t* dyn, int ninst, int s1, int s2, int s3, int s4);
void emit_add8c(dynarec_la64_t* dyn, int ninst, int s1, int32_t c, int s2, int s3, int s4);
void emit_sub32(dynarec_la64_t* dyn, int ninst, rex_t rex, int s1, int s2, int s3, int s4, int s5);
void emit_sub32c(dynarec_la64_t* dyn, int ninst, rex_t rex, int s1, int64_t c, int s2, int s3, int s4, int s5);
void emit_sub8(dynarec_la64_t* dyn, int ninst, int s1, int s2, int s3, int s4, int s5);
void emit_sub8c(dynarec_la64_t* dyn, int ninst, int s1, int32_t c, int s2, int s3, int s4, int s5);

void emit_pf(dynarec_la64_t* dyn, int ninst, int s1, int s3, int s4);

// common coproc helpers
// reset the cache with n
void fpu_reset_cache(dynarec_la64_t* dyn, int ninst, int reset_n);
// propagate stack state
void fpu_propagate_stack(dynarec_la64_t* dyn, int ninst);
// purge the FPU cache (needs 3 scratch registers)
void fpu_purgecache(dynarec_la64_t* dyn, int ninst, int next, int s1, int s2, int s3);
void fpu_reflectcache(dynarec_la64_t* dyn, int ninst, int s1, int s2, int s3);
void fpu_unreflectcache(dynarec_la64_t* dyn, int ninst, int s1, int s2, int s3);
void fpu_pushcache(dynarec_la64_t* dyn, int ninst, int s1, int not07);
void fpu_popcache(dynarec_la64_t* dyn, int ninst, int s1, int not07);

void CacheTransform(dynarec_la64_t* dyn, int ninst, int cacheupd, int s1, int s2, int s3);

#if STEP < 2
#define CHECK_CACHE() 0
#else
#define CHECK_CACHE() (cacheupd = CacheNeedsTransform(dyn, ninst))
#endif

uintptr_t dynarec64_00(dynarec_la64_t* dyn, uintptr_t addr, uintptr_t ip, int ninst, rex_t rex, int rep, int* ok, int* need_epilog);

#if STEP < 3
#define PASS3(A)
#else
#define PASS3(A) A
#endif

#if STEP < 3
#define MAYUSE(A) (void)A
#else
#define MAYUSE(A)
#endif

#define GOCOND(B, T1, T2)                                                                        \
    case B + 0x0:                                                                                \
        INST_NAME(T1 "O " T2);                                                                   \
        GO(ANDI(x1, xFlags, 1 << F_OF), EQZ, NEZ, X_OF, X64_JMP_JO);                             \
        break;                                                                                   \
    case B + 0x1:                                                                                \
        INST_NAME(T1 "NO " T2);                                                                  \
        GO(ANDI(x1, xFlags, 1 << F_OF), NEZ, EQZ, X_OF, X64_JMP_JNO);                            \
        break;                                                                                   \
    case B + 0x2:                                                                                \
        INST_NAME(T1 "C " T2);                                                                   \
        GO(ANDI(x1, xFlags, 1 << F_CF), EQZ, NEZ, X_CF, X64_JMP_JB);                             \
        break;                                                                                   \
    case B + 0x3:                                                                                \
        INST_NAME(T1 "NC " T2);                                                                  \
        GO(ANDI(x1, xFlags, 1 << F_CF), NEZ, EQZ, X_CF, X64_JMP_JNB);                            \
        break;                                                                                   \
    case B + 0x4:                                                                                \
        INST_NAME(T1 "Z " T2);                                                                   \
        GO(ANDI(x1, xFlags, 1 << F_ZF), EQZ, NEZ, X_ZF, X64_JMP_JE);                             \
        break;                                                                                   \
    case B + 0x5:                                                                                \
        INST_NAME(T1 "NZ " T2);                                                                  \
        GO(ANDI(x1, xFlags, 1 << F_ZF), NEZ, EQZ, X_ZF, X64_JMP_JNE);                            \
        break;                                                                                   \
    case B + 0x6:                                                                                \
        INST_NAME(T1 "BE " T2);                                                                  \
        GO(ANDI(x1, xFlags, (1 << F_CF) | (1 << F_ZF)), EQZ, NEZ, X_CF | X_ZF, X64_JMP_JBE);     \
        break;                                                                                   \
    case B + 0x7:                                                                                \
        INST_NAME(T1 "NBE " T2);                                                                 \
        GO(ANDI(x1, xFlags, (1 << F_CF) | (1 << F_ZF)), NEZ, EQZ, X_CF | X_ZF, X64_JMP_JA);      \
        break;                                                                                   \
    case B + 0x8:                                                                                \
        INST_NAME(T1 "S " T2);                                                                   \
        GO(ANDI(x1, xFlags, 1 << F_SF), EQZ, NEZ, X_SF, X64_JMP_JS);                             \
        break;                                                                                   \
    case B + 0x9:                                                                                \
        INST_NAME(T1 "NS " T2);                                                                  \
        GO(ANDI(x1, xFlags, 1 << F_SF), NEZ, EQZ, X_SF, X64_JMP_JNS);                            \
        break;                                                                                   \
    case B + 0xA:                                                                                \
        INST_NAME(T1 "P " T2);                                                                   \
        GO(ANDI(x1, xFlags, 1 << F_PF), EQZ, NEZ, X_PF, X64_JMP_JP);                             \
        break;                                                                                   \
    case B + 0xB:                                                                                \
        INST_NAME(T1 "NP " T2);                                                                  \
        GO(ANDI(x1, xFlags, 1 << F_PF), NEZ, EQZ, X_PF, X64_JMP_JNP);                            \
        break;                                                                                   \
    case B + 0xC:                                                                                \
        INST_NAME(T1 "L " T2);                                                                   \
        GO(SRLI_D(x1, xFlags, F_SF - F_OF);                                                      \
            XOR(x1, x1, xFlags);                                                                 \
            ANDI(x1, x1, 1 << F_OF), EQZ, NEZ, X_SF | X_OF, X64_JMP_JL);                         \
        break;                                                                                   \
    case B + 0xD:                                                                                \
        INST_NAME(T1 "GE " T2);                                                                  \
        GO(SRLI_D(x1, xFlags, F_SF - F_OF);                                                      \
            XOR(x1, x1, xFlags);                                                                 \
            ANDI(x1, x1, 1 << F_OF), NEZ, EQZ, X_SF | X_OF, X64_JMP_JGE);                        \
        break;                                                                                   \
    case B + 0xE:                                                                                \
        INST_NAME(T1 "LE " T2);                                                                  \
        GO(SRLI_D(x1, xFlags, F_SF - F_OF);                                                      \
            XOR(x1, x1, xFlags);                                                                 \
            ANDI(x1, x1, 1 << F_OF);                                                             \
            ANDI(x3, xFlags, 1 << F_ZF);                                                         \
            OR(x1, x1, x3);                                                                      \
            ANDI(x1, x1, (1 << F_OF) | (1 << F_ZF)), EQZ, NEZ, X_SF | X_OF | X_ZF, X64_JMP_JLE); \
        break;                                                                                   \
    case B + 0xF:                                                                                \
        INST_NAME(T1 "G " T2);                                                                   \
        GO(SRLI_D(x1, xFlags, F_SF - F_OF);                                                      \
            XOR(x1, x1, xFlags);                                                                 \
            ANDI(x1, x1, 1 << F_OF);                                                             \
            ANDI(x3, xFlags, 1 << F_ZF);                                                         \
            OR(x1, x1, x3);                                                                      \
            ANDI(x1, x1, (1 << F_OF) | (1 << F_ZF)), NEZ, EQZ, X_SF | X_OF | X_ZF, X64_JMP_JG);  \
        break

#define NOTEST(s1)                                       \
    if (box64_dynarec_test) {                            \
        ST_W(xZR, xEmu, offsetof(x64emu_t, test.test));  \
        ST_W(xZR, xEmu, offsetof(x64emu_t, test.clean)); \
    }

#define GOTEST(s1, s2)                                 \
    if (box64_dynarec_test) {                          \
        MOV32w(s2, 1);                                 \
        ST_W(s2, xEmu, offsetof(x64emu_t, test.test)); \
    }

#endif //__DYNAREC_LA64_HELPER_H__