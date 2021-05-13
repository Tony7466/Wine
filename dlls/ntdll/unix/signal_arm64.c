/*
 * ARM64 signal handling routines
 *
 * Copyright 2010-2013 André Hentschel
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

#ifdef __aarch64__

#include "config.h"
#include "wine/port.h"

#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#ifdef HAVE_SYSCALL_H
# include <syscall.h>
#else
# ifdef HAVE_SYS_SYSCALL_H
#  include <sys/syscall.h>
# endif
#endif
#ifdef HAVE_SYS_SIGNAL_H
# include <sys/signal.h>
#endif
#ifdef HAVE_SYS_UCONTEXT_H
# include <sys/ucontext.h>
#endif
#ifdef HAVE_LIBUNWIND
# define UNW_LOCAL_ONLY
# include <libunwind.h>
#endif

#define NONAMELESSUNION
#define NONAMELESSSTRUCT
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winnt.h"
#include "winternl.h"
#include "wine/exception.h"
#include "wine/asm.h"
#include "unix_private.h"
#include "wine/debug.h"

static pthread_key_t teb_key;

static const size_t teb_size = 0x2000;  /* we reserve two pages for the TEB */

struct arm64_thread_data
{
    void     *exit_frame;    /* exit frame pointer */
    CONTEXT  *context;       /* context to set with SIGUSR2 */
};

C_ASSERT( sizeof(struct arm64_thread_data) <= sizeof(((TEB *)0)->SystemReserved2) );
C_ASSERT( offsetof( TEB, SystemReserved2 ) + offsetof( struct arm64_thread_data, exit_frame ) == 0x300 );

static inline struct arm64_thread_data *arm64_thread_data(void)
{
    return (struct arm64_thread_data *)NtCurrentTeb()->SystemReserved2;
}


/***********************************************************************
 *           set_cpu_context
 *
 * Set the new CPU context.
 */
static void set_cpu_context( const CONTEXT *context )
{
    InterlockedExchangePointer( (void **)&arm64_thread_data()->context, (void *)context );
    raise( SIGUSR2 );
}


/***********************************************************************
 *           get_server_context_flags
 *
 * Convert CPU-specific flags to generic server flags
 */
static unsigned int get_server_context_flags( DWORD flags )
{
    unsigned int ret = 0;

    flags &= ~CONTEXT_ARM64;  /* get rid of CPU id */
    if (flags & CONTEXT_CONTROL) ret |= SERVER_CTX_CONTROL;
    if (flags & CONTEXT_INTEGER) ret |= SERVER_CTX_INTEGER;
    if (flags & CONTEXT_FLOATING_POINT) ret |= SERVER_CTX_FLOATING_POINT;
    if (flags & CONTEXT_DEBUG_REGISTERS) ret |= SERVER_CTX_DEBUG_REGISTERS;
    return ret;
}


/***********************************************************************
 *           copy_context
 *
 * Copy a register context according to the flags.
 */
static void copy_context( CONTEXT *to, const CONTEXT *from, DWORD flags )
{
    flags &= ~CONTEXT_ARM64;  /* get rid of CPU id */
    if (flags & CONTEXT_CONTROL)
    {
        to->u.s.Fp  = from->u.s.Fp;
        to->u.s.Lr  = from->u.s.Lr;
        to->Sp      = from->Sp;
        to->Pc      = from->Pc;
        to->Cpsr    = from->Cpsr;
    }
    if (flags & CONTEXT_INTEGER)
    {
        memcpy( to->u.X, from->u.X, sizeof(to->u.X) );
    }
    if (flags & CONTEXT_FLOATING_POINT)
    {
        memcpy( to->V, from->V, sizeof(to->V) );
        to->Fpcr = from->Fpcr;
        to->Fpsr = from->Fpsr;
    }
    if (flags & CONTEXT_DEBUG_REGISTERS)
    {
        memcpy( to->Bcr, from->Bcr, sizeof(to->Bcr) );
        memcpy( to->Bvr, from->Bvr, sizeof(to->Bvr) );
        memcpy( to->Wcr, from->Wcr, sizeof(to->Wcr) );
        memcpy( to->Wvr, from->Wvr, sizeof(to->Wvr) );
    }
}


/***********************************************************************
 *           context_to_server
 *
 * Convert a register context to the server format.
 */
NTSTATUS context_to_server( context_t *to, const CONTEXT *from )
{
    DWORD i, flags = from->ContextFlags & ~CONTEXT_ARM64;  /* get rid of CPU id */

    memset( to, 0, sizeof(*to) );
    to->cpu = CPU_ARM64;

    if (flags & CONTEXT_CONTROL)
    {
        to->flags |= SERVER_CTX_CONTROL;
        to->integer.arm64_regs.x[29] = from->u.s.Fp;
        to->integer.arm64_regs.x[30] = from->u.s.Lr;
        to->ctl.arm64_regs.sp     = from->Sp;
        to->ctl.arm64_regs.pc     = from->Pc;
        to->ctl.arm64_regs.pstate = from->Cpsr;
    }
    if (flags & CONTEXT_INTEGER)
    {
        to->flags |= SERVER_CTX_INTEGER;
        for (i = 0; i <= 28; i++) to->integer.arm64_regs.x[i] = from->u.X[i];
    }
    if (flags & CONTEXT_FLOATING_POINT)
    {
        to->flags |= SERVER_CTX_FLOATING_POINT;
        for (i = 0; i < 32; i++)
        {
            to->fp.arm64_regs.q[i].low = from->V[i].s.Low;
            to->fp.arm64_regs.q[i].high = from->V[i].s.High;
        }
        to->fp.arm64_regs.fpcr = from->Fpcr;
        to->fp.arm64_regs.fpsr = from->Fpsr;
    }
    if (flags & CONTEXT_DEBUG_REGISTERS)
    {
        to->flags |= SERVER_CTX_DEBUG_REGISTERS;
        for (i = 0; i < ARM64_MAX_BREAKPOINTS; i++) to->debug.arm64_regs.bcr[i] = from->Bcr[i];
        for (i = 0; i < ARM64_MAX_BREAKPOINTS; i++) to->debug.arm64_regs.bvr[i] = from->Bvr[i];
        for (i = 0; i < ARM64_MAX_WATCHPOINTS; i++) to->debug.arm64_regs.wcr[i] = from->Wcr[i];
        for (i = 0; i < ARM64_MAX_WATCHPOINTS; i++) to->debug.arm64_regs.wvr[i] = from->Wvr[i];
    }
    return STATUS_SUCCESS;
}


/***********************************************************************
 *           context_from_server
 *
 * Convert a register context from the server format.
 */
NTSTATUS context_from_server( CONTEXT *to, const context_t *from )
{
    DWORD i;

    if (from->cpu != CPU_ARM64) return STATUS_INVALID_PARAMETER;

    to->ContextFlags = CONTEXT_ARM64;
    if (from->flags & SERVER_CTX_CONTROL)
    {
        to->ContextFlags |= CONTEXT_CONTROL;
        to->u.s.Fp = from->integer.arm64_regs.x[29];
        to->u.s.Lr = from->integer.arm64_regs.x[30];
        to->Sp     = from->ctl.arm64_regs.sp;
        to->Pc     = from->ctl.arm64_regs.pc;
        to->Cpsr   = from->ctl.arm64_regs.pstate;
    }
    if (from->flags & SERVER_CTX_INTEGER)
    {
        to->ContextFlags |= CONTEXT_INTEGER;
        for (i = 0; i <= 28; i++) to->u.X[i] = from->integer.arm64_regs.x[i];
    }
    if (from->flags & SERVER_CTX_FLOATING_POINT)
    {
        to->ContextFlags |= CONTEXT_FLOATING_POINT;
        for (i = 0; i < 32; i++)
        {
            to->V[i].s.Low = from->fp.arm64_regs.q[i].low;
            to->V[i].s.High = from->fp.arm64_regs.q[i].high;
        }
        to->Fpcr = from->fp.arm64_regs.fpcr;
        to->Fpsr = from->fp.arm64_regs.fpsr;
    }
    if (from->flags & SERVER_CTX_DEBUG_REGISTERS)
    {
        to->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
        for (i = 0; i < ARM64_MAX_BREAKPOINTS; i++) to->Bcr[i] = from->debug.arm64_regs.bcr[i];
        for (i = 0; i < ARM64_MAX_BREAKPOINTS; i++) to->Bvr[i] = from->debug.arm64_regs.bvr[i];
        for (i = 0; i < ARM64_MAX_WATCHPOINTS; i++) to->Wcr[i] = from->debug.arm64_regs.wcr[i];
        for (i = 0; i < ARM64_MAX_WATCHPOINTS; i++) to->Wvr[i] = from->debug.arm64_regs.wvr[i];
    }
    return STATUS_SUCCESS;
}


/***********************************************************************
 *              NtSetContextThread  (NTDLL.@)
 *              ZwSetContextThread  (NTDLL.@)
 */
NTSTATUS WINAPI NtSetContextThread( HANDLE handle, const CONTEXT *context )
{
    NTSTATUS ret = STATUS_SUCCESS;
    BOOL self = (handle == GetCurrentThread());

    if (self && (context->ContextFlags & (CONTEXT_DEBUG_REGISTERS & ~CONTEXT_ARM64)))
        self = FALSE;

    if (!self)
    {
        context_t server_context;
        context_to_server( &server_context, context );
        ret = set_thread_context( handle, &server_context, &self );
    }
    if (self && ret == STATUS_SUCCESS) set_cpu_context( context );
    return ret;
}


/***********************************************************************
 *              NtGetContextThread  (NTDLL.@)
 *              ZwGetContextThread  (NTDLL.@)
 */
NTSTATUS WINAPI NtGetContextThread( HANDLE handle, CONTEXT *context )
{
    NTSTATUS ret;
    DWORD needed_flags = context->ContextFlags;
    BOOL self = (handle == GetCurrentThread());

    if (!self)
    {
        context_t server_context;
        unsigned int server_flags = get_server_context_flags( context->ContextFlags );

        if ((ret = get_thread_context( handle, &server_context, server_flags, &self ))) return ret;
        if ((ret = context_from_server( context, &server_context ))) return ret;
        needed_flags &= ~context->ContextFlags;
    }

    if (self && needed_flags)
    {
        CONTEXT ctx;
        RtlCaptureContext( &ctx );
        copy_context( context, &ctx, ctx.ContextFlags & needed_flags );
        context->ContextFlags |= ctx.ContextFlags & needed_flags;
    }
    return STATUS_SUCCESS;
}


/**********************************************************************
 *           get_thread_ldt_entry
 */
NTSTATUS CDECL get_thread_ldt_entry( HANDLE handle, void *data, ULONG len, ULONG *ret_len )
{
    return STATUS_NOT_IMPLEMENTED;
}


/******************************************************************************
 *           NtSetLdtEntries   (NTDLL.@)
 *           ZwSetLdtEntries   (NTDLL.@)
 */
NTSTATUS WINAPI NtSetLdtEntries( ULONG sel1, LDT_ENTRY entry1, ULONG sel2, LDT_ENTRY entry2 )
{
    return STATUS_NOT_IMPLEMENTED;
}


/**********************************************************************
 *             signal_init_threading
 */
void signal_init_threading(void)
{
    pthread_key_create( &teb_key, NULL );
}


/**********************************************************************
 *             signal_alloc_thread
 */
NTSTATUS signal_alloc_thread( TEB *teb )
{
    return STATUS_SUCCESS;
}


/**********************************************************************
 *             signal_free_thread
 */
void signal_free_thread( TEB *teb )
{
}


/**********************************************************************
 *		signal_init_thread
 */
void signal_init_thread( TEB *teb )
{
    stack_t ss;

    ss.ss_sp    = (char *)teb + teb_size;
    ss.ss_size  = signal_stack_size;
    ss.ss_flags = 0;
    if (sigaltstack( &ss, NULL ) == -1) perror( "sigaltstack" );

    /* Win64/ARM applications expect the TEB pointer to be in the x18 platform register. */
    __asm__ __volatile__( "mov x18, %0" : : "r" (teb) );

    pthread_setspecific( teb_key, teb );
}


/***********************************************************************
 *           init_thread_context
 */
static void init_thread_context( CONTEXT *context, LPTHREAD_START_ROUTINE entry, void *arg, void *relay )
{
    context->u.s.X0  = (DWORD64)entry;
    context->u.s.X1  = (DWORD64)arg;
    context->u.s.X18 = (DWORD64)NtCurrentTeb();
    context->Sp      = (DWORD64)NtCurrentTeb()->Tib.StackBase;
    context->Pc      = (DWORD64)relay;
}


/***********************************************************************
 *           attach_thread
 */
PCONTEXT DECLSPEC_HIDDEN attach_thread( LPTHREAD_START_ROUTINE entry, void *arg,
                                        BOOL suspend, void *relay )
{
    CONTEXT *ctx;

    if (suspend)
    {
        CONTEXT context = { CONTEXT_ALL };

        init_thread_context( &context, entry, arg, relay );
        wait_suspend( &context );
        ctx = (CONTEXT *)((ULONG_PTR)context.Sp & ~15) - 1;
        *ctx = context;
    }
    else
    {
        ctx = (CONTEXT *)NtCurrentTeb()->Tib.StackBase - 1;
        init_thread_context( ctx, entry, arg, relay );
    }
    ctx->ContextFlags = CONTEXT_FULL;
    LdrInitializeThunk( ctx, (void **)&ctx->u.s.X0, 0, 0 );
    return ctx;
}


/***********************************************************************
 *           signal_start_thread
 */
__ASM_GLOBAL_FUNC( signal_start_thread,
                   "stp x29, x30, [sp,#-16]!\n\t"
                   "mov x18, x4\n\t"             /* teb */
                   /* store exit frame */
                   "mov x29, sp\n\t"
                   "str x29, [x4, #0x300]\n\t"  /* arm64_thread_data()->exit_frame */
                   /* switch to thread stack */
                   "ldr x5, [x4, #8]\n\t"       /* teb->Tib.StackBase */
                   "sub sp, x5, #0x1000\n\t"
                   /* attach dlls */
                   "bl " __ASM_NAME("attach_thread") "\n\t"
                   "mov sp, x0\n\t"
                   /* clear the stack */
                   "and x0, x0, #~0xfff\n\t"  /* round down to page size */
                   "bl " __ASM_NAME("virtual_clear_thread_stack") "\n\t"
                   /* switch to the initial context */
                   "mov x0, sp\n\t"
                   "ldp q0, q1, [x0, #0x110]\n\t"      /* context->V[0,1] */
                   "ldp q2, q3, [x0, #0x130]\n\t"      /* context->V[2,3] */
                   "ldp q4, q5, [x0, #0x150]\n\t"      /* context->V[4,5] */
                   "ldp q6, q7, [x0, #0x170]\n\t"      /* context->V[6,7] */
                   "ldp q8, q9, [x0, #0x190]\n\t"      /* context->V[8,9] */
                   "ldp q10, q11, [x0, #0x1b0]\n\t"    /* context->V[10,11] */
                   "ldp q12, q13, [x0, #0x1d0]\n\t"    /* context->V[12,13] */
                   "ldp q14, q15, [x0, #0x1f0]\n\t"    /* context->V[14,15] */
                   "ldp q16, q17, [x0, #0x210]\n\t"    /* context->V[16,17] */
                   "ldp q18, q19, [x0, #0x230]\n\t"    /* context->V[18,19] */
                   "ldp q20, q21, [x0, #0x250]\n\t"    /* context->V[20,21] */
                   "ldp q22, q23, [x0, #0x270]\n\t"    /* context->V[22,23] */
                   "ldp q24, q25, [x0, #0x290]\n\t"    /* context->V[24,25] */
                   "ldp q26, q27, [x0, #0x2b0]\n\t"    /* context->V[26,27] */
                   "ldp q28, q29, [x0, #0x2d0]\n\t"    /* context->V[28,29] */
                   "ldp q30, q31, [x0, #0x2f0]\n\t"    /* context->V[30,31] */
                   "ldr w1, [x0, #0x310]\n\t"          /* context->Fpcr */
                   "msr fpcr, x1\n\t"
                   "ldr w1, [x0, #0x314]\n\t"          /* context->Fpsr */
                   "msr fpsr, x1\n\t"
                   "ldp x1, x2, [x0, #0x10]\n\t"       /* context->X1,2 */
                   "ldp x3, x4, [x0, #0x20]\n\t"       /* context->X3,4 */
                   "ldp x5, x6, [x0, #0x30]\n\t"       /* context->X5,6 */
                   "ldp x7, x8, [x0, #0x40]\n\t"       /* context->X7,8 */
                   "ldp x9, x10, [x0, #0x50]\n\t"      /* context->X9,10 */
                   "ldp x11, x12, [x0, #0x60]\n\t"     /* context->X11,12 */
                   "ldp x13, x14, [x0, #0x70]\n\t"     /* context->X13,14 */
                   "ldp x15, x16, [x0, #0x80]\n\t"     /* context->X15,16 */
                   "ldp x17, x18, [x0, #0x90]\n\t"     /* context->X17,18 */
                   "ldp x19, x20, [x0, #0xa0]\n\t"     /* context->X19,20 */
                   "ldp x21, x22, [x0, #0xb0]\n\t"     /* context->X21,22 */
                   "ldp x23, x24, [x0, #0xc0]\n\t"     /* context->X23,24 */
                   "ldp x25, x26, [x0, #0xd0]\n\t"     /* context->X25,26 */
                   "ldp x27, x28, [x0, #0xe0]\n\t"     /* context->X27,28 */
                   "ldp x29, x30, [x0, #0xf0]\n\t"     /* context->Fp,Lr */
                   "ldr x17, [x0, #0x100]\n\t"         /* context->Sp */
                   "mov sp, x17\n\t"
                   "ldr x17, [x0, #0x108]\n\t"         /* context->Pc */
                   "ldr x0, [x0, #0x8]\n\t"            /* context->X0 */
                   "br x17" )


extern void DECLSPEC_NORETURN call_thread_exit_func( int status, void (*func)(int), TEB *teb );
__ASM_GLOBAL_FUNC( call_thread_exit_func,
                   "stp x29, x30, [sp,#-16]!\n\t"
                   "ldr x3, [x2, #0x300]\n\t"  /* arm64_thread_data()->exit_frame */
                   "str xzr, [x2, #0x300]\n\t"
                   "cbz x3, 1f\n\t"
                   "mov sp, x3\n"
                   "1:\tldp x29, x30, [sp], #16\n\t"
                   "br x1" )

/***********************************************************************
 *           signal_exit_thread
 */
void signal_exit_thread( int status, void (*func)(int) )
{
    call_thread_exit_func( status, func, NtCurrentTeb() );
}


/**********************************************************************
 *           NtCurrentTeb   (NTDLL.@)
 */
TEB * WINAPI NtCurrentTeb(void)
{
    return pthread_getspecific( teb_key );
}

#endif  /* __aarch64__ */
