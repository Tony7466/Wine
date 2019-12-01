/*
 * Memory allocator unit tests
 *
 * Copyright (C) 2005 Christian Costa
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

#define COBJMACROS
#include "dshow.h"
#include "wine/test.h"

static IMemAllocator *create_allocator(void)
{
    IMemAllocator *allocator = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_MemoryAllocator, NULL, CLSCTX_INPROC_SERVER,
        &IID_IMemAllocator, (void **)&allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    return allocator;
}

static void test_commit(void)
{
    ALLOCATOR_PROPERTIES req_props = {2, 65536, 1, 0}, ret_props;
    IMemAllocator *allocator = create_allocator();
    IMediaSample *sample, *sample2;
    HRESULT hr;
    BYTE *data;

    hr = IMemAllocator_SetProperties(allocator, &req_props, &ret_props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    ok(hr == VFW_E_NOT_COMMITTED, "Got hr %#x.\n", hr);

    hr = IMemAllocator_Commit(allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMemAllocator_Commit(allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    IMediaSample_Release(sample);

    hr = IMemAllocator_Decommit(allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMemAllocator_Decommit(allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    /* Extant samples remain valid even after Decommit() is called. */
    hr = IMemAllocator_Commit(allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetPointer(sample, &data);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMemAllocator_Decommit(allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    memset(data, 0xcc, 65536);

    hr = IMemAllocator_GetBuffer(allocator, &sample2, NULL, NULL, 0);
    ok(hr == VFW_E_NOT_COMMITTED, "Got hr %#x.\n", hr);

    IMediaSample_Release(sample);
    IMemAllocator_Release(allocator);
}

static void test_sample_time(void)
{
    ALLOCATOR_PROPERTIES req_props = {1, 65536, 1, 0}, ret_props;
    IMemAllocator *allocator = create_allocator();
    AM_SAMPLE2_PROPERTIES props = {sizeof(props)};
    REFERENCE_TIME start, end;
    IMediaSample2 *sample2;
    IMediaSample *sample;
    HRESULT hr;

    hr = IMemAllocator_SetProperties(allocator, &req_props, &ret_props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMemAllocator_Commit(allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_QueryInterface(sample, &IID_IMediaSample2, (void **)&sample2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    start = 0xdeadbeef;
    end = 0xdeadf00d;
    hr = IMediaSample_GetTime(sample, &start, &end);
    ok(hr == VFW_E_SAMPLE_TIME_NOT_SET, "Got hr %#x.\n", hr);
    ok(start == 0xdeadbeef, "Got start %s.\n", wine_dbgstr_longlong(start));
    ok(end == 0xdeadf00d, "Got end %s.\n", wine_dbgstr_longlong(end));

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);

    hr = IMediaSample_SetTime(sample, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetTime(sample, &start, &end);
    ok(hr == VFW_E_SAMPLE_TIME_NOT_SET, "Got hr %#x.\n", hr);

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);

    start = 0x123;
    hr = IMediaSample_SetTime(sample, &start, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    start = end = 0;
    hr = IMediaSample_GetTime(sample, &start, &end);
    ok(hr == VFW_S_NO_STOP_TIME, "Got hr %#x.\n", hr);
    ok(start == 0x123, "Got start %s.\n", wine_dbgstr_longlong(start));
    ok(end == 0x124, "Got end %s.\n", wine_dbgstr_longlong(end));

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(props.dwSampleFlags == AM_SAMPLE_TIMEVALID, "Got flags %#x.\n", props.dwSampleFlags);
    ok(props.tStart == 0x123, "Got start %s.\n", wine_dbgstr_longlong(props.tStart));

    hr = IMediaSample_SetTime(sample, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetTime(sample, &start, &end);
    ok(hr == VFW_E_SAMPLE_TIME_NOT_SET, "Got hr %#x.\n", hr);

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);

    end = 0x321;
    hr = IMediaSample_SetTime(sample, NULL, &end);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetTime(sample, &start, &end);
    ok(hr == VFW_E_SAMPLE_TIME_NOT_SET, "Got hr %#x.\n", hr);

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    todo_wine ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);

    start = 0x123;
    end = 0x321;
    hr = IMediaSample_SetTime(sample, &start, &end);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    start = end = 0;
    hr = IMediaSample_GetTime(sample, &start, &end);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(start == 0x123, "Got start %s.\n", wine_dbgstr_longlong(start));
    ok(end == 0x321, "Got end %s.\n", wine_dbgstr_longlong(end));

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(props.dwSampleFlags == (AM_SAMPLE_TIMEVALID | AM_SAMPLE_STOPVALID),
            "Got flags %#x.\n", props.dwSampleFlags);
    ok(props.tStart == 0x123, "Got start %s.\n", wine_dbgstr_longlong(props.tStart));
    ok(props.tStop == 0x321, "Got end %s.\n", wine_dbgstr_longlong(props.tStop));

    props.dwSampleFlags = 0;
    hr = IMediaSample2_SetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetTime(sample, &start, &end);
    ok(hr == VFW_E_SAMPLE_TIME_NOT_SET, "Got hr %#x.\n", hr);

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);

    props.dwSampleFlags = AM_SAMPLE_TIMEVALID;
    props.tStart = 0x123;
    hr = IMediaSample2_SetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    start = end = 0;
    hr = IMediaSample_GetTime(sample, &start, &end);
    ok(hr == VFW_S_NO_STOP_TIME, "Got hr %#x.\n", hr);
    ok(start == 0x123, "Got start %s.\n", wine_dbgstr_longlong(start));
    ok(end == 0x124, "Got end %s.\n", wine_dbgstr_longlong(end));

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(props.dwSampleFlags == AM_SAMPLE_TIMEVALID, "Got flags %#x.\n", props.dwSampleFlags);
    ok(props.tStart == 0x123, "Got start %s.\n", wine_dbgstr_longlong(props.tStart));

    props.dwSampleFlags = AM_SAMPLE_TIMEVALID | AM_SAMPLE_STOPVALID;
    props.tStart = 0x1234;
    props.tStop = 0x4321;
    hr = IMediaSample2_SetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    start = end = 0;
    hr = IMediaSample_GetTime(sample, &start, &end);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(start == 0x1234, "Got start %s.\n", wine_dbgstr_longlong(start));
    ok(end == 0x4321, "Got end %s.\n", wine_dbgstr_longlong(end));

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(props.dwSampleFlags == (AM_SAMPLE_TIMEVALID | AM_SAMPLE_STOPVALID),
            "Got flags %#x.\n", props.dwSampleFlags);
    ok(props.tStart == 0x1234, "Got start %s.\n", wine_dbgstr_longlong(props.tStart));
    ok(props.tStop == 0x4321, "Got end %s.\n", wine_dbgstr_longlong(props.tStop));

    IMediaSample2_Release(sample2);
    IMediaSample_Release(sample);

    start = 0x123;
    end = 0x321;
    hr = IMemAllocator_GetBuffer(allocator, &sample, &start, &end, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_QueryInterface(sample, &IID_IMediaSample2, (void **)&sample2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    start = 0xdeadbeef;
    end = 0xdeadf00d;
    hr = IMediaSample_GetTime(sample, &start, &end);
    ok(hr == VFW_E_SAMPLE_TIME_NOT_SET, "Got hr %#x.\n", hr);
    ok(start == 0xdeadbeef, "Got start %s.\n", wine_dbgstr_longlong(start));
    ok(end == 0xdeadf00d, "Got end %s.\n", wine_dbgstr_longlong(end));

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);

    start = 0x123;
    end = 0x321;
    hr = IMediaSample_SetMediaTime(sample, &start, &end);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetTime(sample, &start, &end);
    ok(hr == VFW_E_SAMPLE_TIME_NOT_SET, "Got hr %#x.\n", hr);

    IMediaSample2_Release(sample2);
    IMediaSample_Release(sample);
    IMemAllocator_Release(allocator);
}

static void test_media_time(void)
{
    ALLOCATOR_PROPERTIES req_props = {1, 65536, 1, 0}, ret_props;
    IMemAllocator *allocator = create_allocator();
    AM_SAMPLE2_PROPERTIES props = {sizeof(props)};
    IMediaSample *sample;
    LONGLONG start, end;
    HRESULT hr;

    hr = IMemAllocator_SetProperties(allocator, &req_props, &ret_props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMemAllocator_Commit(allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    start = 0xdeadbeef;
    end = 0xdeadf00d;
    hr = IMediaSample_GetMediaTime(sample, &start, &end);
    ok(hr == VFW_E_MEDIA_TIME_NOT_SET, "Got hr %#x.\n", hr);
    ok(start == 0xdeadbeef, "Got start %s.\n", wine_dbgstr_longlong(start));
    ok(end == 0xdeadf00d, "Got end %s.\n", wine_dbgstr_longlong(end));

    hr = IMediaSample_SetMediaTime(sample, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetMediaTime(sample, &start, &end);
    ok(hr == VFW_E_MEDIA_TIME_NOT_SET, "Got hr %#x.\n", hr);

    /* This crashes on quartz.dll < 6.6. */
    if (0)
    {
        start = 0x123;
        hr = IMediaSample_SetMediaTime(sample, &start, NULL);
        todo_wine ok(hr == E_POINTER, "Got hr %#x.\n", hr);
    }

    end = 0x321;
    hr = IMediaSample_SetMediaTime(sample, NULL, &end);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetMediaTime(sample, &start, &end);
    ok(hr == VFW_E_MEDIA_TIME_NOT_SET, "Got hr %#x.\n", hr);

    start = 0x123;
    end = 0x321;
    hr = IMediaSample_SetMediaTime(sample, &start, &end);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    start = end = 0;
    hr = IMediaSample_GetMediaTime(sample, &start, &end);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(start == 0x123, "Got start %s.\n", wine_dbgstr_longlong(start));
    ok(end == 0x321, "Got end %s.\n", wine_dbgstr_longlong(end));

    IMediaSample_Release(sample);

    start = 0x123;
    end = 0x321;
    hr = IMemAllocator_GetBuffer(allocator, &sample, &start, &end, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    start = 0xdeadbeef;
    end = 0xdeadf00d;
    hr = IMediaSample_GetMediaTime(sample, &start, &end);
    ok(hr == VFW_E_MEDIA_TIME_NOT_SET, "Got hr %#x.\n", hr);
    ok(start == 0xdeadbeef, "Got start %s.\n", wine_dbgstr_longlong(start));
    ok(end == 0xdeadf00d, "Got end %s.\n", wine_dbgstr_longlong(end));

    start = 0x123;
    end = 0x321;
    hr = IMediaSample_SetTime(sample, &start, &end);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetMediaTime(sample, &start, &end);
    ok(hr == VFW_E_MEDIA_TIME_NOT_SET, "Got hr %#x.\n", hr);

    start = 0x123;
    end = 0x321;
    hr = IMediaSample_SetMediaTime(sample, &start, &end);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    IMediaSample_Release(sample);

    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetMediaTime(sample, &start, &end);
    ok(hr == VFW_E_MEDIA_TIME_NOT_SET, "Got hr %#x.\n", hr);

    IMediaSample_Release(sample);
    IMemAllocator_Release(allocator);
}

static void test_sample_properties(void)
{
    ALLOCATOR_PROPERTIES req_props = {1, 65536, 1, 0}, ret_props;
    IMemAllocator *allocator = create_allocator();
    AM_SAMPLE2_PROPERTIES props = {sizeof(props)};
    AM_MEDIA_TYPE *mt, expect_mt;
    IMediaSample2 *sample2;
    IMediaSample *sample;
    HRESULT hr;
    BYTE *data;
    LONG len;

    memset(&expect_mt, 0xcc, sizeof(expect_mt));
    expect_mt.pUnk = NULL;
    expect_mt.cbFormat = 0;
    expect_mt.pbFormat = NULL;

    hr = IMemAllocator_SetProperties(allocator, &req_props, &ret_props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMemAllocator_Commit(allocator);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_QueryInterface(sample, &IID_IMediaSample2, (void **)&sample2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetPointer(sample, &data);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!props.dwTypeSpecificFlags, "Got type-specific flags %#x.\n", props.dwTypeSpecificFlags);
    ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);
    ok(props.lActual == 65536, "Got actual length %d.\n", props.lActual);
    ok(!props.tStart, "Got sample start %s.\n", wine_dbgstr_longlong(props.tStart));
    ok(!props.dwStreamId, "Got stream ID %#x.\n", props.dwStreamId);
    ok(!props.pMediaType, "Got media type %p.\n", props.pMediaType);
    ok(props.pbBuffer == data, "Expected pointer %p, got %p.\n", data, props.pbBuffer);
    ok(props.cbBuffer == 65536, "Got buffer length %d.\n", props.cbBuffer);

    /* media type */

    mt = (AM_MEDIA_TYPE *)0xdeadbeef;
    hr = IMediaSample_GetMediaType(sample, &mt);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(!mt, "Got media type %p.\n", mt);

    hr = IMediaSample_SetMediaType(sample, NULL);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

    mt = (AM_MEDIA_TYPE *)0xdeadbeef;
    hr = IMediaSample_GetMediaType(sample, &mt);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(!mt, "Got media type %p.\n", mt);

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);
    ok(!props.pMediaType, "Got media type %p.\n", props.pMediaType);

    hr = IMediaSample_SetMediaType(sample, &expect_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample_GetMediaType(sample, &mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!memcmp(mt, &expect_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");
    CoTaskMemFree(mt);

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    todo_wine ok(props.dwSampleFlags == AM_SAMPLE_TYPECHANGED, "Got flags %#x.\n", props.dwSampleFlags);
    ok(!memcmp(props.pMediaType, &expect_mt, sizeof(AM_MEDIA_TYPE)), "Media types didn't match.\n");

    hr = IMediaSample_SetMediaType(sample, NULL);
    todo_wine ok(hr == S_OK, "Got hr %#x.\n", hr);

    mt = (AM_MEDIA_TYPE *)0xdeadbeef;
    hr = IMediaSample_GetMediaType(sample, &mt);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    ok(!mt, "Got media type %p.\n", mt);

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);
    ok(!props.pMediaType, "Got media type %p.\n", props.pMediaType);

    /* actual length */

    len = IMediaSample_GetActualDataLength(sample);
    ok(len == 65536, "Got length %d.\n", len);

    hr = IMediaSample_SetActualDataLength(sample, 65537);
    ok(hr == VFW_E_BUFFER_OVERFLOW, "Got hr %#x.\n", hr);
    hr = IMediaSample_SetActualDataLength(sample, -1);
    ok(hr == VFW_E_BUFFER_OVERFLOW || broken(hr == S_OK), "Got hr %#x.\n", hr);
    hr = IMediaSample_SetActualDataLength(sample, 65536);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_SetActualDataLength(sample, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    len = IMediaSample_GetActualDataLength(sample);
    ok(len == 0, "Got length %d.\n", len);

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(props.lActual == 0, "Got actual length %d.\n", props.lActual);

    props.lActual = 123;
    hr = IMediaSample2_SetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(props.lActual == 123, "Got actual length %d.\n", props.lActual);

    len = IMediaSample_GetActualDataLength(sample);
    ok(len == 123, "Got length %d.\n", len);

    /* boolean flags */

    hr = IMediaSample_IsPreroll(sample);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    hr = IMediaSample_SetPreroll(sample, TRUE);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_IsPreroll(sample);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(props.dwSampleFlags == AM_SAMPLE_PREROLL, "Got flags %#x.\n", props.dwSampleFlags);
    hr = IMediaSample_SetPreroll(sample, FALSE);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_IsPreroll(sample);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);

    hr = IMediaSample_IsDiscontinuity(sample);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    hr = IMediaSample_SetDiscontinuity(sample, TRUE);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_IsDiscontinuity(sample);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(props.dwSampleFlags == AM_SAMPLE_DATADISCONTINUITY, "Got flags %#x.\n", props.dwSampleFlags);
    hr = IMediaSample_SetDiscontinuity(sample, FALSE);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_IsDiscontinuity(sample);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);

    hr = IMediaSample_IsSyncPoint(sample);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    hr = IMediaSample_SetSyncPoint(sample, TRUE);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_IsSyncPoint(sample);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(props.dwSampleFlags == AM_SAMPLE_SPLICEPOINT, "Got flags %#x.\n", props.dwSampleFlags);
    hr = IMediaSample_SetSyncPoint(sample, FALSE);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_IsSyncPoint(sample);
    ok(hr == S_FALSE, "Got hr %#x.\n", hr);
    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);

    props.dwSampleFlags = (AM_SAMPLE_PREROLL | AM_SAMPLE_DATADISCONTINUITY | AM_SAMPLE_SPLICEPOINT);
    hr = IMediaSample2_SetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_IsPreroll(sample);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_IsDiscontinuity(sample);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_IsSyncPoint(sample);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(props.dwSampleFlags == (AM_SAMPLE_PREROLL | AM_SAMPLE_DATADISCONTINUITY | AM_SAMPLE_SPLICEPOINT),
            "Got flags %#x.\n", props.dwSampleFlags);

    hr = IMediaSample_SetMediaType(sample, &expect_mt);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    IMediaSample2_Release(sample2);
    IMediaSample_Release(sample);

    hr = IMemAllocator_GetBuffer(allocator, &sample, NULL, NULL, 0);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = IMediaSample_QueryInterface(sample, &IID_IMediaSample2, (void **)&sample2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IMediaSample2_GetProperties(sample2, sizeof(props), (BYTE *)&props);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!props.dwTypeSpecificFlags, "Got type-specific flags %#x.\n", props.dwTypeSpecificFlags);
    ok(!props.dwSampleFlags, "Got flags %#x.\n", props.dwSampleFlags);
    ok(props.lActual == 123, "Got actual length %d.\n", props.lActual);
    ok(!props.tStart, "Got sample start %s.\n", wine_dbgstr_longlong(props.tStart));
    ok(!props.dwStreamId, "Got stream ID %#x.\n", props.dwStreamId);
    ok(!props.pMediaType, "Got media type %p.\n", props.pMediaType);
    ok(props.cbBuffer == 65536, "Got buffer length %d.\n", props.cbBuffer);

    IMediaSample2_Release(sample2);
    IMediaSample_Release(sample);
    IMemAllocator_Release(allocator);
}

START_TEST(memallocator)
{
    CoInitialize(NULL);

    test_commit();
    test_sample_time();
    test_media_time();
    test_sample_properties();

    CoUninitialize();
}
