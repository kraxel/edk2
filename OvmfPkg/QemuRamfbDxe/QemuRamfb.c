#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FrameBufferBltLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/QemuFwCfgLib.h>

#include <Guid/QemuRamfb.h>

#define RAMFB_FORMAT  0x34325258 /* DRM_FORMAT_XRGB8888 */
#define RAMFB_BPP     4

EFI_GUID gQemuRamfbGuid = QEMU_RAMFB_GUID;

typedef struct RAMFB_CONFIG {
  UINT64 Address;
  UINT32 FourCC;
  UINT32 Flags;
  UINT32 Width;
  UINT32 Height;
  UINT32 Stride;
} RAMFB_CONFIG;

EFI_HANDLE                    RamfbHandle;
EFI_HANDLE                    GopHandle;
FRAME_BUFFER_CONFIGURE        *QemuRamfbFrameBufferBltConfigure;
UINTN                         QemuRamfbFrameBufferBltConfigureSize;

EFI_GRAPHICS_OUTPUT_MODE_INFORMATION QemuRamfbModeInfo[] = {
  {
    .HorizontalResolution  = 640,
    .VerticalResolution    = 480,
  },{
    .HorizontalResolution  = 800,
    .VerticalResolution    = 600,
  },{
    .HorizontalResolution  = 1024,
    .VerticalResolution    = 768,
  }
};
#define QemuRamfbModeCount (sizeof(QemuRamfbModeInfo)/sizeof(QemuRamfbModeInfo[0]))

EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE QemuRamfbMode = {
  .MaxMode               = QemuRamfbModeCount,
  .Mode                  = 0,
  .Info                  = QemuRamfbModeInfo,
  .SizeOfInfo            = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
};

EFI_STATUS
EFIAPI
QemuRamfbGraphicsOutputQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo;

  if (Info == NULL || SizeOfInfo == NULL || ModeNumber > QemuRamfbMode.MaxMode) {
    return EFI_INVALID_PARAMETER;
  }
  ModeInfo = &QemuRamfbModeInfo[ModeNumber];

  *Info = AllocateCopyPool (sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
                            ModeInfo);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  *SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
QemuRamfbGraphicsOutputSetMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  IN  UINT32                       ModeNumber
  )
{
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo;
  RAMFB_CONFIG                          Config;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL         Black;
  RETURN_STATUS                         Ret;
  FIRMWARE_CONFIG_ITEM                  Item;
  UINTN                                 Size;

  if (ModeNumber > QemuRamfbMode.MaxMode) {
    return EFI_UNSUPPORTED;
  }
  ModeInfo = &QemuRamfbModeInfo[ModeNumber];

  DEBUG ((EFI_D_INFO, "Ramfb: SetMode %d (%dx%d)\n", ModeNumber,
          ModeInfo->HorizontalResolution,
          ModeInfo->VerticalResolution));

  QemuRamfbMode.Mode = ModeNumber;
  QemuRamfbMode.Info = ModeInfo;

  Config.Address = SwapBytes64( QemuRamfbMode.FrameBufferBase );
  Config.FourCC  = SwapBytes32( RAMFB_FORMAT );
  Config.Flags   = SwapBytes32( 0 );
  Config.Width   = SwapBytes32( ModeInfo->HorizontalResolution );
  Config.Height  = SwapBytes32( ModeInfo->VerticalResolution );
  Config.Stride  = SwapBytes32( ModeInfo->HorizontalResolution * RAMFB_BPP );

  QemuFwCfgFindFile("etc/ramfb", &Item, &Size);
  QemuFwCfgSelectItem(Item);
  QemuFwCfgWriteBytes(sizeof(Config), &Config);

  Ret = FrameBufferBltConfigure (
    (VOID*)(UINTN)QemuRamfbMode.FrameBufferBase,
    ModeInfo,
    QemuRamfbFrameBufferBltConfigure,
    &QemuRamfbFrameBufferBltConfigureSize);

  if (Ret == RETURN_BUFFER_TOO_SMALL) {
    if (QemuRamfbFrameBufferBltConfigure != NULL) {
      FreePool(QemuRamfbFrameBufferBltConfigure);
    }
    QemuRamfbFrameBufferBltConfigure =
      AllocatePool(QemuRamfbFrameBufferBltConfigureSize);

    Ret = FrameBufferBltConfigure (
      (VOID*)(UINTN)QemuRamfbMode.FrameBufferBase,
      ModeInfo,
      QemuRamfbFrameBufferBltConfigure,
      &QemuRamfbFrameBufferBltConfigureSize);
  }

  /* clear screen */
  ZeroMem (&Black, sizeof (Black));
  Ret = FrameBufferBlt (
    QemuRamfbFrameBufferBltConfigure,
    &Black,
    EfiBltVideoFill,
    0, 0,
    0, 0,
    ModeInfo->HorizontalResolution,
    ModeInfo->VerticalResolution,
    0
    );

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
QemuRamfbGraphicsOutputBlt (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  EFI_GRAPHICS_OUTPUT_BLT_PIXEL         *BltBuffer, OPTIONAL
  IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION     BltOperation,
  IN  UINTN                                 SourceX,
  IN  UINTN                                 SourceY,
  IN  UINTN                                 DestinationX,
  IN  UINTN                                 DestinationY,
  IN  UINTN                                 Width,
  IN  UINTN                                 Height,
  IN  UINTN                                 Delta
  )
{
  return FrameBufferBlt (
    QemuRamfbFrameBufferBltConfigure,
    BltBuffer,
    BltOperation,
    SourceX,
    SourceY,
    DestinationX,
    DestinationY,
    Width,
    Height,
    Delta);
}

EFI_GRAPHICS_OUTPUT_PROTOCOL QemuRamfbGraphicsOutput = {
  .QueryMode        = QemuRamfbGraphicsOutputQueryMode,
  .SetMode          = QemuRamfbGraphicsOutputSetMode,
  .Blt              = QemuRamfbGraphicsOutputBlt,
  .Mode             = &QemuRamfbMode,
};

EFI_STATUS
EFIAPI
InitializeQemuRamfb (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *RamfbDevicePath;
  EFI_DEVICE_PATH_PROTOCOL  *GopDevicePath;
  VOID                      *DevicePath;
  VENDOR_DEVICE_PATH        VendorDeviceNode;
  ACPI_ADR_DEVICE_PATH      AcpiDeviceNode;
  EFI_STATUS                Status;
  RETURN_STATUS             Ret;
  FIRMWARE_CONFIG_ITEM      Item;
  EFI_PHYSICAL_ADDRESS      FbBase;
  UINTN                     Size, FbSize, MaxFbSize, Pages, Index;

  DEBUG ((EFI_D_INFO, "InitializeQemuRamfb\n"));

  if (!QemuFwCfgIsAvailable()) {
    DEBUG ((EFI_D_INFO, "Ramfb: no FwCfg\n"));
    return EFI_NOT_FOUND;
  }

  Ret = QemuFwCfgFindFile("etc/ramfb", &Item, &Size);
  if (Ret != RETURN_SUCCESS) {
    DEBUG ((EFI_D_INFO, "Ramfb: no etc/ramfb in FwCfg\n"));
    return EFI_NOT_FOUND;
  }

  MaxFbSize = 0;
  for (Index = 0; Index < QemuRamfbModeCount; Index++) {
    QemuRamfbModeInfo[Index].PixelsPerScanLine =
      QemuRamfbModeInfo[Index].HorizontalResolution;
    QemuRamfbModeInfo[Index].PixelFormat =
      PixelBlueGreenRedReserved8BitPerColor,
    FbSize = RAMFB_BPP *
      QemuRamfbModeInfo[Index].HorizontalResolution *
      QemuRamfbModeInfo[Index].VerticalResolution;
    if (MaxFbSize < FbSize)
      MaxFbSize = FbSize;
    DEBUG ((EFI_D_INFO, "Ramfb: Mode %d: %dx%d, %d kB\n", Index,
            QemuRamfbModeInfo[Index].HorizontalResolution,
            QemuRamfbModeInfo[Index].VerticalResolution,
            FbSize / 1024));
  }

  Pages = EFI_SIZE_TO_PAGES(MaxFbSize);
  MaxFbSize = EFI_PAGES_TO_SIZE(Pages);
  FbBase = (EFI_PHYSICAL_ADDRESS)(UINTN)AllocateRuntimePages(Pages);
  if (!FbBase) {
    DEBUG ((EFI_D_INFO, "Ramfb: memory allocation failed\n"));
    return EFI_OUT_OF_RESOURCES;
  }
  DEBUG ((EFI_D_INFO, "Ramfb: Framebuffer at 0x%lx, %d kB, %d pages\n",
          FbBase, MaxFbSize / 1024, Pages));
  QemuRamfbMode.FrameBufferSize = MaxFbSize;
  QemuRamfbMode.FrameBufferBase = FbBase;

  /* 800 x 600 */
  QemuRamfbGraphicsOutputSetMode (&QemuRamfbGraphicsOutput, 1);

  /* ramfb vendor devpath */
  ZeroMem (&VendorDeviceNode, sizeof (VENDOR_DEVICE_PATH));
  VendorDeviceNode.Header.Type = HARDWARE_DEVICE_PATH;
  VendorDeviceNode.Header.SubType = HW_VENDOR_DP;
  VendorDeviceNode.Guid = gQemuRamfbGuid;
  SetDevicePathNodeLength (&VendorDeviceNode.Header, sizeof (VENDOR_DEVICE_PATH));

  RamfbDevicePath = AppendDevicePathNode (
    NULL,
    (EFI_DEVICE_PATH_PROTOCOL *) &VendorDeviceNode);

  Status = gBS->InstallMultipleProtocolInterfaces (
    &RamfbHandle,
    &gEfiDevicePathProtocolGuid, RamfbDevicePath,
    NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_INFO, "Ramfb: install Ramfb Vendor DevicePath failed\n"));
    FreePool((VOID*)(UINTN)QemuRamfbMode.FrameBufferBase);
    return Status;
  }

  /* gop devpath + protocol */
  ZeroMem (&AcpiDeviceNode, sizeof (ACPI_ADR_DEVICE_PATH));
  AcpiDeviceNode.Header.Type = ACPI_DEVICE_PATH;
  AcpiDeviceNode.Header.SubType = ACPI_ADR_DP;
  AcpiDeviceNode.ADR = ACPI_DISPLAY_ADR (1, 0, 0, 1, 0,
                                         ACPI_ADR_DISPLAY_TYPE_EXTERNAL_DIGITAL,
                                         0, 0);
  SetDevicePathNodeLength (&AcpiDeviceNode.Header, sizeof (ACPI_ADR_DEVICE_PATH));

  GopDevicePath = AppendDevicePathNode (
    RamfbDevicePath,
    (EFI_DEVICE_PATH_PROTOCOL *) &AcpiDeviceNode);

  Status = gBS->InstallMultipleProtocolInterfaces (
    &GopHandle,
    &gEfiDevicePathProtocolGuid, GopDevicePath,
    &gEfiGraphicsOutputProtocolGuid, &QemuRamfbGraphicsOutput,
    NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_INFO, "Ramfb: install GOP DevicePath failed\n"));
    FreePool((VOID*)(UINTN)QemuRamfbMode.FrameBufferBase);
    return Status;
  }

  gBS->OpenProtocol (
    RamfbHandle,
    &gEfiDevicePathProtocolGuid,
    &DevicePath,
    gImageHandle,
    GopHandle,
    EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);

  return EFI_SUCCESS;
}
