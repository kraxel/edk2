//! DXE Core Sample X64 Binary for the Open Virtual Machine Firmware (OVMF) platform.
//!
//! ## License
//!
//! Copyright (c) Microsoft Corporation.
//!
//! SPDX-License-Identifier: Apache-2.0
//!
#![cfg(all(target_os = "uefi"))]
#![no_std]
#![no_main]

use core::arch::asm;
use core::ffi::c_void;
use core::panic::PanicInfo;
use patina::debug::log::{Format, SerialLogger};
use patina::peripheral::serial::uart::UartQemuDebug;
use patina_dxe_core::*;
use patina_ffs_extractors::CompositeSectionExtractor;
use patina_stacktrace::StackTrace;

// PlatformPei maps ACPI PM registeres at 0x0600
const ACPI_PM_BASE: u16 = 0x600;
const ACPI_PM_CTRL: u16 = ACPI_PM_BASE + 0x04;

// Firmware logging console, at ioport 0x402 (by convention)
const QEMU_DEBUGCON: u16 = 0x402;

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    log::error!("{}", info);

    if let Err(err) = unsafe { StackTrace::dump() } {
        log::error!("StackTrace: {}", err);
    }

    // SAFETY: This triggers poweroff via ACPI PM registers
    unsafe {
        asm!("outw %ax, %dx",
             in("ax") 0x2000,
             in("dx") ACPI_PM_CTRL,
             options(att_syntax))
    }
    loop {}
}

static LOGGER: SerialLogger<UartQemuDebug> = SerialLogger::new(
    Format::Standard,
    &[
        ("allocations", log::LevelFilter::Off),
        ("efi_memory_map", log::LevelFilter::Off),
        ("gcd_measure", log::LevelFilter::Off),
        ("goblin", log::LevelFilter::Off),
    ],
    log::LevelFilter::Info,
    UartQemuDebug::new(QEMU_DEBUGCON),
);

struct Ovmf;
impl MemoryInfo for Ovmf {}
impl CpuInfo for Ovmf {}
impl ComponentInfo for Ovmf {}

impl PlatformInfo for Ovmf {
    const UEFI_SPEC_VERSION: patina::UefiSpecVersion = patina::UefiSpecVersion::V2_7;

    type CpuInfo = Self;
    type MemoryInfo = Self;
    type ComponentInfo = Self;
    type Extractor = CompositeSectionExtractor;
}

static CORE: Core<Ovmf> = Core::new(CompositeSectionExtractor::new());

#[cfg_attr(target_os = "uefi", unsafe(export_name = "efi_main"))]
/// SAFETY: We must take on faith that the physical_hob_list pointer is valid.
pub unsafe extern "efiapi" fn _start(physical_hob_list: *const c_void) -> ! {
    LOGGER.init();
    log::set_logger(&LOGGER)
        .map(|()| log::set_max_level(log::LevelFilter::Trace))
        .unwrap();

    log::info!("Patina DXE Core Binary for OVMF Platform");
    CORE.entry_point(physical_hob_list)
}
