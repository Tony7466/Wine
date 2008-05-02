/*
 * Copyright 2006 Juan Lang
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
 *
 */
#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "wincrypt.h"
#include "wine/debug.h"
#include "crypt32_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(crypt);

static HCERTCHAINENGINE CRYPT_defaultChainEngine;

/* This represents a subset of a certificate chain engine:  it doesn't include
 * the "hOther" store described by MSDN, because I'm not sure how that's used.
 * It also doesn't include the "hTrust" store, because I don't yet implement
 * CTLs or complex certificate chains.
 */
typedef struct _CertificateChainEngine
{
    LONG       ref;
    HCERTSTORE hRoot;
    HCERTSTORE hWorld;
    DWORD      dwFlags;
    DWORD      dwUrlRetrievalTimeout;
    DWORD      MaximumCachedCertificates;
    DWORD      CycleDetectionModulus;
} CertificateChainEngine, *PCertificateChainEngine;

static inline void CRYPT_AddStoresToCollection(HCERTSTORE collection,
 DWORD cStores, HCERTSTORE *stores)
{
    DWORD i;

    for (i = 0; i < cStores; i++)
        CertAddStoreToCollection(collection, stores[i], 0, 0);
}

static inline void CRYPT_CloseStores(DWORD cStores, HCERTSTORE *stores)
{
    DWORD i;

    for (i = 0; i < cStores; i++)
        CertCloseStore(stores[i], 0);
}

static const WCHAR rootW[] = { 'R','o','o','t',0 };

static BOOL CRYPT_CheckRestrictedRoot(HCERTSTORE store)
{
    BOOL ret = TRUE;

    if (store)
    {
        HCERTSTORE rootStore = CertOpenSystemStoreW(0, rootW);
        PCCERT_CONTEXT cert = NULL, check;
        BYTE hash[20];
        DWORD size;

        do {
            cert = CertEnumCertificatesInStore(store, cert);
            if (cert)
            {
                size = sizeof(hash);

                ret = CertGetCertificateContextProperty(cert, CERT_HASH_PROP_ID,
                 hash, &size);
                if (ret)
                {
                    CRYPT_HASH_BLOB blob = { sizeof(hash), hash };

                    check = CertFindCertificateInStore(rootStore,
                     cert->dwCertEncodingType, 0, CERT_FIND_SHA1_HASH, &blob,
                     NULL);
                    if (!check)
                        ret = FALSE;
                    else
                        CertFreeCertificateContext(check);
                }
            }
        } while (ret && cert);
        if (cert)
            CertFreeCertificateContext(cert);
        CertCloseStore(rootStore, 0);
    }
    return ret;
}

BOOL WINAPI CertCreateCertificateChainEngine(PCERT_CHAIN_ENGINE_CONFIG pConfig,
 HCERTCHAINENGINE *phChainEngine)
{
    static const WCHAR caW[] = { 'C','A',0 };
    static const WCHAR myW[] = { 'M','y',0 };
    static const WCHAR trustW[] = { 'T','r','u','s','t',0 };
    BOOL ret;

    TRACE("(%p, %p)\n", pConfig, phChainEngine);

    if (pConfig->cbSize != sizeof(*pConfig))
    {
        SetLastError(E_INVALIDARG);
        return FALSE;
    }
    *phChainEngine = NULL;
    ret = CRYPT_CheckRestrictedRoot(pConfig->hRestrictedRoot);
    if (ret)
    {
        PCertificateChainEngine engine =
         CryptMemAlloc(sizeof(CertificateChainEngine));

        if (engine)
        {
            HCERTSTORE worldStores[4];

            engine->ref = 1;
            if (pConfig->hRestrictedRoot)
                engine->hRoot = CertDuplicateStore(pConfig->hRestrictedRoot);
            else
                engine->hRoot = CertOpenSystemStoreW(0, rootW);
            engine->hWorld = CertOpenStore(CERT_STORE_PROV_COLLECTION, 0, 0,
             CERT_STORE_CREATE_NEW_FLAG, NULL);
            worldStores[0] = CertDuplicateStore(engine->hRoot);
            worldStores[1] = CertOpenSystemStoreW(0, caW);
            worldStores[2] = CertOpenSystemStoreW(0, myW);
            worldStores[3] = CertOpenSystemStoreW(0, trustW);
            CRYPT_AddStoresToCollection(engine->hWorld,
             sizeof(worldStores) / sizeof(worldStores[0]), worldStores);
            CRYPT_AddStoresToCollection(engine->hWorld,
             pConfig->cAdditionalStore, pConfig->rghAdditionalStore);
            CRYPT_CloseStores(sizeof(worldStores) / sizeof(worldStores[0]),
             worldStores);
            engine->dwFlags = pConfig->dwFlags;
            engine->dwUrlRetrievalTimeout = pConfig->dwUrlRetrievalTimeout;
            engine->MaximumCachedCertificates =
             pConfig->MaximumCachedCertificates;
            engine->CycleDetectionModulus = pConfig->CycleDetectionModulus;
            *phChainEngine = (HCERTCHAINENGINE)engine;
            ret = TRUE;
        }
        else
            ret = FALSE;
    }
    return ret;
}

void WINAPI CertFreeCertificateChainEngine(HCERTCHAINENGINE hChainEngine)
{
    PCertificateChainEngine engine = (PCertificateChainEngine)hChainEngine;

    TRACE("(%p)\n", hChainEngine);

    if (engine && InterlockedDecrement(&engine->ref) == 0)
    {
        CertCloseStore(engine->hWorld, 0);
        CertCloseStore(engine->hRoot, 0);
        CryptMemFree(engine);
    }
}

static HCERTCHAINENGINE CRYPT_GetDefaultChainEngine(void)
{
    if (!CRYPT_defaultChainEngine)
    {
        CERT_CHAIN_ENGINE_CONFIG config = { 0 };
        HCERTCHAINENGINE engine;

        config.cbSize = sizeof(config);
        CertCreateCertificateChainEngine(&config, &engine);
        InterlockedCompareExchangePointer(&CRYPT_defaultChainEngine, engine,
         NULL);
        if (CRYPT_defaultChainEngine != engine)
            CertFreeCertificateChainEngine(engine);
    }
    return CRYPT_defaultChainEngine;
}

void default_chain_engine_free(void)
{
    CertFreeCertificateChainEngine(CRYPT_defaultChainEngine);
}

typedef struct _CertificateChain
{
    CERT_CHAIN_CONTEXT context;
    LONG ref;
} CertificateChain, *PCertificateChain;

static inline BOOL WINAPI CRYPT_IsCertificateSelfSigned(PCCERT_CONTEXT cert)
{
    return CertCompareCertificateName(cert->dwCertEncodingType,
     &cert->pCertInfo->Subject, &cert->pCertInfo->Issuer);
}

/* Gets cert's issuer from store, and returns the validity flags associated
 * with it.  Returns NULL if no issuer whose public key matches cert's
 * signature could be found.
 */
static PCCERT_CONTEXT CRYPT_GetIssuerFromStore(HCERTSTORE store,
 PCCERT_CONTEXT cert, PDWORD pdwFlags)
{
    PCCERT_CONTEXT issuer = NULL;

    /* There might be more than issuer with the same name, so keep looking until
     * one produces the correct signature for this cert.
     */
    do {
        *pdwFlags = CERT_STORE_REVOCATION_FLAG | CERT_STORE_SIGNATURE_FLAG |
         CERT_STORE_TIME_VALIDITY_FLAG;
        issuer = CertGetIssuerCertificateFromStore(store, cert, issuer,
         pdwFlags);
    } while (issuer && (*pdwFlags & CERT_STORE_SIGNATURE_FLAG));
    return issuer;
}

static BOOL CRYPT_AddCertToSimpleChain(PCERT_SIMPLE_CHAIN chain,
 PCCERT_CONTEXT cert, DWORD dwFlags)
{
    BOOL ret = FALSE;
    PCERT_CHAIN_ELEMENT element = CryptMemAlloc(sizeof(CERT_CHAIN_ELEMENT));

    if (element)
    {
        if (!chain->cElement)
            chain->rgpElement = CryptMemAlloc(sizeof(PCERT_CHAIN_ELEMENT));
        else
            chain->rgpElement = CryptMemRealloc(chain->rgpElement,
             (chain->cElement + 1) * sizeof(PCERT_CHAIN_ELEMENT));
        if (chain->rgpElement)
        {
            memset(element, 0, sizeof(CERT_CHAIN_ELEMENT));
            element->cbSize = sizeof(CERT_CHAIN_ELEMENT);
            element->pCertContext = cert;
            if (dwFlags & CERT_STORE_REVOCATION_FLAG &&
             !(dwFlags & CERT_STORE_NO_CRL_FLAG))
                element->TrustStatus.dwErrorStatus |= CERT_TRUST_IS_REVOKED;
            if (dwFlags & CERT_STORE_SIGNATURE_FLAG)
                element->TrustStatus.dwErrorStatus |=
                 CERT_TRUST_IS_NOT_SIGNATURE_VALID;
            if (dwFlags & CERT_STORE_TIME_VALIDITY_FLAG)
                element->TrustStatus.dwErrorStatus |=
                 CERT_TRUST_IS_NOT_TIME_VALID;
            /* It appears, from every example certificate chain I've found,
             * that this flag is always set:
             */
            element->TrustStatus.dwInfoStatus = CERT_TRUST_HAS_PREFERRED_ISSUER;
            if (chain->cElement)
            {
                PCERT_CHAIN_ELEMENT prevElement =
                 chain->rgpElement[chain->cElement - 1];

                /* This cert is the issuer of the previous one in the chain, so
                 * retroactively check the previous one's time validity nesting.
                 */
                if (!CertVerifyValidityNesting(
                 prevElement->pCertContext->pCertInfo, cert->pCertInfo))
                    prevElement->TrustStatus.dwErrorStatus |=
                     CERT_TRUST_IS_NOT_TIME_NESTED;
            }
            /* FIXME: check valid usages, name constraints, and for cycles */
            /* FIXME: initialize the rest of element */
            chain->TrustStatus.dwErrorStatus |=
             element->TrustStatus.dwErrorStatus;
            chain->TrustStatus.dwInfoStatus |=
             element->TrustStatus.dwInfoStatus;
            chain->rgpElement[chain->cElement++] = element;
            ret = TRUE;
        }
        else
            CryptMemFree(element);
    }
    return ret;
}

static void CRYPT_FreeSimpleChain(PCERT_SIMPLE_CHAIN chain)
{
    DWORD i;

    for (i = 0; i < chain->cElement; i++)
        CryptMemFree(chain->rgpElement[i]);
    CryptMemFree(chain->rgpElement);
    CryptMemFree(chain);
}

static BOOL CRYPT_BuildSimpleChain(HCERTCHAINENGINE hChainEngine,
 PCCERT_CONTEXT cert, LPFILETIME pTime, HCERTSTORE hAdditionalStore,
 PCERT_SIMPLE_CHAIN *ppChain)
{
    BOOL ret = FALSE;
    PCertificateChainEngine engine = (PCertificateChainEngine)hChainEngine;
    PCERT_SIMPLE_CHAIN chain;
    HCERTSTORE world;

    TRACE("(%p, %p, %p, %p)\n", hChainEngine, cert, pTime, hAdditionalStore);

    world = CertOpenStore(CERT_STORE_PROV_COLLECTION, 0, 0,
     CERT_STORE_CREATE_NEW_FLAG, NULL);
    CertAddStoreToCollection(world, engine->hWorld, 0, 0);
    if (cert->hCertStore)
        CertAddStoreToCollection(world, cert->hCertStore, 0, 0);
    if (hAdditionalStore)
        CertAddStoreToCollection(world, hAdditionalStore, 0, 0);
    chain = CryptMemAlloc(sizeof(CERT_SIMPLE_CHAIN));
    if (chain)
    {
        memset(chain, 0, sizeof(CERT_SIMPLE_CHAIN));
        chain->cbSize = sizeof(CERT_SIMPLE_CHAIN);
        ret = CRYPT_AddCertToSimpleChain(chain, cert, 0);
        while (ret && !CRYPT_IsCertificateSelfSigned(cert))
        {
            DWORD flags;
            PCCERT_CONTEXT issuer = CRYPT_GetIssuerFromStore(world, cert,
             &flags);

            if (issuer)
            {
                ret = CRYPT_AddCertToSimpleChain(chain, issuer, flags);
                cert = issuer;
            }
            else
            {
                TRACE("Couldn't find issuer, aborting chain creation\n");
                ret = FALSE;
            }
        }
        if (ret)
        {
            PCCERT_CONTEXT root = chain->rgpElement[chain->cElement - 1]->
             pCertContext;

            if (!(ret = CRYPT_IsCertificateSelfSigned(root)))
                TRACE("Last certificate is not self-signed\n");
            else
            {
                chain->rgpElement[chain->cElement - 1]->TrustStatus.dwInfoStatus
                 |= CERT_TRUST_IS_SELF_SIGNED;
                if (!(ret = CryptVerifyCertificateSignatureEx(0,
                 root->dwCertEncodingType, CRYPT_VERIFY_CERT_SIGN_SUBJECT_CERT,
                 (void *)root, CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT, (void *)root,
                 0, NULL)))
                    TRACE("Last certificate's signature is invalid\n");
            }
            if (ret)
            {
                BYTE hash[20];
                DWORD size = sizeof(hash);
                CRYPT_HASH_BLOB blob = { sizeof(hash), hash };
                PCCERT_CONTEXT trustedRoot;

                CertGetCertificateContextProperty(root, CERT_HASH_PROP_ID, hash,
                 &size);
                trustedRoot = CertFindCertificateInStore(engine->hRoot,
                 root->dwCertEncodingType, 0, CERT_FIND_SHA1_HASH, &blob, NULL);
                if (!trustedRoot)
                    chain->TrustStatus.dwErrorStatus |=
                     CERT_TRUST_IS_UNTRUSTED_ROOT;
                else
                    CertFreeCertificateContext(trustedRoot);
            }
        }
        if (!ret)
        {
            CRYPT_FreeSimpleChain(chain);
            chain = NULL;
        }
        *ppChain = chain;
    }
    CertCloseStore(world, 0);
    return ret;
}

typedef struct _CERT_CHAIN_PARA_NO_EXTRA_FIELDS {
    DWORD            cbSize;
    CERT_USAGE_MATCH RequestedUsage;
} CERT_CHAIN_PARA_NO_EXTRA_FIELDS, *PCERT_CHAIN_PARA_NO_EXTRA_FIELDS;

typedef struct _CERT_CHAIN_PARA_EXTRA_FIELDS {
    DWORD            cbSize;
    CERT_USAGE_MATCH RequestedUsage;
    CERT_USAGE_MATCH RequestedIssuancePolicy;
    DWORD            dwUrlRetrievalTimeout;
    BOOL             fCheckRevocationFreshnessTime;
    DWORD            dwRevocationFreshnessTime;
} CERT_CHAIN_PARA_EXTRA_FIELDS, *PCERT_CHAIN_PARA_EXTRA_FIELDS;

BOOL WINAPI CertGetCertificateChain(HCERTCHAINENGINE hChainEngine,
 PCCERT_CONTEXT pCertContext, LPFILETIME pTime, HCERTSTORE hAdditionalStore,
 PCERT_CHAIN_PARA pChainPara, DWORD dwFlags, LPVOID pvReserved,
 PCCERT_CHAIN_CONTEXT* ppChainContext)
{
    PCERT_SIMPLE_CHAIN simpleChain = NULL;
    BOOL ret;

    TRACE("(%p, %p, %p, %p, %p, %08x, %p, %p)\n", hChainEngine, pCertContext,
     pTime, hAdditionalStore, pChainPara, dwFlags, pvReserved, ppChainContext);

    if (!pChainPara)
    {
        SetLastError(E_INVALIDARG);
        return FALSE;
    }
    if (ppChainContext)
        *ppChainContext = NULL;
    if (!hChainEngine)
        hChainEngine = CRYPT_GetDefaultChainEngine();
    /* FIXME: what about HCCE_LOCAL_MACHINE? */
    /* FIXME: pChainPara is for now ignored */
    /* FIXME: only simple chains are supported for now, as CTLs aren't
     * supported yet.
     */
    if ((ret = CRYPT_BuildSimpleChain(hChainEngine, pCertContext, pTime,
     hAdditionalStore, &simpleChain)))
    {
        PCertificateChain chain = CryptMemAlloc(sizeof(CertificateChain));

        if (chain)
        {
            chain->ref = 1;
            chain->context.cbSize = sizeof(CERT_CHAIN_CONTEXT);
            memcpy(&chain->context.TrustStatus, &simpleChain->TrustStatus,
             sizeof(CERT_TRUST_STATUS));
            chain->context.cChain = 1;
            chain->context.rgpChain = CryptMemAlloc(sizeof(PCERT_SIMPLE_CHAIN));
            chain->context.rgpChain[0] = simpleChain;
            chain->context.cLowerQualityChainContext = 0;
            chain->context.rgpLowerQualityChainContext = NULL;
            chain->context.fHasRevocationFreshnessTime = FALSE;
            chain->context.dwRevocationFreshnessTime = 0;
        }
        else
            ret = FALSE;
        if (ppChainContext)
            *ppChainContext = (PCCERT_CHAIN_CONTEXT)chain;
        else
            CertFreeCertificateChain((PCCERT_CHAIN_CONTEXT)chain);
    }
    TRACE("returning %d\n", ret);
    return ret;
}

static void CRYPT_FreeChainContext(PCertificateChain chain)
{
    DWORD i;

    /* Note the chain's rgpLowerQualityChainContext isn't freed, but
     * it's never set, either.
     */
    for (i = 0; i < chain->context.cChain; i++)
        CRYPT_FreeSimpleChain(chain->context.rgpChain[i]);
    CryptMemFree(chain->context.rgpChain);
    CryptMemFree(chain);
}

void WINAPI CertFreeCertificateChain(PCCERT_CHAIN_CONTEXT pChainContext)
{
    PCertificateChain chain = (PCertificateChain)pChainContext;

    TRACE("(%p)\n", pChainContext);

    if (chain)
    {
        if (InterlockedDecrement(&chain->ref) == 0)
            CRYPT_FreeChainContext(chain);
    }
}
