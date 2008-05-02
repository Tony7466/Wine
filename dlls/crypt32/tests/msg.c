/*
 * Unit test suite for crypt32.dll's CryptMsg functions
 *
 * Copyright 2007 Juan Lang
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

#include <stdio.h>
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <wincrypt.h>

#include "wine/test.h"

static void test_msg_open_to_encode(void)
{
    HCRYPTMSG msg;

    /* Crash
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_ENVELOPED, NULL,
     NULL, NULL);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, NULL, NULL,
     NULL);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_SIGNED, NULL, NULL,
     NULL);
     */

    /* Bad encodings */
    SetLastError(0xdeadbeef);
    msg = CryptMsgOpenToEncode(0, 0, 0, NULL, NULL, NULL);
    ok(!msg && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());
    SetLastError(0xdeadbeef);
    msg = CryptMsgOpenToEncode(X509_ASN_ENCODING, 0, 0, NULL, NULL, NULL);
    ok(!msg && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());

    /* Bad message types */
    SetLastError(0xdeadbeef);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, 0, NULL, NULL, NULL);
    ok(!msg && GetLastError() == CRYPT_E_INVALID_MSG_TYPE,
     "Expected CRYPT_E_INVALID_MSG_TYPE, got %x\n", GetLastError());
    SetLastError(0xdeadbeef);
    msg = CryptMsgOpenToEncode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0,
     NULL, NULL, NULL);
    ok(!msg && GetLastError() == CRYPT_E_INVALID_MSG_TYPE,
     "Expected CRYPT_E_INVALID_MSG_TYPE, got %x\n", GetLastError());
    SetLastError(0xdeadbeef);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0,
     CMSG_SIGNED_AND_ENVELOPED, NULL, NULL, NULL);
    ok(!msg && GetLastError() == CRYPT_E_INVALID_MSG_TYPE,
     "Expected CRYPT_E_INVALID_MSG_TYPE, got %x\n", GetLastError());
    SetLastError(0xdeadbeef);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_ENCRYPTED, NULL,
     NULL, NULL);
    ok(!msg && GetLastError() == CRYPT_E_INVALID_MSG_TYPE,
     "Expected CRYPT_E_INVALID_MSG_TYPE, got %x\n", GetLastError());
}

static void test_msg_open_to_decode(void)
{
    HCRYPTMSG msg;
    CMSG_STREAM_INFO streamInfo = { 0 };

    SetLastError(0xdeadbeef);
    msg = CryptMsgOpenToDecode(0, 0, 0, 0, NULL, NULL);
    ok(!msg && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());

    /* Bad encodings */
    SetLastError(0xdeadbeef);
    msg = CryptMsgOpenToDecode(X509_ASN_ENCODING, 0, 0, 0, NULL, NULL);
    ok(!msg && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());
    SetLastError(0xdeadbeef);
    msg = CryptMsgOpenToDecode(X509_ASN_ENCODING, 0, CMSG_DATA, 0, NULL, NULL);
    ok(!msg && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());

    /* The message type can be explicit... */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, 0, NULL,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_ENVELOPED, 0, NULL,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, 0, NULL,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_SIGNED, 0, NULL,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0,
     CMSG_SIGNED_AND_ENVELOPED, 0, NULL, NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    /* or implicit.. */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    /* or even invalid. */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_ENCRYPTED, 0, NULL,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 1000, 0, NULL, NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    CryptMsgClose(msg);

    /* And even though the stream info parameter "must be set to NULL" for
     * CMSG_HASHED, it's still accepted.
     */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, 0, NULL,
     &streamInfo);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
}

static void test_msg_get_param(void)
{
    BOOL ret;
    HCRYPTMSG msg;
    DWORD size, i, value;
    CMSG_SIGNED_ENCODE_INFO signInfo = { sizeof(signInfo), 0 };
    CMSG_SIGNER_ENCODE_INFO signer = { sizeof(signer), 0 };

    /* Crash
    ret = CryptMsgGetParam(NULL, 0, 0, NULL, NULL);
    ret = CryptMsgGetParam(NULL, 0, 0, NULL, &size);
    ret = CryptMsgGetParam(msg, 0, 0, NULL, NULL);
     */

    /* Decoded messages */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    /* For decoded messages, the type is always available */
    size = 0;
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, NULL, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    /* For this (empty) message, the type isn't set */
    ok(value == 0, "Expected type 0, got %d\n", value);
    CryptMsgClose(msg);

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, 0, NULL,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    /* For explicitly typed messages, the type is known. */
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    ok(value == CMSG_DATA, "Expected CMSG_DATA, got %d\n", value);
    for (i = CMSG_CONTENT_PARAM; i <= CMSG_CMS_SIGNER_INFO_PARAM; i++)
    {
        size = 0;
        ret = CryptMsgGetParam(msg, i, 0, NULL, &size);
        ok(!ret, "Parameter %d: expected failure\n", i);
    }
    CryptMsgClose(msg);

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_ENVELOPED, 0, NULL,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    ok(value == CMSG_ENVELOPED, "Expected CMSG_ENVELOPED, got %d\n", value);
    for (i = CMSG_CONTENT_PARAM; i <= CMSG_CMS_SIGNER_INFO_PARAM; i++)
    {
        size = 0;
        ret = CryptMsgGetParam(msg, i, 0, NULL, &size);
        ok(!ret, "Parameter %d: expected failure\n", i);
    }
    CryptMsgClose(msg);

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, 0, NULL,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    ok(value == CMSG_HASHED, "Expected CMSG_HASHED, got %d\n", value);
    for (i = CMSG_CONTENT_PARAM; i <= CMSG_CMS_SIGNER_INFO_PARAM; i++)
    {
        size = 0;
        ret = CryptMsgGetParam(msg, i, 0, NULL, &size);
        ok(!ret, "Parameter %d: expected failure\n", i);
    }
    CryptMsgClose(msg);

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_SIGNED, 0, NULL,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    ok(value == CMSG_SIGNED, "Expected CMSG_SIGNED, got %d\n", value);
    for (i = CMSG_CONTENT_PARAM; i <= CMSG_CMS_SIGNER_INFO_PARAM; i++)
    {
        size = 0;
        ret = CryptMsgGetParam(msg, i, 0, NULL, &size);
        ok(!ret, "Parameter %d: expected failure\n", i);
    }
    CryptMsgClose(msg);

    /* Explicitly typed messages get their types set, even if they're invalid */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_ENCRYPTED, 0, NULL,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    ok(value == CMSG_ENCRYPTED, "Expected CMSG_ENCRYPTED, got %d\n", value);
    CryptMsgClose(msg);

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 1000, 0, NULL, NULL);
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    ok(value == 1000, "Expected 1000, got %d\n", value);
    CryptMsgClose(msg);
}

static void test_msg_close(void)
{
    BOOL ret;
    HCRYPTMSG msg;

    /* NULL succeeds.. */
    ret = CryptMsgClose(NULL);
    ok(ret, "CryptMsgClose failed: %x\n", GetLastError());
    /* but an arbitrary pointer crashes. */
    if (0)
        ret = CryptMsgClose((HCRYPTMSG)1);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL, NULL,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToEncode failed: %x\n", GetLastError());
    ret = CryptMsgClose(msg);
    ok(ret, "CryptMsgClose failed: %x\n", GetLastError());
}

static void check_param(LPCSTR test, HCRYPTMSG msg, DWORD param,
 const BYTE *expected, DWORD expectedSize)
{
    DWORD size;
    LPBYTE buf;
    BOOL ret;

    size = 0;
    ret = CryptMsgGetParam(msg, param, 0, NULL, &size);
    ok(ret, "%s: CryptMsgGetParam failed: %08x\n", test, GetLastError());
    buf = HeapAlloc(GetProcessHeap(), 0, size);
    ret = CryptMsgGetParam(msg, param, 0, buf, &size);
    ok(ret, "%s: CryptMsgGetParam failed: %08x\n", test, GetLastError());
    ok(size == expectedSize, "%s: expected size %d, got %d\n", test,
     expectedSize, size);
    if (size)
        ok(!memcmp(buf, expected, size), "%s: unexpected data\n", test);
    HeapFree(GetProcessHeap(), 0, buf);
}

static void test_data_msg_open(void)
{
    HCRYPTMSG msg;
    CMSG_HASHED_ENCODE_INFO hashInfo = { 0 };
    CMSG_STREAM_INFO streamInfo = { 0 };
    char oid[] = "1.2.3";

    /* The data message type takes no additional info */
    SetLastError(0xdeadbeef);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, &hashInfo,
     NULL, NULL);
    ok(!msg && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL, NULL,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToEncode failed: %x\n", GetLastError());
    CryptMsgClose(msg);

    /* An empty stream info is allowed. */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL, NULL,
     &streamInfo);
    ok(msg != NULL, "CryptMsgOpenToEncode failed: %x\n", GetLastError());
    CryptMsgClose(msg);

    /* Passing a bogus inner OID succeeds for a non-streamed message.. */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL, oid,
     NULL);
    ok(msg != NULL, "CryptMsgOpenToEncode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    /* and still succeeds when CMSG_DETACHED_FLAG is passed.. */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, CMSG_DETACHED_FLAG,
     CMSG_DATA, NULL, oid, NULL);
    ok(msg != NULL, "CryptMsgOpenToEncode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    /* and when a stream info is given, even though you're not supposed to be
     * able to use anything but szOID_RSA_data when streaming is being used.
     */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, CMSG_DETACHED_FLAG,
     CMSG_DATA, NULL, oid, &streamInfo);
    ok(msg != NULL, "CryptMsgOpenToEncode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
}

static const BYTE msgData[] = { 1, 2, 3, 4 };

static BOOL WINAPI nop_stream_output(const void *pvArg, BYTE *pb, DWORD cb,
 BOOL final)
{
    return TRUE;
}

static void test_data_msg_update(void)
{
    HCRYPTMSG msg;
    BOOL ret;
    CMSG_STREAM_INFO streamInfo = { 0 };

    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL, NULL,
     NULL);
    /* Can't update a message that wasn't opened detached with final = FALSE */
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, NULL, 0, FALSE);
    ok(!ret && GetLastError() == CRYPT_E_MSG_ERROR,
     "Expected CRYPT_E_MSG_ERROR, got %x\n", GetLastError());
    /* Updating it with final = TRUE succeeds */
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    /* Any subsequent update will fail, as the last was final */
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(!ret && GetLastError() == CRYPT_E_MSG_ERROR,
     "Expected CRYPT_E_MSG_ERROR, got %x\n", GetLastError());
    CryptMsgClose(msg);

    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL, NULL,
     NULL);
    /* Can't update a message with no data */
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, NULL, 0, TRUE);
    ok(!ret && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());
    /* Curiously, a valid update will now fail as well, presumably because of
     * the last (invalid, but final) update.
     */
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(!ret && GetLastError() == CRYPT_E_MSG_ERROR,
     "Expected CRYPT_E_MSG_ERROR, got %x\n", GetLastError());
    CryptMsgClose(msg);

    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, CMSG_DETACHED_FLAG,
     CMSG_DATA, NULL, NULL, NULL);
    /* Doesn't appear to be able to update CMSG-DATA with non-final updates */
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, NULL, 0, FALSE);
    ok(!ret && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), FALSE);
    ok(!ret && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    CryptMsgClose(msg);

    /* Calling update after opening with an empty stream info (with a bogus
     * output function) yields an error:
     */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL, NULL,
     &streamInfo);
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), FALSE);
    ok(!ret && GetLastError() == STATUS_ACCESS_VIOLATION,
     "Expected STATUS_ACCESS_VIOLATION, got %x\n", GetLastError());
    CryptMsgClose(msg);
    /* Calling update with a valid output function succeeds, even if the data
     * exceeds the size specified in the stream info.
     */
    streamInfo.pfnStreamOutput = nop_stream_output;
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL, NULL,
     &streamInfo);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), FALSE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    CryptMsgClose(msg);
}

static void test_data_msg_get_param(void)
{
    HCRYPTMSG msg;
    DWORD size;
    BOOL ret;
    CMSG_STREAM_INFO streamInfo = { 0, nop_stream_output, NULL };

    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL, NULL,
     NULL);

    /* Content and bare content are always gettable when not streaming */
    size = 0;
    ret = CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, NULL, &size);
    ok(ret, "CryptMsgGetParam failed: %08x\n", GetLastError());
    size = 0;
    ret = CryptMsgGetParam(msg, CMSG_BARE_CONTENT_PARAM, 0, NULL, &size);
    ok(ret, "CryptMsgGetParam failed: %08x\n", GetLastError());
    /* But for this type of message, the signer and hash aren't applicable,
     * and the type isn't available.
     */
    size = 0;
    SetLastError(0xdeadbeef);
    ret = CryptMsgGetParam(msg, CMSG_ENCODED_SIGNER, 0, NULL, &size);
    ok(!ret && GetLastError() == CRYPT_E_INVALID_MSG_TYPE,
     "Expected CRYPT_E_INVALID_MSG_TYPE, got %x\n", GetLastError());
    SetLastError(0xdeadbeef);
    ret = CryptMsgGetParam(msg, CMSG_COMPUTED_HASH_PARAM, 0, NULL, &size);
    ok(!ret && GetLastError() == CRYPT_E_INVALID_MSG_TYPE,
     "Expected CRYPT_E_INVALID_MSG_TYPE, got %x\n", GetLastError());
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, NULL, &size);
    ok(!ret && GetLastError() == CRYPT_E_INVALID_MSG_TYPE,
     "Expected CRYPT_E_INVALID_MSG_TYPE, got %x\n", GetLastError());
    CryptMsgClose(msg);

    /* Can't get content or bare content when streaming */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL,
     NULL, &streamInfo);
    SetLastError(0xdeadbeef);
    ret = CryptMsgGetParam(msg, CMSG_BARE_CONTENT_PARAM, 0, NULL, &size);
    ok(!ret && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());
    SetLastError(0xdeadbeef);
    ret = CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, NULL, &size);
    ok(!ret && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());
    CryptMsgClose(msg);
}

static const BYTE dataEmptyBareContent[] = { 0x04,0x00 };
static const BYTE dataEmptyContent[] = {
0x30,0x0f,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x07,0x01,0xa0,0x02,
0x04,0x00 };
static const BYTE dataBareContent[] = { 0x04,0x04,0x01,0x02,0x03,0x04 };
static const BYTE dataContent[] = {
0x30,0x13,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x07,0x01,0xa0,0x06,
0x04,0x04,0x01,0x02,0x03,0x04 };

struct update_accum
{
    DWORD cUpdates;
    CRYPT_DATA_BLOB *updates;
};

static BOOL WINAPI accumulating_stream_output(const void *pvArg, BYTE *pb,
 DWORD cb, BOOL final)
{
    struct update_accum *accum = (struct update_accum *)pvArg;
    BOOL ret = FALSE;

    if (accum->cUpdates)
        accum->updates = CryptMemRealloc(accum->updates,
         (accum->cUpdates + 1) * sizeof(CRYPT_DATA_BLOB));
    else
        accum->updates = CryptMemAlloc(sizeof(CRYPT_DATA_BLOB));
    if (accum->updates)
    {
        CRYPT_DATA_BLOB *blob = &accum->updates[accum->cUpdates];

        blob->pbData = CryptMemAlloc(cb);
        if (blob->pbData)
        {
            memcpy(blob->pbData, pb, cb);
            blob->cbData = cb;
            ret = TRUE;
        }
        accum->cUpdates++;
    }
    return ret;
}

/* The updates of a (bogus) definite-length encoded message */
static BYTE u1[] = { 0x30,0x0f,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,
 0x07,0x01,0xa0,0x02,0x04,0x00 };
static BYTE u2[] = { 0x01,0x02,0x03,0x04 };
static CRYPT_DATA_BLOB b1[] = {
    { sizeof(u1), u1 },
    { sizeof(u2), u2 },
    { sizeof(u2), u2 },
};
static const struct update_accum a1 = { sizeof(b1) / sizeof(b1[0]), b1 };
/* The updates of a definite-length encoded message */
static BYTE u3[] = { 0x30,0x13,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,
 0x07,0x01,0xa0,0x06,0x04,0x04 };
static CRYPT_DATA_BLOB b2[] = {
    { sizeof(u3), u3 },
    { sizeof(u2), u2 },
};
static const struct update_accum a2 = { sizeof(b2) / sizeof(b2[0]), b2 };
/* The updates of an indefinite-length encoded message */
static BYTE u4[] = { 0x30,0x80,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,
 0x07,0x01,0xa0,0x80,0x24,0x80 };
static BYTE u5[] = { 0x04,0x04 };
static BYTE u6[] = { 0x00,0x00,0x00,0x00,0x00,0x00 };
static CRYPT_DATA_BLOB b3[] = {
    { sizeof(u4), u4 },
    { sizeof(u5), u5 },
    { sizeof(u2), u2 },
    { sizeof(u5), u5 },
    { sizeof(u2), u2 },
    { sizeof(u6), u6 },
};
static const struct update_accum a3 = { sizeof(b3) / sizeof(b3[0]), b3 };

static void check_updates(LPCSTR header, const struct update_accum *expected,
 const struct update_accum *got)
{
    DWORD i;

    ok(expected->cUpdates == got->cUpdates,
     "%s: expected %d updates, got %d\n", header, expected->cUpdates,
     got->cUpdates);
    if (expected->cUpdates == got->cUpdates)
        for (i = 0; i < min(expected->cUpdates, got->cUpdates); i++)
        {
            ok(expected->updates[i].cbData == got->updates[i].cbData,
             "%s, update %d: expected %d bytes, got %d\n", header, i,
             expected->updates[i].cbData, got->updates[i].cbData);
            if (expected->updates[i].cbData && expected->updates[i].cbData ==
             got->updates[i].cbData)
                ok(!memcmp(expected->updates[i].pbData, got->updates[i].pbData,
                 got->updates[i].cbData), "%s, update %d: unexpected value\n",
                 header, i);
        }
}

/* Frees the updates stored in accum */
static void free_updates(struct update_accum *accum)
{
    DWORD i;

    for (i = 0; i < accum->cUpdates; i++)
        CryptMemFree(accum->updates[i].pbData);
    CryptMemFree(accum->updates);
    accum->updates = NULL;
    accum->cUpdates = 0;
}

static void test_data_msg_encoding(void)
{
    HCRYPTMSG msg;
    BOOL ret;
    static char oid[] = "1.2.3";
    struct update_accum accum = { 0, NULL };
    CMSG_STREAM_INFO streamInfo = { 0, accumulating_stream_output, &accum };

    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL, NULL,
     NULL);
    check_param("data empty bare content", msg, CMSG_BARE_CONTENT_PARAM,
     dataEmptyBareContent, sizeof(dataEmptyBareContent));
    check_param("data empty content", msg, CMSG_CONTENT_PARAM, dataEmptyContent,
     sizeof(dataEmptyContent));
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    check_param("data bare content", msg, CMSG_BARE_CONTENT_PARAM,
     dataBareContent, sizeof(dataBareContent));
    check_param("data content", msg, CMSG_CONTENT_PARAM, dataContent,
     sizeof(dataContent));
    CryptMsgClose(msg);
    /* Same test, but with CMSG_BARE_CONTENT_FLAG set */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, CMSG_BARE_CONTENT_FLAG,
     CMSG_DATA, NULL, NULL, NULL);
    check_param("data empty bare content", msg, CMSG_BARE_CONTENT_PARAM,
     dataEmptyBareContent, sizeof(dataEmptyBareContent));
    check_param("data empty content", msg, CMSG_CONTENT_PARAM, dataEmptyContent,
     sizeof(dataEmptyContent));
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    check_param("data bare content", msg, CMSG_BARE_CONTENT_PARAM,
     dataBareContent, sizeof(dataBareContent));
    check_param("data content", msg, CMSG_CONTENT_PARAM, dataContent,
     sizeof(dataContent));
    CryptMsgClose(msg);
    /* The inner OID is apparently ignored */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL, oid,
     NULL);
    check_param("data bogus oid bare content", msg, CMSG_BARE_CONTENT_PARAM,
     dataEmptyBareContent, sizeof(dataEmptyBareContent));
    check_param("data bogus oid content", msg, CMSG_CONTENT_PARAM,
     dataEmptyContent, sizeof(dataEmptyContent));
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    check_param("data bare content", msg, CMSG_BARE_CONTENT_PARAM,
     dataBareContent, sizeof(dataBareContent));
    check_param("data content", msg, CMSG_CONTENT_PARAM, dataContent,
     sizeof(dataContent));
    CryptMsgClose(msg);
    /* A streaming message is DER encoded if the length is not 0xffffffff, but
     * curiously, updates aren't validated to make sure they don't exceed the
     * stated length.  (The resulting output will of course fail to decode.)
     */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL,
     NULL, &streamInfo);
    CryptMsgUpdate(msg, msgData, sizeof(msgData), FALSE);
    CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    CryptMsgClose(msg);
    check_updates("bogus data message with definite length", &a1, &accum);
    free_updates(&accum);
    /* A valid definite-length encoding: */
    streamInfo.cbContent = sizeof(msgData);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL,
     NULL, &streamInfo);
    CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    CryptMsgClose(msg);
    check_updates("data message with definite length", &a2, &accum);
    free_updates(&accum);
    /* An indefinite-length encoding: */
    streamInfo.cbContent = 0xffffffff;
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL,
     NULL, &streamInfo);
    CryptMsgUpdate(msg, msgData, sizeof(msgData), FALSE);
    CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    CryptMsgClose(msg);
    todo_wine
    check_updates("data message with indefinite length", &a3, &accum);
    free_updates(&accum);
}

static void test_data_msg(void)
{
    test_data_msg_open();
    test_data_msg_update();
    test_data_msg_get_param();
    test_data_msg_encoding();
}

static void test_hash_msg_open(void)
{
    HCRYPTMSG msg;
    CMSG_HASHED_ENCODE_INFO hashInfo = { 0 };
    static char oid_rsa_md5[] = szOID_RSA_MD5;
    CMSG_STREAM_INFO streamInfo = { 0, nop_stream_output, NULL };

    SetLastError(0xdeadbeef);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, &hashInfo,
     NULL, NULL);
    ok(!msg && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());
    hashInfo.cbSize = sizeof(hashInfo);
    SetLastError(0xdeadbeef);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, &hashInfo,
     NULL, NULL);
    ok(!msg && GetLastError() == CRYPT_E_UNKNOWN_ALGO,
     "Expected CRYPT_E_UNKNOWN_ALGO, got %x\n", GetLastError());
    hashInfo.HashAlgorithm.pszObjId = oid_rsa_md5;
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, &hashInfo,
     NULL, NULL);
    ok(msg != NULL, "CryptMsgOpenToEncode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, CMSG_DETACHED_FLAG,
     CMSG_HASHED, &hashInfo, NULL, NULL);
    ok(msg != NULL, "CryptMsgOpenToEncode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, CMSG_DETACHED_FLAG,
     CMSG_HASHED, &hashInfo, NULL, &streamInfo);
    ok(msg != NULL, "CryptMsgOpenToEncode failed: %x\n", GetLastError());
    CryptMsgClose(msg);
}

static void test_hash_msg_update(void)
{
    HCRYPTMSG msg;
    BOOL ret;
    static char oid_rsa_md5[] = szOID_RSA_MD5;
    CMSG_HASHED_ENCODE_INFO hashInfo = { sizeof(hashInfo), 0,
     { oid_rsa_md5, { 0, NULL } }, NULL };
    CMSG_STREAM_INFO streamInfo = { 0, nop_stream_output, NULL };

    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, CMSG_DETACHED_FLAG,
     CMSG_HASHED, &hashInfo, NULL, NULL);
    /* Detached hashed messages opened in non-streaming mode allow non-final
     * updates..
     */
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), FALSE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    /* including non-final updates with no data.. */
    ret = CryptMsgUpdate(msg, NULL, 0, FALSE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    /* and final updates with no data. */
    ret = CryptMsgUpdate(msg, NULL, 0, TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    /* But no updates are allowed after the final update. */
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, NULL, 0, FALSE);
    ok(!ret && GetLastError() == CRYPT_E_MSG_ERROR,
     "Expected CRYPT_E_MSG_ERROR, got %x\n", GetLastError());
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, NULL, 0, TRUE);
    ok(!ret && GetLastError() == CRYPT_E_MSG_ERROR,
     "Expected CRYPT_E_MSG_ERROR, got %x\n", GetLastError());
    CryptMsgClose(msg);
    /* Non-detached messages, in contrast, don't allow non-final updates in
     * non-streaming mode.
     */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, &hashInfo,
     NULL, NULL);
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), FALSE);
    ok(!ret && GetLastError() == CRYPT_E_MSG_ERROR,
     "Expected CRYPT_E_MSG_ERROR, got %x\n", GetLastError());
    /* Final updates (including empty ones) are allowed. */
    ret = CryptMsgUpdate(msg, NULL, 0, TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    /* And, of course, streaming mode allows non-final updates */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, &hashInfo,
     NULL, &streamInfo);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), FALSE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    /* Setting pfnStreamOutput to NULL results in no error.  (In what appears
     * to be a bug, it isn't actually used - see encoding tests.)
     */
    streamInfo.pfnStreamOutput = NULL;
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, &hashInfo,
     NULL, &streamInfo);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), FALSE);
    ok(ret, "CryptMsgUpdate failed: %08x\n", GetLastError());
    CryptMsgClose(msg);
}

static const BYTE emptyHashParam[] = {
0xd4,0x1d,0x8c,0xd9,0x8f,0x00,0xb2,0x04,0xe9,0x80,0x09,0x98,0xec,0xf8,0x42,
0x7e };

static void test_hash_msg_get_param(void)
{
    HCRYPTMSG msg;
    BOOL ret;
    static char oid_rsa_md5[] = szOID_RSA_MD5;
    CMSG_HASHED_ENCODE_INFO hashInfo = { sizeof(hashInfo), 0,
     { oid_rsa_md5, { 0, NULL } }, NULL };
    DWORD size, value;
    CMSG_STREAM_INFO streamInfo = { 0, nop_stream_output, NULL };
    BYTE buf[16];

    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, &hashInfo,
     NULL, NULL);
    /* Content and bare content are always gettable for non-streamed messages */
    size = 0;
    ret = CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, NULL, &size);
    ok(ret, "CryptMsgGetParam failed: %08x\n", GetLastError());
    size = 0;
    ret = CryptMsgGetParam(msg, CMSG_BARE_CONTENT_PARAM, 0, NULL, &size);
    ok(ret, "CryptMsgGetParam failed: %08x\n", GetLastError());
    /* The hash is also available. */
    size = 0;
    ret = CryptMsgGetParam(msg, CMSG_COMPUTED_HASH_PARAM, 0, NULL, &size);
    ok(ret, "CryptMsgGetParam failed: %08x\n", GetLastError());
    ok(size == sizeof(buf), "Unexpected size %d\n", size);
    ret = CryptMsgGetParam(msg, CMSG_COMPUTED_HASH_PARAM, 0, buf, &size);
    if (size == sizeof(buf))
        ok(!memcmp(buf, emptyHashParam, size), "Unexpected value\n");
    /* By getting the hash, further updates are not allowed */
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(!ret && GetLastError() == NTE_BAD_HASH_STATE,
     "Expected NTE_BAD_HASH_STATE, got %x\n", GetLastError());
    /* The version is also available, and should be zero for this message. */
    size = 0;
    ret = CryptMsgGetParam(msg, CMSG_VERSION_PARAM, 0, NULL, &size);
    ok(ret, "CryptMsgGetParam failed: %08x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_VERSION_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %08x\n", GetLastError());
    ok(value == 0, "Expected version 0, got %d\n", value);
    /* As usual, the type isn't available. */
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, NULL, &size);
    ok(!ret, "Expected failure\n");
    CryptMsgClose(msg);

    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, &hashInfo,
     NULL, &streamInfo);
    /* Streamed messages don't allow you to get the content or bare content. */
    SetLastError(0xdeadbeef);
    ret = CryptMsgGetParam(msg, CMSG_CONTENT_PARAM, 0, NULL, &size);
    ok(!ret && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());
    SetLastError(0xdeadbeef);
    ret = CryptMsgGetParam(msg, CMSG_BARE_CONTENT_PARAM, 0, NULL, &size);
    ok(!ret && GetLastError() == E_INVALIDARG,
     "Expected E_INVALIDARG, got %x\n", GetLastError());
    /* The hash is still available. */
    size = 0;
    ret = CryptMsgGetParam(msg, CMSG_COMPUTED_HASH_PARAM, 0, NULL, &size);
    ok(ret, "CryptMsgGetParam failed: %08x\n", GetLastError());
    ok(size == sizeof(buf), "Unexpected size %d\n", size);
    ret = CryptMsgGetParam(msg, CMSG_COMPUTED_HASH_PARAM, 0, buf, &size);
    ok(ret, "CryptMsgGetParam failed: %08x\n", GetLastError());
    if (size == sizeof(buf))
        ok(!memcmp(buf, emptyHashParam, size), "Unexpected value\n");
    /* After updating the hash, further updates aren't allowed on streamed
     * messages either.
     */
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(!ret && GetLastError() == NTE_BAD_HASH_STATE,
     "Expected NTE_BAD_HASH_STATE, got %x\n", GetLastError());
    CryptMsgClose(msg);
}

static const BYTE hashEmptyBareContent[] = {
0x30,0x17,0x02,0x01,0x00,0x30,0x0c,0x06,0x08,0x2a,0x86,0x48,0x86,0xf7,0x0d,
0x02,0x05,0x05,0x00,0x30,0x02,0x06,0x00,0x04,0x00 };
static const BYTE hashEmptyContent[] = {
0x30,0x26,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x07,0x05,0xa0,0x19,
0x30,0x17,0x02,0x01,0x00,0x30,0x0c,0x06,0x08,0x2a,0x86,0x48,0x86,0xf7,0x0d,
0x02,0x05,0x05,0x00,0x30,0x02,0x06,0x00,0x04,0x00 };
static const BYTE hashBareContent[] = {
0x30,0x38,0x02,0x01,0x00,0x30,0x0c,0x06,0x08,0x2a,0x86,0x48,0x86,0xf7,0x0d,
0x02,0x05,0x05,0x00,0x30,0x13,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,
0x07,0x01,0xa0,0x06,0x04,0x04,0x01,0x02,0x03,0x04,0x04,0x10,0x08,0xd6,0xc0,
0x5a,0x21,0x51,0x2a,0x79,0xa1,0xdf,0xeb,0x9d,0x2a,0x8f,0x26,0x2f };
static const BYTE hashContent[] = {
0x30,0x47,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x07,0x05,0xa0,0x3a,
0x30,0x38,0x02,0x01,0x00,0x30,0x0c,0x06,0x08,0x2a,0x86,0x48,0x86,0xf7,0x0d,
0x02,0x05,0x05,0x00,0x30,0x13,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,
0x07,0x01,0xa0,0x06,0x04,0x04,0x01,0x02,0x03,0x04,0x04,0x10,0x08,0xd6,0xc0,
0x5a,0x21,0x51,0x2a,0x79,0xa1,0xdf,0xeb,0x9d,0x2a,0x8f,0x26,0x2f };

static const BYTE detachedHashNonFinalBareContent[] = {
0x30,0x20,0x02,0x01,0x00,0x30,0x0c,0x06,0x08,0x2a,0x86,0x48,0x86,0xf7,0x0d,
0x02,0x05,0x05,0x00,0x30,0x0b,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,
0x07,0x01,0x04,0x00 };
static const BYTE detachedHashNonFinalContent[] = {
0x30,0x2f,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x07,0x05,0xa0,0x22,
0x30,0x20,0x02,0x01,0x00,0x30,0x0c,0x06,0x08,0x2a,0x86,0x48,0x86,0xf7,0x0d,
0x02,0x05,0x05,0x00,0x30,0x0b,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,
0x07,0x01,0x04,0x00 };
static const BYTE detachedHashBareContent[] = {
0x30,0x30,0x02,0x01,0x00,0x30,0x0c,0x06,0x08,0x2a,0x86,0x48,0x86,0xf7,0x0d,
0x02,0x05,0x05,0x00,0x30,0x0b,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,
0x07,0x01,0x04,0x10,0x08,0xd6,0xc0,0x5a,0x21,0x51,0x2a,0x79,0xa1,0xdf,0xeb,
0x9d,0x2a,0x8f,0x26,0x2f };
static const BYTE detachedHashContent[] = {
0x30,0x3f,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x07,0x05,0xa0,0x32,
0x30,0x30,0x02,0x01,0x00,0x30,0x0c,0x06,0x08,0x2a,0x86,0x48,0x86,0xf7,0x0d,
0x02,0x05,0x05,0x00,0x30,0x0b,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,
0x07,0x01,0x04,0x10,0x08,0xd6,0xc0,0x5a,0x21,0x51,0x2a,0x79,0xa1,0xdf,0xeb,
0x9d,0x2a,0x8f,0x26,0x2f };

static void test_hash_msg_encoding(void)
{
    HCRYPTMSG msg;
    CMSG_HASHED_ENCODE_INFO hashInfo = { sizeof(hashInfo), 0 };
    BOOL ret;
    struct update_accum accum = { 0, NULL }, empty_accum = { 0, NULL };
    CMSG_STREAM_INFO streamInfo = { 0, accumulating_stream_output, &accum };
    static char oid_rsa_md5[] = szOID_RSA_MD5;

    hashInfo.HashAlgorithm.pszObjId = oid_rsa_md5;
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, &hashInfo,
     NULL, NULL);
    check_param("hash empty bare content", msg, CMSG_BARE_CONTENT_PARAM,
     hashEmptyBareContent, sizeof(hashEmptyBareContent));
    check_param("hash empty content", msg, CMSG_CONTENT_PARAM,
     hashEmptyContent, sizeof(hashEmptyContent));
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    check_param("hash bare content", msg, CMSG_BARE_CONTENT_PARAM,
     hashBareContent, sizeof(hashBareContent));
    check_param("hash content", msg, CMSG_CONTENT_PARAM,
     hashContent, sizeof(hashContent));
    CryptMsgClose(msg);
    /* Same test, but with CMSG_BARE_CONTENT_FLAG set */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, CMSG_BARE_CONTENT_FLAG,
     CMSG_HASHED, &hashInfo, NULL, NULL);
    check_param("hash empty bare content", msg, CMSG_BARE_CONTENT_PARAM,
     hashEmptyBareContent, sizeof(hashEmptyBareContent));
    check_param("hash empty content", msg, CMSG_CONTENT_PARAM,
     hashEmptyContent, sizeof(hashEmptyContent));
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    check_param("hash bare content", msg, CMSG_BARE_CONTENT_PARAM,
     hashBareContent, sizeof(hashBareContent));
    check_param("hash content", msg, CMSG_CONTENT_PARAM,
     hashContent, sizeof(hashContent));
    CryptMsgClose(msg);
    /* Same test, but with CMSG_DETACHED_FLAG set */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, CMSG_DETACHED_FLAG,
     CMSG_HASHED, &hashInfo, NULL, NULL);
    check_param("detached hash empty bare content", msg,
     CMSG_BARE_CONTENT_PARAM, hashEmptyBareContent,
     sizeof(hashEmptyBareContent));
    check_param("detached hash empty content", msg, CMSG_CONTENT_PARAM,
     hashEmptyContent, sizeof(hashEmptyContent));
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), FALSE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    check_param("detached hash not final bare content", msg,
     CMSG_BARE_CONTENT_PARAM, detachedHashNonFinalBareContent,
     sizeof(detachedHashNonFinalBareContent));
    check_param("detached hash not final content", msg, CMSG_CONTENT_PARAM,
     detachedHashNonFinalContent, sizeof(detachedHashNonFinalContent));
    ret = CryptMsgUpdate(msg, NULL, 0, TRUE);
    ok(ret, "CryptMsgUpdate failed: %08x\n", GetLastError());
    check_param("detached hash bare content", msg, CMSG_BARE_CONTENT_PARAM,
     detachedHashBareContent, sizeof(detachedHashBareContent));
    check_param("detached hash content", msg, CMSG_CONTENT_PARAM,
     detachedHashContent, sizeof(detachedHashContent));
    check_param("detached hash bare content", msg, CMSG_BARE_CONTENT_PARAM,
     detachedHashBareContent, sizeof(detachedHashBareContent));
    check_param("detached hash content", msg, CMSG_CONTENT_PARAM,
     detachedHashContent, sizeof(detachedHashContent));
    CryptMsgClose(msg);
    /* In what appears to be a bug, streamed updates to hash messages don't
     * call the output function.
     */
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, &hashInfo,
     NULL, &streamInfo);
    ret = CryptMsgUpdate(msg, NULL, 0, FALSE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    ret = CryptMsgUpdate(msg, NULL, 0, TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    check_updates("empty hash message", &empty_accum, &accum);
    free_updates(&accum);

    streamInfo.cbContent = sizeof(msgData);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, &hashInfo,
     NULL, &streamInfo);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    check_updates("hash message", &empty_accum, &accum);
    free_updates(&accum);

    streamInfo.cbContent = sizeof(msgData);
    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, CMSG_DETACHED_FLAG,
     CMSG_HASHED, &hashInfo, NULL, &streamInfo);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    CryptMsgClose(msg);
    check_updates("detached hash message", &empty_accum, &accum);
    free_updates(&accum);
}

static void test_hash_msg(void)
{
    test_hash_msg_open();
    test_hash_msg_update();
    test_hash_msg_get_param();
    test_hash_msg_encoding();
}

static CRYPT_DATA_BLOB b4 = { 0, NULL };
static const struct update_accum a4 = { 1, &b4 };

static const BYTE bogusOIDContent[] = {
0x30,0x0f,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x07,0x07,0xa0,0x02,
0x04,0x00 };

static void test_decode_msg_update(void)
{
    HCRYPTMSG msg;
    BOOL ret;
    CMSG_STREAM_INFO streamInfo = { 0 };
    DWORD i;
    struct update_accum accum = { 0, NULL };

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
    /* Update with a full message in a final update */
    ret = CryptMsgUpdate(msg, dataEmptyContent, sizeof(dataEmptyContent), TRUE);
    todo_wine
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    /* Can't update after a final update */
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, dataEmptyContent, sizeof(dataEmptyContent), TRUE);
    ok(!ret && GetLastError() == CRYPT_E_MSG_ERROR,
     "Expected CRYPT_E_MSG_ERROR, got %x\n", GetLastError());
    CryptMsgClose(msg);

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
    /* Can't send a non-final update without streaming */
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, dataEmptyContent, sizeof(dataEmptyContent),
     FALSE);
    todo_wine
    ok(!ret && GetLastError() == CRYPT_E_MSG_ERROR,
     "Expected CRYPT_E_MSG_ERROR, got %x\n", GetLastError());
    /* A subsequent final update succeeds */
    ret = CryptMsgUpdate(msg, dataEmptyContent, sizeof(dataEmptyContent), TRUE);
    todo_wine
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    CryptMsgClose(msg);

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, &streamInfo);
    /* Updating a message that has a NULL stream callback fails */
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, dataEmptyContent, sizeof(dataEmptyContent),
     FALSE);
    todo_wine
    ok(!ret && GetLastError() == STATUS_ACCESS_VIOLATION,
     "Expected STATUS_ACCESS_VIOLATION, got %x\n", GetLastError());
    /* Changing the callback pointer after the fact yields the same error (so
     * the message must copy the stream info, not just store a pointer to it)
     */
    streamInfo.pfnStreamOutput = nop_stream_output;
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, dataEmptyContent, sizeof(dataEmptyContent),
     FALSE);
    todo_wine
    ok(!ret && GetLastError() == STATUS_ACCESS_VIOLATION,
     "Expected STATUS_ACCESS_VIOLATION, got %x\n", GetLastError());
    CryptMsgClose(msg);

    /* Empty non-final updates are allowed when streaming.. */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, &streamInfo);
    ret = CryptMsgUpdate(msg, NULL, 0, FALSE);
    todo_wine
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    /* but final updates aren't when not enough data has been received. */
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, NULL, 0, TRUE);
    todo_wine
    ok(!ret && GetLastError() == CRYPT_E_STREAM_INSUFFICIENT_DATA,
     "Expected CRYPT_E_STREAM_INSUFFICIENT_DATA, got %x\n", GetLastError());
    CryptMsgClose(msg);

    /* Updating the message byte by byte is legal */
    streamInfo.pfnStreamOutput = accumulating_stream_output;
    streamInfo.pvArg = &accum;
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, &streamInfo);
    for (i = 0, ret = TRUE; ret && i < sizeof(dataEmptyContent); i++)
        ret = CryptMsgUpdate(msg, &dataEmptyContent[i], 1, FALSE);
    todo_wine {
    ok(ret, "CryptMsgUpdate failed on byte %d: %x\n", i, GetLastError());
    ret = CryptMsgUpdate(msg, NULL, 0, TRUE);
    ok(ret, "CryptMsgUpdate failed on byte %d: %x\n", i, GetLastError());
    }
    CryptMsgClose(msg);
    todo_wine
    check_updates("byte-by-byte empty content", &a4, &accum);
    free_updates(&accum);

    /* Decoding bogus content fails in non-streaming mode.. */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    todo_wine
    ok(!ret && GetLastError() == CRYPT_E_ASN1_BADTAG,
     "Expected CRYPT_E_ASN1_BADTAG, got %x\n", GetLastError());
    CryptMsgClose(msg);
    /* and as the final update in streaming mode.. */
    streamInfo.pfnStreamOutput = nop_stream_output;
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, &streamInfo);
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), TRUE);
    todo_wine
    ok(!ret && GetLastError() == CRYPT_E_ASN1_BADTAG,
     "Expected CRYPT_E_ASN1_BADTAG, got %x\n", GetLastError());
    CryptMsgClose(msg);
    /* and even as a non-final update in streaming mode. */
    streamInfo.pfnStreamOutput = nop_stream_output;
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, &streamInfo);
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, msgData, sizeof(msgData), FALSE);
    todo_wine
    ok(!ret && GetLastError() == CRYPT_E_ASN1_BADTAG,
     "Expected CRYPT_E_ASN1_BADTAG, got %x\n", GetLastError());
    CryptMsgClose(msg);

    /* An empty message can be opened with indetermined type.. */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
    ret = CryptMsgUpdate(msg, dataEmptyContent, sizeof(dataEmptyContent),
     TRUE);
    todo_wine
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    /* but decoding it as an explicitly typed message fails. */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, 0, NULL,
     NULL);
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, dataEmptyContent, sizeof(dataEmptyContent),
     TRUE);
    todo_wine
    ok(!ret && GetLastError() == CRYPT_E_ASN1_BADTAG,
     "Expected CRYPT_E_ASN1_BADTAG, got %x\n", GetLastError());
    CryptMsgClose(msg);
    /* On the other hand, decoding the bare content of an empty message fails
     * with unspecified type..
     */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, dataEmptyBareContent,
     sizeof(dataEmptyBareContent), TRUE);
    todo_wine
    ok(!ret && GetLastError() == CRYPT_E_ASN1_BADTAG,
     "Expected CRYPT_E_ASN1_BADTAG, got %x\n", GetLastError());
    CryptMsgClose(msg);
    /* but succeeds with explicit type. */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, 0, NULL,
     NULL);
    ret = CryptMsgUpdate(msg, dataEmptyBareContent,
     sizeof(dataEmptyBareContent), TRUE);
    todo_wine
    ok(ret, "CryptMsgUpdate failed: %x\n", GetLastError());
    CryptMsgClose(msg);

    /* Decoding valid content with an unsupported OID fails */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
    SetLastError(0xdeadbeef);
    ret = CryptMsgUpdate(msg, bogusOIDContent, sizeof(bogusOIDContent), TRUE);
    todo_wine
    ok(!ret && GetLastError() == CRYPT_E_INVALID_MSG_TYPE,
     "Expected CRYPT_E_INVALID_MSG_TYPE, got %x\n", GetLastError());
    CryptMsgClose(msg);
}

static void test_decode_msg(void)
{
    test_decode_msg_update();
}

START_TEST(msg)
{
    /* Basic parameter checking tests */
    test_msg_open_to_encode();
    test_msg_open_to_decode();
    test_msg_get_param();
    test_msg_close();

    /* Message-type specific tests */
    test_data_msg();
    test_hash_msg();
    test_decode_msg();
}
