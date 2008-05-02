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
    todo_wine {
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
    }
    CryptMsgClose(msg);

    /* And even though the stream info parameter "must be set to NULL" for
     * CMSG_HASHED, it's still accepted.
     */
    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, 0, NULL,
     &streamInfo);
    todo_wine
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
    todo_wine
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    /* For decoded messages, the type is always available */
    size = 0;
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, NULL, &size);
    todo_wine {
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    /* For this (empty) message, the type isn't set */
    ok(value == 0, "Expected type 0, got %d\n", value);
    }
    CryptMsgClose(msg);

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, 0, NULL,
     NULL);
    todo_wine
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    /* For explicitly typed messages, the type is known. */
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    todo_wine {
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    ok(value == CMSG_DATA, "Expected CMSG_DATA, got %d\n", value);
    }
    for (i = CMSG_CONTENT_PARAM; i <= CMSG_CMS_SIGNER_INFO_PARAM; i++)
    {
        size = 0;
        ret = CryptMsgGetParam(msg, i, 0, NULL, &size);
        ok(!ret, "Parameter %d: expected failure\n", i);
    }
    CryptMsgClose(msg);

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_ENVELOPED, 0, NULL,
     NULL);
    todo_wine {
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    ok(value == CMSG_ENVELOPED, "Expected CMSG_ENVELOPED, got %d\n", value);
    }
    for (i = CMSG_CONTENT_PARAM; i <= CMSG_CMS_SIGNER_INFO_PARAM; i++)
    {
        size = 0;
        ret = CryptMsgGetParam(msg, i, 0, NULL, &size);
        ok(!ret, "Parameter %d: expected failure\n", i);
    }
    CryptMsgClose(msg);

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_HASHED, 0, NULL,
     NULL);
    todo_wine {
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    ok(value == CMSG_HASHED, "Expected CMSG_HASHED, got %d\n", value);
    }
    for (i = CMSG_CONTENT_PARAM; i <= CMSG_CMS_SIGNER_INFO_PARAM; i++)
    {
        size = 0;
        ret = CryptMsgGetParam(msg, i, 0, NULL, &size);
        ok(!ret, "Parameter %d: expected failure\n", i);
    }
    CryptMsgClose(msg);

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, CMSG_SIGNED, 0, NULL,
     NULL);
    todo_wine {
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    ok(value == CMSG_SIGNED, "Expected CMSG_SIGNED, got %d\n", value);
    }
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
    todo_wine {
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    ok(value == CMSG_ENCRYPTED, "Expected CMSG_ENCRYPTED, got %d\n", value);
    }
    CryptMsgClose(msg);

    msg = CryptMsgOpenToDecode(PKCS_7_ASN_ENCODING, 0, 1000, 0, NULL, NULL);
    todo_wine {
    ok(msg != NULL, "CryptMsgOpenToDecode failed: %x\n", GetLastError());
    size = sizeof(value);
    ret = CryptMsgGetParam(msg, CMSG_TYPE_PARAM, 0, (LPBYTE)&value, &size);
    ok(ret, "CryptMsgGetParam failed: %x\n", GetLastError());
    ok(value == 1000, "Expected 1000, got %d\n", value);
    }
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
}

static const BYTE msgData[] = { 1, 2, 3, 4 };

static void test_data_msg_update(void)
{
    HCRYPTMSG msg;
    BOOL ret;

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
    /* Dont appear to be able to update CMSG-DATA with non-final updates */
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
}

static void test_data_msg_get_param(void)
{
    HCRYPTMSG msg;
    DWORD size;
    BOOL ret;

    msg = CryptMsgOpenToEncode(PKCS_7_ASN_ENCODING, 0, CMSG_DATA, NULL, NULL,
     NULL);

    /* Content and bare content are always gettable */
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
}

static const BYTE dataEmptyBareContent[] = { 0x04,0x00 };
static const BYTE dataEmptyContent[] = {
0x30,0x0f,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x07,0x01,0xa0,0x02,
0x04,0x00 };
static const BYTE dataBareContent[] = { 0x04,0x04,0x01,0x02,0x03,0x04 };
static const BYTE dataContent[] = {
0x30,0x13,0x06,0x09,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x07,0x01,0xa0,0x06,
0x04,0x04,0x01,0x02,0x03,0x04 };

static void test_data_msg_encoding(void)
{
    HCRYPTMSG msg;
    BOOL ret;

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
}

static void test_data_msg(void)
{
    test_data_msg_open();
    test_data_msg_update();
    test_data_msg_get_param();
    test_data_msg_encoding();
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
}
