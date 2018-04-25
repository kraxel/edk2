#include <Uefi.h>
#include <Protocol/PciIo.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PciLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/QemuFwCfgLib.h>

#include <IndustryStandard/Pci.h>

VOID *QemuIGDHelperOpRegion;

EFI_STATUS
EFIAPI
QemuIGDHelperControllerDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  )
{
  EFI_STATUS          Status;
  EFI_PCI_IO_PROTOCOL *PciIo;
  PCI_TYPE00          Pci;
  UINT32              OpRegion;

  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint32,
                        0,
                        sizeof (Pci) / sizeof (UINT32),
                        &Pci
                        );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = EFI_UNSUPPORTED;
  if (!IS_PCI_VGA (&Pci)) {
    goto Done;
  }
  if (Pci.Hdr.VendorId != 0x8086 /* Intel */) {
    goto Done;
  }

  DEBUG ((EFI_D_INFO, "IGDHelper: intel gfx %x:%x found\n",
          Pci.Hdr.VendorId, Pci.Hdr.DeviceId));
  OpRegion = (UINTN)QemuIGDHelperOpRegion;
  Status = PciIo->Pci.Write (
                        PciIo,
                        EfiPciIoWidthUint32,
                        0xFC,
                        1,
                        &OpRegion
                        );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_INFO, "IGDHelper: write error: %d\n", Status));
  } else {
    DEBUG ((EFI_D_INFO, "IGDHelper: opregion setup OK\n"));
  }

Done:
  gBS->CloseProtocol (
        Controller,
        &gEfiPciIoProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  return Status;
}

EFI_STATUS
EFIAPI
QemuIGDHelperControllerDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  )
{
  DEBUG ((EFI_D_INFO, "QemuIGDHelperControllerDriverStart\n"));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
QemuIGDHelperControllerDriverStop (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN UINTN                          NumberOfChildren,
  IN EFI_HANDLE                     *ChildHandleBuffer
  )
{
  DEBUG ((EFI_D_INFO, "QemuIGDHelperControllerDriverStop\n"));
  return EFI_SUCCESS;
}

EFI_DRIVER_BINDING_PROTOCOL gQemuIGDHelperDriverBinding = {
  QemuIGDHelperControllerDriverSupported,
  QemuIGDHelperControllerDriverStart,
  QemuIGDHelperControllerDriverStop,
  0x10,
  NULL,
  NULL
};

EFI_STATUS
EFIAPI
QemuIGDHelperComponentNameGetDriverName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **DriverName
  );

EFI_STATUS
EFIAPI
QemuIGDHelperComponentNameGetControllerName (
  IN  EFI_COMPONENT_NAME_PROTOCOL                     *This,
  IN  EFI_HANDLE                                      ControllerHandle,
  IN  EFI_HANDLE                                      ChildHandle        OPTIONAL,
  IN  CHAR8                                           *Language,
  OUT CHAR16                                          **ControllerName
  );

EFI_UNICODE_STRING_TABLE mQemuIGDHelperDriverNameTable[] = {
  { "eng;en", L"QEMU IGD Helper" },
  { NULL , NULL }
};

EFI_UNICODE_STRING_TABLE mQemuIGDHelperControllerNameTable[] = {
  { "eng;en", L"IGD" },
  { NULL , NULL }
};

EFI_COMPONENT_NAME_PROTOCOL  gQemuIGDHelperComponentName = {
  QemuIGDHelperComponentNameGetDriverName,
  QemuIGDHelperComponentNameGetControllerName,
  "eng"
};

EFI_COMPONENT_NAME2_PROTOCOL gQemuIGDHelperComponentName2 = {
  (EFI_COMPONENT_NAME2_GET_DRIVER_NAME) QemuIGDHelperComponentNameGetDriverName,
  (EFI_COMPONENT_NAME2_GET_CONTROLLER_NAME) QemuIGDHelperComponentNameGetControllerName,
  "en"
};

EFI_STATUS
EFIAPI
QemuIGDHelperComponentNameGetDriverName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **DriverName
  )
{
  return LookupUnicodeString2 (
           Language,
           This->SupportedLanguages,
           mQemuIGDHelperDriverNameTable,
           DriverName,
           (BOOLEAN)(This == &gQemuIGDHelperComponentName)
           );
}

EFI_STATUS
EFIAPI
QemuIGDHelperComponentNameGetControllerName (
  IN  EFI_COMPONENT_NAME_PROTOCOL                     *This,
  IN  EFI_HANDLE                                      ControllerHandle,
  IN  EFI_HANDLE                                      ChildHandle        OPTIONAL,
  IN  CHAR8                                           *Language,
  OUT CHAR16                                          **ControllerName
  )
{
  EFI_STATUS                      Status;

  if (ChildHandle != NULL) {
    return EFI_UNSUPPORTED;
  }

  Status = EfiTestManagedDevice (
             ControllerHandle,
             gQemuIGDHelperDriverBinding.DriverBindingHandle,
             &gEfiPciIoProtocolGuid
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return LookupUnicodeString2 (
           Language,
           This->SupportedLanguages,
           mQemuIGDHelperControllerNameTable,
           ControllerName,
           (BOOLEAN)(This == &gQemuIGDHelperComponentName)
           );
}

EFI_STATUS
EFIAPI
InitializeQemuIGDHelper (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
  EFI_STATUS                Status;
  RETURN_STATUS             Ret;
  FIRMWARE_CONFIG_ITEM      Item;
  UINTN                     Size,Pages;

  DEBUG ((EFI_D_INFO, "InitializeQemuIGDHelper\n"));

  if (!QemuFwCfgIsAvailable()) {
    DEBUG ((EFI_D_INFO, "IGDHelper: no FwCfg\n"));
    return EFI_NOT_FOUND;
  }

  Ret = QemuFwCfgFindFile("etc/igd-opregion", &Item, &Size);
  if (Ret != RETURN_SUCCESS) {
    DEBUG ((EFI_D_INFO, "IGDHelper: no etc/igd-opregion in FwCfg\n"));
    return EFI_NOT_FOUND;
  }

  Pages = EFI_SIZE_TO_PAGES(Size);
  DEBUG ((EFI_D_INFO, "IGDHelper: opregion: fwcfg entry %d, size %d (%d pages)\n",
          Item, Size, Pages));

  QemuIGDHelperOpRegion = AllocateRuntimePages(Pages);
  QemuFwCfgSelectItem(Item);
  QemuFwCfgReadBytes(Size, QemuIGDHelperOpRegion);
  DEBUG ((EFI_D_INFO, "IGDHelper: opregion: allocated at %p\n",
          QemuIGDHelperOpRegion));

  Status = EfiLibInstallDriverBindingComponentName2 (
             ImageHandle,
             SystemTable,
             &gQemuIGDHelperDriverBinding,
             ImageHandle,
             &gQemuIGDHelperComponentName,
             &gQemuIGDHelperComponentName2
             );
  ASSERT_EFI_ERROR (Status);

  return Status;
}
