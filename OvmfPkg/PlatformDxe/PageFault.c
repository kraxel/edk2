/** @file
  custom page fault handler to fixup nx faults

  Copyright (C) 2025, Red Hat, Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/CpuLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/Cpu.h>
#include <Protocol/MemoryAttribute.h>

#include "PlatformConfig.h"

/* ia32 ovmf does not have paging enabled */
#if defined (MDE_CPU_X64)

STATIC EFI_CPU_ARCH_PROTOCOL  *mCpu;
STATIC EFI_EVENT              mExitBoot;
STATIC UINTN                  mFixupNX;
STATIC UINTN                  mFixupRW;

/*
 * X64 page table walker, returns a pointer to the page table entry.
 * For large pages a pointer to the large PTE is returned.
 */
UINT64 *
EFIAPI
PageFaultGetPte (
  UINT64  Page
  )
{
  UINT64    Idx;
  UINT64    Pde;
  UINT64    *Pd3;
  UINT64    *Pd2;
  UINT64    *Pte;
  IA32_CR4  Cr4;

  Pde       = AsmReadCr3 ();
  Cr4.UintN = AsmReadCr4 ();
  DEBUG ((DEBUG_INFO, "%a: cr3 0x%lx\n", __func__, Pde));

  if (Cr4.Bits.LA57) {
    Idx = Page >> (12 + 4 * 9) & 0x1ff;
    Pde = *((UINT64 *)(Pde & ~0xfff) + Idx);
    DEBUG ((DEBUG_INFO, "%a:  pd5 0x%lx\n", __func__, Pde));
    if (!(Pde & 1)) {
      return NULL; // not present
    }
  } else {
    DEBUG ((DEBUG_INFO, "%a:  no pd5\n", __func__));
  }

  Idx = Page >> (12 + 3 * 9) & 0x1ff;
  Pde = *((UINT64 *)(Pde & ~0xfff) + Idx);
  DEBUG ((DEBUG_INFO, "%a:   pd4 0x%lx\n", __func__, Pde));
  if (!(Pde & 1)) {
    return NULL; // not present
  }

  Idx = Page >> (12 + 2 * 9) & 0x1ff;
  Pd3 = (UINT64 *)(Pde & ~0xfff) + Idx;
  Pde = *Pd3;
  DEBUG ((DEBUG_INFO, "%a:    pd3 0x%lx (at %p)\n", __func__, Pde, Pd3));
  if (!(Pde & 1)) {
    return NULL; // not present
  }

  if (Pde & 0x80) {
    DEBUG ((DEBUG_INFO, "%a:     1G page\n", __func__));
    return Pd3; // 1G page
  }

  Idx = Page >> (12 + 1 * 9) & 0x1ff;
  Pd2 = (UINT64 *)(Pde & ~0xfff) + Idx;
  Pde = *Pd2;
  DEBUG ((DEBUG_INFO, "%a:     pd2 0x%lx (at %p)\n", __func__, Pde, Pd2));
  if (!(Pde & 1)) {
    return NULL; // not present
  }

  if (Pde & 0x80) {
    DEBUG ((DEBUG_INFO, "%a:      2M page\n", __func__));
    return Pd2;
  }

  Idx = Page >> (12 + 0 * 9) & 0x1ff;
  Pte = (UINT64 *)(Pde & ~0xfff) + Idx;
  DEBUG ((DEBUG_INFO, "%a:      pte 0x%lx (at %p)\n", __func__, *Pte, Pte));

  return Pte;
}

/*
 * Page fault handler which fixes up NX + WR faults by flipping the PTE bits.
 * This allows guest OSes which are not NX clean boot.  The fixups needed are
 * counted for later reporting.
 */
VOID
EFIAPI
PageFaultExceptionHandler (
  IN EFI_EXCEPTION_TYPE  ExceptionType,
  IN EFI_SYSTEM_CONTEXT  SystemContext
  )
{
  STATIC BOOLEAN  Running = FALSE;
  UINT64          Page, *Pte;
  UINT64          ExceptionData;

  Page          = SystemContext.SystemContextX64->Cr2 & ~(EFI_PAGE_SIZE-1);
  ExceptionData = SystemContext.SystemContextX64->ExceptionData;

  DEBUG ((
    DEBUG_INFO,
    "%a: CR2: %016lx - RIP: %016lx - ID:%x WR:%x P:%x\n",
    __func__,
    SystemContext.SystemContextX64->Cr2,
    SystemContext.SystemContextX64->Rip,
    (ExceptionData & BIT4) != 0,
    (ExceptionData & BIT1) != 0,
    (ExceptionData & BIT0) != 0
    ));

  if (Running) {
    DEBUG ((DEBUG_INFO, "%a: nested page fault\n", __func__));
    goto fatal;
  }

  Running = TRUE;

  if (ExceptionData & BIT0) {
    /* page present */

    if (ExceptionData & BIT4) {
      /* instruction decode (-> NX fault) */
      Pte = PageFaultGetPte (Page);
      if (Pte && (*Pte & BIT63)) {
        DEBUG ((DEBUG_INFO, "%a: clearing NX for page 0x%lx\n", __func__, Page));
        *Pte &= ~BIT63;
        CpuFlushTlb ();
        mFixupNX++;
        Running = FALSE;
        return;
      }
    }

    if (ExceptionData & BIT1) {
      /* write access */
      Pte = PageFaultGetPte (Page);
      if (Pte && (!(*Pte & BIT1))) {
        DEBUG ((DEBUG_INFO, "%a: setting RW for page 0x%lx\n", __func__, Page));
        *Pte |= BIT1;
        CpuFlushTlb ();
        mFixupRW++;
        Running = FALSE;
        return;
      }
    }
  }

  if (!(ExceptionData & BIT0)) {
    /* page not present */
    if (Page == 0) {
      DEBUG ((DEBUG_INFO, "%a: NULL pointer dereference\n", __func__));
    }
  }

fatal:
  DEBUG ((DEBUG_INFO, "%a: fatal: can't handle exception -> HALT\n", __func__));
  CpuDeadLoop ();
}

/*
 * In case any fixups have been applies report them to the console at
 * ExitBootService time.
 */
STATIC
VOID
EFIAPI
PageFaultExitBoot (
  IN  EFI_EVENT  Event,
  IN  VOID       *Context
  )
{
  DEBUG ((DEBUG_INFO, "%a: fixups: %d NX, %d RW\n", __func__, mFixupNX, mFixupRW));

  if (mFixupNX || mFixupRW) {
    AsciiPrint (
      "%a: Page fault fixups needed (NX: %d, RW: %d).\n"
      "%a: The guest OS boot chain is not NX clean.\n",
      __func__,
      mFixupNX,
      mFixupRW,
      __func__
      );
    gBS->Stall (3000000);
  }
}

#endif

VOID
EFIAPI
PageFaultInit (
  VOID
  )
{
 #if defined (MDE_CPU_X64)
  EFI_STATUS  Status;

  if (FixedPcdGet64 (PcdDxeNxMemoryProtectionPolicy) == 0) {
    DEBUG ((DEBUG_INFO, "%a: no NX protection in this build.\n", __func__));
    return;
  }

  gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&mCpu);
  Status = mCpu->RegisterInterruptHandler (mCpu, EXCEPT_IA32_PAGE_FAULT, PageFaultExceptionHandler);
  DEBUG ((DEBUG_INFO, "%a: mCpu->RegisterInterruptHandler: %r\n", __func__, Status));
  if (Status != EFI_SUCCESS) {
    return;
  }

  Status = gBS->CreateEvent (
                  EVT_SIGNAL_EXIT_BOOT_SERVICES,
                  TPL_CALLBACK,
                  &PageFaultExitBoot,
                  NULL,
                  &mExitBoot
                  );
  DEBUG ((DEBUG_INFO, "%a: gBS->CreateEvent: %r\n", __func__, Status));
 #else
  DEBUG ((DEBUG_INFO, "%a: not supported on this architecture.\n", __func__));
 #endif
}
