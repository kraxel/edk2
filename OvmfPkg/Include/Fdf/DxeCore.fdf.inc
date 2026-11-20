!if $(PATINA_DXE_CORE) == TRUE

INF  OvmfPkg/Patina/DxeCore.inf

!else

INF  MdeModulePkg/Core/Dxe/DxeMain.inf

INF  UefiCpuPkg/CpuDxe/CpuDxe.inf
INF  FILE_GUID = $(UP_CPU_DXE_GUID) UefiCpuPkg/CpuDxe/CpuDxe.inf

!endif
