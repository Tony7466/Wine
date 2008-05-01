/*
 * Implementation of the Local Security Authority API
 *
 * Copyright 1999 Juergen Schmied
 * Copyright 2002 Andriy Palamarchuk
 * Copyright 2004 Mike McCormack
 * Copyright 2005 Hans Leidekker
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winternl.h"
#include "ntsecapi.h"
#include "advapi32_misc.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(advapi);

#define ADVAPI_ForceLocalComputer(ServerName, FailureCode) \
    if (!ADVAPI_IsLocalComputer(ServerName)) \
{ \
        FIXME("Action Implemented for local computer only. " \
              "Requested for server %s\n", debugstr_w(ServerName)); \
        return FailureCode; \
}

static void dumpLsaAttributes(PLSA_OBJECT_ATTRIBUTES oa)
{
    if (oa)
    {
        TRACE("\n\tlength=%u, rootdir=%p, objectname=%s\n\tattr=0x%08x, sid=%s qos=%p\n",
              oa->Length, oa->RootDirectory,
              oa->ObjectName?debugstr_w(oa->ObjectName->Buffer):"null",
              oa->Attributes, debugstr_sid(oa->SecurityDescriptor),
              oa->SecurityQualityOfService);
    }
}

static void* ADVAPI_GetDomainName(unsigned sz, unsigned ofs)
{
    HKEY key;
    LONG ret;
    BYTE* ptr = NULL;
    UNICODE_STRING* ustr;

    static const WCHAR wVNETSUP[] = {
        'S','y','s','t','e','m','\\',
        'C','u','r','r','e','n','t','C','o','n','t','r','o','l','S','e','t','\\',
        'S','e','r','v','i','c','e','s','\\',
        'V','x','D','\\','V','N','E','T','S','U','P','\0'};

    ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, wVNETSUP, 0, KEY_READ, &key);
    if (ret == ERROR_SUCCESS)
    {
        DWORD size = 0;
        static const WCHAR wg[] = { 'W','o','r','k','g','r','o','u','p',0 };

        ret = RegQueryValueExW(key, wg, NULL, NULL, NULL, &size);
        if (ret == ERROR_MORE_DATA || ret == ERROR_SUCCESS)
        {
            ptr = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sz + size);
            if (!ptr) return NULL;
            ustr = (UNICODE_STRING*)(ptr + ofs);
            ustr->MaximumLength = size;
            ustr->Buffer = (WCHAR*)(ptr + sz);
            ret = RegQueryValueExW(key, wg, NULL, NULL, (LPBYTE)ustr->Buffer, &size);
            if (ret != ERROR_SUCCESS)
            {
                HeapFree(GetProcessHeap(), 0, ptr);
                ptr = NULL;
            }   
            else ustr->Length = size - sizeof(WCHAR);
        }
        RegCloseKey(key);
    }
    if (!ptr)
    {
        static const WCHAR wDomain[] = {'D','O','M','A','I','N','\0'};
        ptr = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                        sz + sizeof(wDomain));
        if (!ptr) return NULL;
        ustr = (UNICODE_STRING*)(ptr + ofs);
        ustr->MaximumLength = sizeof(wDomain);
        ustr->Buffer = (WCHAR*)(ptr + sz);
        ustr->Length = sizeof(wDomain) - sizeof(WCHAR);
        memcpy(ustr->Buffer, wDomain, sizeof(wDomain));
    }
    return ptr;
}

/******************************************************************************
 * LsaAddAccountRights [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaAddAccountRights(
    LSA_HANDLE policy,
    PSID sid,
    PLSA_UNICODE_STRING rights,
    ULONG count)
{
    FIXME("(%p,%p,%p,0x%08x) stub\n", policy, sid, rights, count);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/******************************************************************************
 * LsaClose [ADVAPI32.@]
 *
 * Closes a handle to a Policy or TrustedDomain.
 *
 * PARAMS
 *  ObjectHandle [I] Handle to a Policy or TrustedDomain.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: NTSTATUS code.
 */
NTSTATUS WINAPI LsaClose(IN LSA_HANDLE ObjectHandle)
{
    FIXME("(%p) stub\n", ObjectHandle);
    return STATUS_SUCCESS;
}

/******************************************************************************
 * LsaCreateTrustedDomainEx [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaCreateTrustedDomainEx(
    LSA_HANDLE policy,
    PTRUSTED_DOMAIN_INFORMATION_EX domain_info,
    PTRUSTED_DOMAIN_AUTH_INFORMATION auth_info,
    ACCESS_MASK access,
    PLSA_HANDLE domain)
{
    FIXME("(%p,%p,%p,0x%08x,%p) stub\n", policy, domain_info, auth_info,
          access, domain);
    return STATUS_SUCCESS;
}

/******************************************************************************
 * LsaDeleteTrustedDomain [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaDeleteTrustedDomain(LSA_HANDLE policy, PSID sid)
{
    FIXME("(%p,%p) stub\n", policy, sid);
    return STATUS_SUCCESS;
}

/******************************************************************************
 * LsaEnumerateAccountRights [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaEnumerateAccountRights(
    LSA_HANDLE policy,
    PSID sid,
    PLSA_UNICODE_STRING *rights,
    PULONG count)
{
    FIXME("(%p,%p,%p,%p) stub\n", policy, sid, rights, count);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/******************************************************************************
 * LsaEnumerateAccountsWithUserRight [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaEnumerateAccountsWithUserRight(
    LSA_HANDLE policy,
    PLSA_UNICODE_STRING rights,
    PVOID *buffer,
    PULONG count)
{
    FIXME("(%p,%p,%p,%p) stub\n", policy, rights, buffer, count);
    return STATUS_NO_MORE_ENTRIES;
}

/******************************************************************************
 * LsaEnumerateTrustedDomains [ADVAPI32.@]
 *
 * Returns the names and SIDs of trusted domains.
 *
 * PARAMS
 *  PolicyHandle          [I] Handle to a Policy object.
 *  EnumerationContext    [I] Pointer to an enumeration handle.
 *  Buffer                [O] Contains the names and SIDs of trusted domains.
 *  PreferredMaximumLength[I] Preferred maximum size in bytes of Buffer.
 *  CountReturned         [O] Number of elements in Buffer.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS,
 *           STATUS_MORE_ENTRIES,
 *           STATUS_NO_MORE_ENTRIES
 *  Failure: NTSTATUS code.
 *
 * NOTES
 *  LsaEnumerateTrustedDomains can be called multiple times to enumerate
 *  all trusted domains.
 */
NTSTATUS WINAPI LsaEnumerateTrustedDomains(
    IN LSA_HANDLE PolicyHandle,
    IN PLSA_ENUMERATION_HANDLE EnumerationContext,
    OUT PVOID* Buffer,
    IN ULONG PreferredMaximumLength,
    OUT PULONG CountReturned)
{
    FIXME("(%p,%p,%p,0x%08x,%p) stub\n", PolicyHandle, EnumerationContext,
          Buffer, PreferredMaximumLength, CountReturned);

    if (CountReturned) *CountReturned = 0;
    return STATUS_SUCCESS;
}

/******************************************************************************
 * LsaEnumerateTrustedDomainsEx [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaEnumerateTrustedDomainsEx(
    LSA_HANDLE policy,
    PLSA_ENUMERATION_HANDLE context,
    PVOID *buffer,
    ULONG length,
    PULONG count)
{
    FIXME("(%p,%p,%p,0x%08x,%p) stub\n", policy, context, buffer, length, count);

    if (count) *count = 0;
    return STATUS_SUCCESS;
}

/******************************************************************************
 * LsaFreeMemory [ADVAPI32.@]
 *
 * Frees memory allocated by a LSA function.
 *
 * PARAMS
 *  Buffer [I] Memory buffer to free.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: NTSTATUS code.
 */
NTSTATUS WINAPI LsaFreeMemory(IN PVOID Buffer)
{
    TRACE("(%p)\n", Buffer);
    return HeapFree(GetProcessHeap(), 0, Buffer);
}

/******************************************************************************
 * LsaLookupNames [ADVAPI32.@]
 *
 * Returns the SIDs of an array of user, group, or local group names.
 *
 * PARAMS
 *  PolicyHandle      [I] Handle to a Policy object.
 *  Count             [I] Number of names in Names.
 *  Names             [I] Array of names to lookup.
 *  ReferencedDomains [O] Array of domains where the names were found.
 *  Sids              [O] Array of SIDs corresponding to Names.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS,
 *           STATUS_SOME_NOT_MAPPED
 *  Failure: STATUS_NONE_MAPPED or NTSTATUS code.
 */
NTSTATUS WINAPI LsaLookupNames(
    IN LSA_HANDLE PolicyHandle,
    IN ULONG Count,
    IN PLSA_UNICODE_STRING Names,
    OUT PLSA_REFERENCED_DOMAIN_LIST* ReferencedDomains,
    OUT PLSA_TRANSLATED_SID* Sids)
{
    FIXME("(%p,0x%08x,%p,%p,%p) stub\n", PolicyHandle, Count, Names,
          ReferencedDomains, Sids);

    return STATUS_NONE_MAPPED;
}

/******************************************************************************
 * LsaLookupNames2 [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaLookupNames2(
    LSA_HANDLE policy,
    ULONG flags,
    ULONG count,
    PLSA_UNICODE_STRING names,
    PLSA_REFERENCED_DOMAIN_LIST *domains,
    PLSA_TRANSLATED_SID2 *sids)
{
    FIXME("(%p,0x%08x,0x%08x,%p,%p,%p) stub\n", policy, flags, count, names, domains, sids);
    return STATUS_NONE_MAPPED;
}

/******************************************************************************
 * LsaLookupSids [ADVAPI32.@]
 *
 * Looks up the names that correspond to an array of SIDs.
 *
 * PARAMS
 *  PolicyHandle      [I] Handle to a Policy object.
 *  Count             [I] Number of SIDs in the Sids array.
 *  Sids              [I] Array of SIDs to lookup.
 *  ReferencedDomains [O] Array of domains where the sids were found.
 *  Names             [O] Array of names corresponding to Sids.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS,
 *           STATUS_SOME_NOT_MAPPED
 *  Failure: STATUS_NONE_MAPPED or NTSTATUS code.
 */
NTSTATUS WINAPI LsaLookupSids(
    IN LSA_HANDLE PolicyHandle,
    IN ULONG Count,
    IN PSID *Sids,
    OUT PLSA_REFERENCED_DOMAIN_LIST *ReferencedDomains,
    OUT PLSA_TRANSLATED_NAME *Names )
{
    FIXME("(%p,%u,%p,%p,%p) stub\n", PolicyHandle, Count, Sids,
          ReferencedDomains, Names);

    return STATUS_NONE_MAPPED;
}

/******************************************************************************
 * LsaNtStatusToWinError [ADVAPI32.@]
 *
 * Converts an LSA NTSTATUS code to a Windows error code.
 *
 * PARAMS
 *  Status [I] NTSTATUS code.
 *
 * RETURNS
 *  Success: Corresponding Windows error code.
 *  Failure: ERROR_MR_MID_NOT_FOUND.
 */
ULONG WINAPI LsaNtStatusToWinError(NTSTATUS Status)
{
    return RtlNtStatusToDosError(Status);
}

/******************************************************************************
 * LsaOpenPolicy [ADVAPI32.@]
 *
 * Opens a handle to the Policy object on a local or remote system.
 *
 * PARAMS
 *  SystemName       [I] Name of the target system.
 *  ObjectAttributes [I] Connection attributes.
 *  DesiredAccess    [I] Requested access rights.
 *  PolicyHandle     [I/O] Handle to the Policy object.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: NTSTATUS code.
 *
 * NOTES
 *  Set SystemName to NULL to open the local Policy object.
 */
NTSTATUS WINAPI LsaOpenPolicy(
    IN PLSA_UNICODE_STRING SystemName,
    IN PLSA_OBJECT_ATTRIBUTES ObjectAttributes,
    IN ACCESS_MASK DesiredAccess,
    IN OUT PLSA_HANDLE PolicyHandle)
{
    FIXME("(%s,%p,0x%08x,%p) stub\n",
          SystemName?debugstr_w(SystemName->Buffer):"(null)",
          ObjectAttributes, DesiredAccess, PolicyHandle);

    ADVAPI_ForceLocalComputer(SystemName ? SystemName->Buffer : NULL,
                              STATUS_ACCESS_VIOLATION);
    dumpLsaAttributes(ObjectAttributes);

    if(PolicyHandle) *PolicyHandle = (LSA_HANDLE)0xcafe;
    return STATUS_SUCCESS;
}

/******************************************************************************
 * LsaOpenTrustedDomainByName [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaOpenTrustedDomainByName(
    LSA_HANDLE policy,
    PLSA_UNICODE_STRING name,
    ACCESS_MASK access,
    PLSA_HANDLE handle)
{
    FIXME("(%p,%p,0x%08x,%p) stub\n", policy, name, access, handle);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/******************************************************************************
 * LsaQueryInformationPolicy [ADVAPI32.@]
 *
 * Returns information about a Policy object.
 *
 * PARAMS
 *  PolicyHandle     [I] Handle to a Policy object.
 *  InformationClass [I] Type of information to retrieve.
 *  Buffer           [O] Pointer to the requested information.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: NTSTATUS code.
 */
NTSTATUS WINAPI LsaQueryInformationPolicy(
    IN LSA_HANDLE PolicyHandle,
    IN POLICY_INFORMATION_CLASS InformationClass,
    OUT PVOID *Buffer)
{
    TRACE("(%p,0x%08x,%p)\n", PolicyHandle, InformationClass, Buffer);

    if(!Buffer) return STATUS_INVALID_PARAMETER;
    switch (InformationClass)
    {
        case PolicyAuditEventsInformation: /* 2 */
        {
            PPOLICY_AUDIT_EVENTS_INFO p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                                    sizeof(POLICY_AUDIT_EVENTS_INFO));
            p->AuditingMode = FALSE; /* no auditing */
            *Buffer = p;
        }
        break;
        case PolicyPrimaryDomainInformation: /* 3 */
        {
            /* Only the domain name is valid for the local computer.
             * All other fields are zero.
             */
            PPOLICY_PRIMARY_DOMAIN_INFO pinfo;

            pinfo = ADVAPI_GetDomainName(sizeof(*pinfo), offsetof(POLICY_PRIMARY_DOMAIN_INFO, Name));

            TRACE("setting domain to %s\n", debugstr_w(pinfo->Name.Buffer));

            *Buffer = pinfo;
        }
        break;
        case PolicyAccountDomainInformation: /* 5 */
        {
            struct di
            {
                POLICY_ACCOUNT_DOMAIN_INFO info;
                SID sid;
                DWORD padding[3];
                WCHAR domain[MAX_COMPUTERNAME_LENGTH + 1];
            };

            DWORD dwSize = MAX_COMPUTERNAME_LENGTH + 1;
            struct di * xdi = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*xdi));

            xdi->info.DomainName.MaximumLength = dwSize * sizeof(WCHAR);
            xdi->info.DomainName.Buffer = xdi->domain;
            if (GetComputerNameW(xdi->info.DomainName.Buffer, &dwSize))
                xdi->info.DomainName.Length = dwSize * sizeof(WCHAR);

            TRACE("setting name to %s\n", debugstr_w(xdi->info.DomainName.Buffer));

            xdi->info.DomainSid = &xdi->sid;

            /* read the computer SID from the registry */
            if (!ADVAPI_GetComputerSid(&xdi->sid))
            {
                HeapFree(GetProcessHeap(), 0, xdi);

                WARN("Computer SID not found\n");

                return STATUS_UNSUCCESSFUL;
            }

            TRACE("setting SID to %s\n", debugstr_sid(&xdi->sid));

            *Buffer = xdi;
        }
        break;
        case  PolicyDnsDomainInformation:	/* 12 (0xc) */
        {
            /* Only the domain name is valid for the local computer.
             * All other fields are zero.
             */
            PPOLICY_DNS_DOMAIN_INFO pinfo;

            pinfo = ADVAPI_GetDomainName(sizeof(*pinfo), offsetof(POLICY_DNS_DOMAIN_INFO, Name));

            TRACE("setting domain to %s\n", debugstr_w(pinfo->Name.Buffer));

            *Buffer = pinfo;
        }
        break;
        case  PolicyAuditLogInformation:
        case  PolicyPdAccountInformation:
        case  PolicyLsaServerRoleInformation:
        case  PolicyReplicaSourceInformation:
        case  PolicyDefaultQuotaInformation:
        case  PolicyModificationInformation:
        case  PolicyAuditFullSetInformation:
        case  PolicyAuditFullQueryInformation:
        {
            FIXME("category %d not implemented\n", InformationClass);
            return STATUS_UNSUCCESSFUL;
        }
    }
    return STATUS_SUCCESS;
}

/******************************************************************************
 * LsaQueryTrustedDomainInfo [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaQueryTrustedDomainInfo(
    LSA_HANDLE policy,
    PSID sid,
    TRUSTED_INFORMATION_CLASS class,
    PVOID *buffer)
{
    FIXME("(%p,%p,%d,%p) stub\n", policy, sid, class, buffer);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/******************************************************************************
 * LsaQueryTrustedDomainInfoByName [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaQueryTrustedDomainInfoByName(
    LSA_HANDLE policy,
    PLSA_UNICODE_STRING name,
    TRUSTED_INFORMATION_CLASS class,
    PVOID *buffer)
{
    FIXME("(%p,%p,%d,%p) stub\n", policy, name, class, buffer);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/******************************************************************************
 * LsaRegisterPolicyChangeNotification [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaRegisterPolicyChangeNotification(
    POLICY_NOTIFICATION_INFORMATION_CLASS class,
    HANDLE event)
{
    FIXME("(%d,%p) stub\n", class, event);
    return STATUS_UNSUCCESSFUL;
}

/******************************************************************************
 * LsaRemoveAccountRights [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaRemoveAccountRights(
    LSA_HANDLE policy,
    PSID sid,
    BOOLEAN all,
    PLSA_UNICODE_STRING rights,
    ULONG count)
{
    FIXME("(%p,%p,%d,%p,0x%08x) stub\n", policy, sid, all, rights, count);
    return STATUS_SUCCESS;
}

/******************************************************************************
 * LsaRetrievePrivateData [ADVAPI32.@]
 *
 * Retrieves data stored by LsaStorePrivateData.
 *
 * PARAMS
 *  PolicyHandle [I] Handle to a Policy object.
 *  KeyName      [I] Name of the key where the data is stored.
 *  PrivateData  [O] Pointer to the private data.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: STATUS_OBJECT_NAME_NOT_FOUND or NTSTATUS code.
 */
NTSTATUS WINAPI LsaRetrievePrivateData(
    IN LSA_HANDLE PolicyHandle,
    IN PLSA_UNICODE_STRING KeyName,
    OUT PLSA_UNICODE_STRING* PrivateData)
{
    FIXME("(%p,%p,%p) stub\n", PolicyHandle, KeyName, PrivateData);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/******************************************************************************
 * LsaSetInformationPolicy [ADVAPI32.@]
 *
 * Modifies information in a Policy object.
 *
 * PARAMS
 *  PolicyHandle     [I] Handle to a Policy object.
 *  InformationClass [I] Type of information to set.
 *  Buffer           [I] Pointer to the information to set.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: NTSTATUS code.
 */
NTSTATUS WINAPI LsaSetInformationPolicy(
    IN LSA_HANDLE PolicyHandle,
    IN POLICY_INFORMATION_CLASS InformationClass,
    IN PVOID Buffer)
{
    FIXME("(%p,0x%08x,%p) stub\n", PolicyHandle, InformationClass, Buffer);

    return STATUS_UNSUCCESSFUL;
}

/******************************************************************************
 * LsaSetTrustedDomainInfoByName [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaSetTrustedDomainInfoByName(
    LSA_HANDLE policy,
    PLSA_UNICODE_STRING name,
    TRUSTED_INFORMATION_CLASS class,
    PVOID buffer)
{
    FIXME("(%p,%p,%d,%p) stub\n", policy, name, class, buffer);
    return STATUS_SUCCESS;
}

/******************************************************************************
 * LsaSetTrustedDomainInformation [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaSetTrustedDomainInformation(
    LSA_HANDLE policy,
    PSID sid,
    TRUSTED_INFORMATION_CLASS class,
    PVOID buffer)
{
    FIXME("(%p,%p,%d,%p) stub\n", policy, sid, class, buffer);
    return STATUS_SUCCESS;
}

/******************************************************************************
 * LsaStorePrivateData [ADVAPI32.@]
 *
 * Stores or deletes a Policy object's data under the specified reg key.
 *
 * PARAMS
 *  PolicyHandle [I] Handle to a Policy object.
 *  KeyName      [I] Name of the key where the data will be stored.
 *  PrivateData  [O] Pointer to the private data.
 *
 * RETURNS
 *  Success: STATUS_SUCCESS.
 *  Failure: STATUS_OBJECT_NAME_NOT_FOUND or NTSTATUS code.
 */
NTSTATUS WINAPI LsaStorePrivateData(
    IN LSA_HANDLE PolicyHandle,
    IN PLSA_UNICODE_STRING KeyName,
    IN PLSA_UNICODE_STRING PrivateData)
{
    FIXME("(%p,%p,%p) stub\n", PolicyHandle, KeyName, PrivateData);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/******************************************************************************
 * LsaUnregisterPolicyChangeNotification [ADVAPI32.@]
 *
 */
NTSTATUS WINAPI LsaUnregisterPolicyChangeNotification(
    POLICY_NOTIFICATION_INFORMATION_CLASS class,
    HANDLE event)
{
    FIXME("(%d,%p) stub\n", class, event);
    return STATUS_SUCCESS;
}
