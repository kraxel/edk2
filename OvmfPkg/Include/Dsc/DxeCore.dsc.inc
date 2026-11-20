!if $(PATINA_DXE_CORE) == FALSE
  MdeModulePkg/Core/Dxe/DxeMain.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/LzmaCustomDecompressLib/LzmaCustomDecompressLib.inf
      DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  }

  UefiCpuPkg/CpuDxe/CpuDxe.inf {
    <LibraryClasses>
      #
      # Directly use DxeMpInitLib. It depends on DxeMpInitLibMpDepLib which
      # checks the Protocol of gEfiMpInitLibMpDepProtocolGuid.
      #
      MpInitLib|UefiCpuPkg/Library/MpInitLib/DxeMpInitLib.inf
      NULL|OvmfPkg/Library/MpInitLibDepLib/DxeMpInitLibMpDepLib.inf
  }
  UefiCpuPkg/CpuDxe/CpuDxe.inf {
    <Defines>
      FILE_GUID = $(UP_CPU_DXE_GUID)

    <LibraryClasses>
      #
      # Directly use MpInitLibUp. It depends on DxeMpInitLibUpDepLib which
      # checks the Protocol of gEfiMpInitLibUpDepProtocolGuid.
      #
      MpInitLib|UefiCpuPkg/Library/MpInitLibUp/MpInitLibUp.inf
      NULL|OvmfPkg/Library/MpInitLibDepLib/DxeMpInitLibUpDepLib.inf
  }
!endif
