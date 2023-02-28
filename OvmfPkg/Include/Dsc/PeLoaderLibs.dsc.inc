[LibraryClasses]
  #
  # old (insecure) PE loader libs
  #
  PeCoffLib|MdePkg/Library/BasePeCoffLib/BasePeCoffLib.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
!if $(SOURCE_DEBUG_ENABLE) == TRUE
  PeCoffExtraActionLib|SourceLevelDebugPkg/Library/PeCoffExtraActionLibDebug/PeCoffExtraActionLibDebug.inf
!else
  PeCoffExtraActionLib|MdePkg/Library/BasePeCoffExtraActionLibNull/BasePeCoffExtraActionLibNull.inf
!endif

  #
  # new (secure) PE loader libs
  #
  BaseOverflowLib|MdePkg/Library/BaseOverflowLib/BaseOverflowLib.inf
  PeCoffLib2|MdePkg/Library/BasePeCoffLib2/BasePeCoffLib2.inf
  UefiImageLib|MdePkg/Library/BaseUefiImageLib/BaseUefiImageLibPeCoff.inf
!if $(SOURCE_DEBUG_ENABLE) == TRUE
  UefiImageExtraActionLib|SourceLevelDebugPkg/Library/UefiImageExtraActionLibDebug/UefiImageExtraActionLibDebug.inf
!else
  UefiImageExtraActionLib|MdePkg/Library/BaseUefiImageExtraActionLibNull/BaseUefiImageExtraActionLibNull.inf
!endif
