/**********************************************************************
* Copyright (c) 2013-2014  Red Hat, Inc.
*
* Developed by Daynix Computing LTD.
*
* Authors:
*     Dmitry Fleytman <dmitry@daynix.com>
*     Pavel Gurvich <pavel@daynix.com>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
**********************************************************************/

#pragma once

#include "WdfDevice.h"
#include "Alloc.h"
#include "RegText.h"
#include "Irp.h"
#include "RedirectorStrategy.h"
#include "Public.h"

class CUsbDkControlDevice;
class CUsbDkFilterDevice;

typedef struct _USBDK_FILTER_DEVICE_EXTENSION {

    CUsbDkFilterDevice *UsbDkFilter;

    _USBDK_FILTER_DEVICE_EXTENSION(const _USBDK_FILTER_DEVICE_EXTENSION&) = delete;
    _USBDK_FILTER_DEVICE_EXTENSION& operator= (const _USBDK_FILTER_DEVICE_EXTENSION&) = delete;

} USBDK_FILTER_DEVICE_EXTENSION, *PUSBDK_FILTER_DEVICE_EXTENSION;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(USBDK_FILTER_DEVICE_EXTENSION, UsbDkFilterGetContext);

class CUsbDkChildDevice : public CAllocatable<NonPagedPool, 'DCHR'>
{
public:

    typedef CBufferSet<NonPagedPool, 'CCHR', UCHAR> TDescriptorsCache;

    CUsbDkChildDevice(CRegText *DeviceID,
                      CRegText *InstanceID,
                      ULONG Port,
                      USB_DK_DEVICE_SPEED Speed,
                      USB_DEVICE_DESCRIPTOR &DevDescriptor,
                      TDescriptorsCache &CfgDescriptors,
                      const CUsbDkFilterDevice &ParentDevice,
                      PDEVICE_OBJECT PDO)
        : m_DeviceID(DeviceID)
        , m_InstanceID(InstanceID)
        , m_Port(Port)
        , m_Speed(Speed)
        , m_DevDescriptor(DevDescriptor)
        , m_CfgDescriptors(CfgDescriptors)
        , m_ParentDevice(ParentDevice)
        , m_PDO(PDO)
    {}

    ULONG ParentID() const;
    PCWCHAR DeviceID() const { return *m_DeviceID->begin(); }
    PCWCHAR InstanceID() const { return *m_InstanceID->begin(); }
    ULONG Port() const
    { return m_Port; }
    USB_DK_DEVICE_SPEED Speed() const
    { return m_Speed; }
    const USB_DEVICE_DESCRIPTOR &DeviceDescriptor() const
    { return m_DevDescriptor; }
    PDEVICE_OBJECT PDO() const { return m_PDO; }

    bool ConfigurationDescriptor(UCHAR Index, USB_CONFIGURATION_DESCRIPTOR &Buffer, size_t BufferLength)
    {
        if (Index < m_CfgDescriptors.Size())
        {
            m_CfgDescriptors.CopyEntry(Index, (PVOID)&Buffer, BufferLength);
            return true;
        }
        return false;
    }

    bool Match(PCWCHAR deviceID, PCWCHAR instanceID) const
    { return m_DeviceID->Match(deviceID) && m_InstanceID->Match(instanceID); }

    bool Match(PDEVICE_OBJECT PDO) const
    { return m_PDO == PDO; }

    bool MakeRedirected();

    void Dump();

private:
    CObjHolder<CRegText> m_DeviceID;
    CObjHolder<CRegText> m_InstanceID;
    ULONG m_Port;
    USB_DK_DEVICE_SPEED m_Speed;
    USB_DEVICE_DESCRIPTOR m_DevDescriptor;
    TDescriptorsCache m_CfgDescriptors;
    PDEVICE_OBJECT m_PDO;
    const CUsbDkFilterDevice &m_ParentDevice;

    bool CreateRedirectorDevice();

    CUsbDkChildDevice(const CUsbDkChildDevice&) = delete;
    CUsbDkChildDevice& operator= (const CUsbDkChildDevice&) = delete;

    DECLARE_CWDMLIST_ENTRY(CUsbDkChildDevice);
};

class CDeviceRelations;
class CWdmUsbDeviceAccess;

class CUsbDkHubFilterStrategy : public CUsbDkFilterStrategy
{
public:
    virtual NTSTATUS Create(CUsbDkFilterDevice *Owner) override;
    virtual void Delete() override;
    virtual NTSTATUS MakeAvailable() override
    { return STATUS_SUCCESS; }
    virtual NTSTATUS PNPPreProcess(PIRP Irp) override;

private:
    void DropRemovedDevices(const CDeviceRelations &Relations);
    void AddNewDevices(const CDeviceRelations &Relations);
    void RegisterNewChild(PDEVICE_OBJECT PDO);
    void ApplyRedirectionPolicy(CUsbDkChildDevice &Device);
    bool FetchConfigurationDescriptors(CWdmUsbDeviceAccess &devAccess,
                                       CUsbDkChildDevice::TDescriptorsCache &DescriptorsHolder);

    bool IsChildRegistered(PDEVICE_OBJECT PDO)
    { return !Children().ForEachIf([PDO](CUsbDkChildDevice *Child){ return Child->Match(PDO); }, ConstFalse); }
};

class CUsbDkFilterDevice : public CWdfDevice, public CAllocatable<NonPagedPool, 'DFHR'>
{
public:
    CUsbDkFilterDevice()
    {}
    ~CUsbDkFilterDevice()
    { m_Strategy->Delete(); }

    NTSTATUS Create(PWDFDEVICE_INIT DevInit, WDFDRIVER Driver);

    template <typename TPredicate, typename TFunctor>
    bool EnumerateChildrenIf(TPredicate Predicate, TFunctor Functor)
    { return m_Strategy->Children().ForEachIf(Predicate, Functor); }

    ULONG GetChildrenCount()
    { return m_Strategy->Children().GetCount(); }

    WDFDRIVER GetDriverHandle() const
    { return m_Driver; }

    PDRIVER_OBJECT GetDriverObject() const
    { return WdfDriverWdmGetDriverObject(m_Driver); }

    ULONG GetInstanceNumber() const
    { return m_InstanceNumber; }

private:
    NTSTATUS InitializeFilterDevice(PWDFDEVICE_INIT DevInit);
    NTSTATUS DefineStrategy();
    static void ContextCleanup(_In_ WDFOBJECT DeviceObject);

    WDFDRIVER m_Driver = WDF_NO_HANDLE;

    class CStrategist
    {
    public:
        bool SelectStrategy(PDEVICE_OBJECT Device);
        void SetNullStrategy() { m_Strategy = &m_NullStrategy; }
        CUsbDkFilterStrategy *operator ->() const { return m_Strategy; }
        static size_t GetRequestContextSize();
    private:
        CUsbDkFilterStrategy *m_Strategy = nullptr;
        CUsbDkNullFilterStrategy m_NullStrategy;
        CUsbDkHubFilterStrategy m_HubStrategy;
        CUsbDkRedirectorStrategy m_DevStrategy;
    } m_Strategy;

    CUsbDkFilterDevice(const CUsbDkFilterDevice&) = delete;
    CUsbDkFilterDevice& operator= (const CUsbDkFilterDevice&) = delete;

    CInstanceCounter<CUsbDkFilterDevice> m_InstanceNumber;

    friend class CUsbDkFilterDeviceInit;
    friend class CUsbDkRedirectorQueueData;
    friend class CUsbDkRedirectorQueueConfig;

    DECLARE_CWDMLIST_ENTRY(CUsbDkFilterDevice);
};
