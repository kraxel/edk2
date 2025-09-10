/** @file
  custom page fault handler to fixup nx faults

  Copyright (C) 2025, Red Hat, Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/BaseLib.h>
#include <Library/CpuLib.h>
#include <Library/DebugLib.h>
#include <Library/QemuFwCfgSimpleParserLib.h>
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
 * X64 page table walker, find level3 page table directory.
 */
UINT64 *
EFIAPI
PageFaultGetPd3 (
  UINT64  Page
  )
{
  UINT64    Idx;
  UINT64    Pde;
  UINT64    *Pd3;
  IA32_CR4  Cr4;

  Pde       = AsmReadCr3 ();
  Cr4.UintN = AsmReadCr4 ();
  DEBUG ((DEBUG_VERBOSE, "%a: cr3 0x%lx\n", __func__, Pde));

  if (Cr4.Bits.LA57) {
    Idx = Page >> (12 + 4 * 9) & 0x1ff;
    Pde = *((UINT64 *)(Pde & ~0xfff) + Idx);
    DEBUG ((DEBUG_VERBOSE, "%a:  pd5 0x%lx\n", __func__, Pde));
    if (!(Pde & 1)) {
      return NULL; // not present
    }
  } else {
    DEBUG ((DEBUG_VERBOSE, "%a:  no pd5\n", __func__));
  }

  Idx = Page >> (12 + 3 * 9) & 0x1ff;
  Pde = *((UINT64 *)(Pde & ~0xfff) + Idx);
  DEBUG ((DEBUG_VERBOSE, "%a:   pd4 0x%lx\n", __func__, Pde));
  if (!(Pde & 1)) {
    return NULL; // not present
  }

  Idx = Page >> (12 + 2 * 9) & 0x1ff;
  Pd3 = (UINT64 *)(Pde & ~0xfff) + Idx;
  return Pd3;
}

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
  UINT64  Idx;
  UINT64  Pde;
  UINT64  *Pd3;
  UINT64  *Pd2;
  UINT64  *Pte;

  Pd3 = PageFaultGetPd3 (Page);
  Pde = *Pd3;
  DEBUG ((DEBUG_VERBOSE, "%a:    pd3 0x%lx (at %p)\n", __func__, Pde, Pd3));
  if (!(Pde & 1)) {
    return NULL; // not present
  }

  if (Pde & 0x80) {
    DEBUG ((DEBUG_VERBOSE, "%a:     1G page\n", __func__));
    return Pd3; // 1G page
  }

  Idx = Page >> (12 + 1 * 9) & 0x1ff;
  Pd2 = (UINT64 *)(Pde & ~0xfff) + Idx;
  Pde = *Pd2;
  DEBUG ((DEBUG_VERBOSE, "%a:     pd2 0x%lx (at %p)\n", __func__, Pde, Pd2));
  if (!(Pde & 1)) {
    return NULL; // not present
  }

  if (Pde & 0x80) {
    DEBUG ((DEBUG_VERBOSE, "%a:      2M page\n", __func__));
    return Pd2; // 2M page
  }

  Idx = Page >> (12 + 0 * 9) & 0x1ff;
  Pte = (UINT64 *)(Pde & ~0xfff) + Idx;
  DEBUG ((DEBUG_VERBOSE, "%a:      pte 0x%lx (at %p)\n", __func__, *Pte, Pte));

  return Pte;
}

/*
 * Fix page tables, clear NX, set RW, using 1G pages, lowest 512G.
 *
 * Background:
 *
 * Sometimes NX/RW faults happen with the kernel still running on the EFI page
 * tables, but after the kernel installed its own page fault handler.
 *
 * Sometimes the kernel handler fails to deal with the faults -> BOOM.
 *
 * So lets tweak the EFI page table to allow everything if our heuristics
 * indicate this is a good idea.
 */
VOID
EFIAPI
PageFaultFixMap (
  CHAR8 *Reason
  )
{
  UINT64  Idx;
  UINT64  *Pd3;

  if (PcdGet64 (PcdConfidentialComputingGuestAttr) != 0) {
    /* switching to 1G pages will break things */
    DEBUG ((DEBUG_INFO, "%a: skip global RW+NX fixup in CVM\n", __func__));
    return;
  }

  DEBUG ((DEBUG_INFO, "%a: global RW+NX fixup (%a)\n", __func__, Reason));

  Pd3 = PageFaultGetPd3 (0);
  DEBUG ((DEBUG_VERBOSE, "%a:    pd3 at 0x%lx [global RW+NX fixup]\n", __func__, Pd3));

  for (Idx = 0; Idx < 512; Idx++) {
    Pd3[Idx] = (0x40000000 * Idx) | 0x83;  // use 1G page
    if (!(Pd3[Idx] & 1)) {
      break;
    }
  }

  CpuFlushTlb ();
}

/*
 * Page fault handler which fixes up NX + WR faults by flipping the PTE bits.
 * This allows guest OSes which are not NX clean boot.  The fixups needed are
 * counted for later reporting.
 */
VOID
EFIAPI
PageFaultHandler (
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
    "%a: CR2: %016lx - RIP: %016lx - ID:%x WR:%x P:%x [0x%x]\n",
    __func__,
    SystemContext.SystemContextX64->Cr2,
    SystemContext.SystemContextX64->Rip,
    (ExceptionData & BIT4) != 0,
    (ExceptionData & BIT1) != 0,
    (ExceptionData & BIT0) != 0,
    ExceptionData
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
        DEBUG ((
          DEBUG_INFO,
          "%a: clearing NX for page 0x%lx%a\n",
          __func__,
          Page,
          (*Pte & 0x80) ? " [large pte]" : ""
          ));
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
        DEBUG ((
          DEBUG_INFO,
          "%a: setting RW for page 0x%lx%a\n",
          __func__,
          Page,
          (*Pte & 0x80) ? " [large pte]" : ""
          ));
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
  STATIC EFI_GUID  SHIM_LOCK_GUID = {
    0x605dab50, 0xe046, 0x4300, { 0xab, 0xb6, 0x3d, 0xd8, 0x10, 0xdd, 0x8b, 0x23 }
  };
  STATIC EFI_GUID  SHIM_IMAGE_LOADER_GUID = {
    0x1f492041, 0xfadb, 0x4e59, { 0x9e, 0x57, 0x7c, 0xaf, 0xe7, 0x3a, 0x55, 0xab }
  };
  VOID             *ShimLock   = NULL;
  VOID             *ShimLoader = NULL;
  BOOLEAN          MessageWait = FALSE;
  BOOLEAN          HaveOldShim;

  gBS->LocateProtocol (&SHIM_LOCK_GUID, NULL, &ShimLock);
  gBS->LocateProtocol (&SHIM_IMAGE_LOADER_GUID, NULL, &ShimLoader);
  HaveOldShim = (ShimLock != NULL) && (ShimLoader == NULL);

  DEBUG ((
      DEBUG_INFO,
      "%a: shim protocols: lock=%a, loader=%a -> old-shim=%a\n",
      __func__,
      (ShimLock != NULL) ? "yes" : "no",
      (ShimLoader != NULL) ? "yes" : "no",
      (HaveOldShim) ? "yes" : "no"
      ));
  DEBUG ((DEBUG_INFO, "%a: fixups: %d NX, %d RW\n", __func__, mFixupNX, mFixupRW));

  if (mFixupNX || mFixupRW) {
    /* we had to fixup NX or RW faults -> broken behavior -> report it */
    AsciiPrint (
      "%a: Page fault fixups needed (NX: %d, RW: %d).\n"
      "%a: The guest OS boot chain is not NX clean.\n",
      __func__,
      mFixupNX,
      mFixupRW,
      __func__
      );
    MessageWait = TRUE;
  }

  if (mFixupNX) {
    /* we had to fixup NX faults -> apply global fixup as precaution + report it */
    AsciiPrint (
      "%a: Applying global page table fixup (saw NX faults).\n",
      __func__
      );
    PageFaultFixMap ("nx-fault");

  } else if (HaveOldShim) {
    /* we detected shim older than v16 -> apply global fixup as precaution + report it */
    AsciiPrint (
      "%a: Applying global page table fixup (shim is older than v16).\n",
      __func__
      );
    PageFaultFixMap ("old-shim");
  }

  if (MessageWait) {
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
  BOOLEAN     StrictNX;

  if (FixedPcdGet64 (PcdDxeNxMemoryProtectionPolicy) == 0) {
    DEBUG ((DEBUG_INFO, "%a: no NX protection in this build.\n", __func__));
    return;
  }

  StrictNX = FALSE;
  Status   = QemuFwCfgParseBool (
               "opt/org.tianocore/StrictNX",
               &StrictNX
               );
  if (StrictNX) {
    DEBUG ((DEBUG_INFO, "%a: StrictNX enabled\n", __func__));
    return;
  }

  DEBUG ((DEBUG_INFO, "%a: StrictNX disabled - installing page fault handler\n", __func__));

  gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&mCpu);
  Status = mCpu->RegisterInterruptHandler (mCpu, EXCEPT_IA32_PAGE_FAULT, PageFaultHandler);
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
