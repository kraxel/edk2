;-----------------------------------------------------------------------------
; @file
; OVMF metadata for IGVM parameters
;
; Copyright (c) 2021 - 2024, AMD Inc. All rights reserved.<BR>
;
; SPDX-License-Identifier: BSD-2-Clause-Patent
;-----------------------------------------------------------------------------

BITS  64
ALIGN 16

; from igvm spec
%define IGVM_VHT_PARAMETER_AREA                          0x301
%define IGVM_VHT_MEMORY_MAP                              0x30C

%define MEMORY_MAP_OFFSET                                    0
%define MEMORY_MAP_ENTRIES                                   8
%define MEMORY_MAP_SIZE              (MEMORY_MAP_ENTRIES * 24)

IgvmParamStart:
_IgvmDescriptor:
  DB 'I','G','V','M'                            ; Signature
  DD IgvmParamEnd - IgvmParamStart              ; Length
  DD 1                                          ; Version
  DD (IgvmParamEnd - IgvmParamStart - 16) / 12  ; Number of sections

%if (IGVM_PARAM_SIZE > 0)

_IgvmParamArea:
  DD  IGVM_PARAM_START
  DD  IGVM_PARAM_SIZE
  DD  IGVM_VHT_PARAMETER_AREA
       
_IgvmMemoryMap:
  DD  MEMORY_MAP_OFFSET
  DD  MEMORY_MAP_SIZE
  DD  IGVM_VHT_MEMORY_MAP

%endif

IgvmParamEnd:
