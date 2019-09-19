/*
 * GStreamer splitter + decoder, adapted from parser.c
 *
 * Copyright 2010 Maarten Lankhorst for CodeWeavers
 * Copyright 2010 Aric Stewart for CodeWeavers
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

#include "config.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

#include "gst_private.h"
#include "gst_guids.h"
#include "gst_cbs.h"

#include "vfwmsgs.h"
#include "amvideo.h"

#include "wine/unicode.h"
#include "wine/debug.h"

#include <assert.h>

#include "dvdmedia.h"
#include "mmreg.h"
#include "ks.h"
#include "initguid.h"
#include "ksmedia.h"

WINE_DEFAULT_DEBUG_CHANNEL(gstreamer);

static pthread_key_t wine_gst_key;

struct gstdemux
{
    struct strmbase_filter filter;

    BasePin sink;
    IAsyncReader *reader;
    IMemAllocator *alloc;
    struct gstdemux_source **ppPins;
    LONG cStreams;

    LONGLONG filesize;

    BOOL initial, ignore_flush;
    GstElement *container;
    GstPad *my_src, *their_sink;
    GstBus *bus;
    guint64 start, nextofs, nextpullofs, stop;
    ALLOCATOR_PROPERTIES props;
    HANDLE no_more_pads_event;

    HANDLE push_thread;
};

struct gstdemux_source
{
    struct strmbase_source pin;
    IQualityControl IQualityControl_iface;

    GstElement *flipfilter;
    GstPad *flip_sink, *flip_src;
    GstPad *their_src;
    GstPad *my_sink;
    AM_MEDIA_TYPE * pmt;
    HANDLE caps_event;
    GstSegment *segment;
    SourceSeeking seek;
};

static inline struct gstdemux *impl_from_IBaseFilter(IBaseFilter *iface)
{
    return CONTAINING_RECORD(iface, struct gstdemux, filter.IBaseFilter_iface);
}

static inline struct gstdemux *impl_from_strmbase_filter(struct strmbase_filter *iface)
{
    return CONTAINING_RECORD(iface, struct gstdemux, filter);
}

const char* media_quark_string = "media-sample";

static const WCHAR wcsInputPinName[] = {'i','n','p','u','t',' ','p','i','n',0};
static const IMediaSeekingVtbl GST_Seeking_Vtbl;
static const IPinVtbl GST_OutputPin_Vtbl;
static const IPinVtbl GST_InputPin_Vtbl;
static const IBaseFilterVtbl GST_Vtbl;
static const IQualityControlVtbl GSTOutPin_QualityControl_Vtbl;

static BOOL create_pin(struct gstdemux *filter, const WCHAR *name, const AM_MEDIA_TYPE *mt);
static HRESULT GST_RemoveOutputPins(struct gstdemux *This);
static HRESULT WINAPI GST_ChangeCurrent(IMediaSeeking *iface);
static HRESULT WINAPI GST_ChangeStop(IMediaSeeking *iface);
static HRESULT WINAPI GST_ChangeRate(IMediaSeeking *iface);

void mark_wine_thread(void)
{
    /* set it to non-NULL to indicate that this is a Wine thread */
    pthread_setspecific(wine_gst_key, &wine_gst_key);
}

BOOL is_wine_thread(void)
{
    return pthread_getspecific(wine_gst_key) != NULL;
}

static gboolean amt_from_gst_caps_audio(GstCaps *caps, AM_MEDIA_TYPE *amt)
{
    WAVEFORMATEXTENSIBLE *wfe;
    WAVEFORMATEX *wfx;
    gint32 depth, bpp;
    GstAudioInfo ainfo;

    if (!gst_audio_info_from_caps (&ainfo, caps))
        return FALSE;

    wfe = heap_alloc(sizeof(*wfe));
    wfx = (WAVEFORMATEX*)wfe;
    amt->majortype = MEDIATYPE_Audio;
    amt->subtype = MEDIASUBTYPE_PCM;
    amt->formattype = FORMAT_WaveFormatEx;
    amt->pbFormat = (BYTE*)wfe;
    amt->cbFormat = sizeof(*wfe);
    amt->bFixedSizeSamples = 0;
    amt->bTemporalCompression = 1;
    amt->lSampleSize = 0;
    amt->pUnk = NULL;

    wfx->wFormatTag = WAVE_FORMAT_EXTENSIBLE;

    wfx->nChannels = ainfo.channels;
    wfx->nSamplesPerSec = ainfo.rate;
    depth = GST_AUDIO_INFO_WIDTH(&ainfo);
    bpp = GST_AUDIO_INFO_DEPTH(&ainfo);

    if (!depth || depth > 32 || depth % 8)
        depth = bpp;
    else if (!bpp)
        bpp = depth;
    wfe->Samples.wValidBitsPerSample = depth;
    wfx->wBitsPerSample = bpp;
    wfx->cbSize = sizeof(*wfe)-sizeof(*wfx);
    switch (wfx->nChannels) {
        case 1: wfe->dwChannelMask = KSAUDIO_SPEAKER_MONO; break;
        case 2: wfe->dwChannelMask = KSAUDIO_SPEAKER_STEREO; break;
        case 4: wfe->dwChannelMask = KSAUDIO_SPEAKER_SURROUND; break;
        case 5: wfe->dwChannelMask = (KSAUDIO_SPEAKER_5POINT1 & ~SPEAKER_LOW_FREQUENCY); break;
        case 6: wfe->dwChannelMask = KSAUDIO_SPEAKER_5POINT1; break;
        case 8: wfe->dwChannelMask = KSAUDIO_SPEAKER_7POINT1; break;
        default:
        wfe->dwChannelMask = 0;
    }
    if (GST_AUDIO_INFO_IS_FLOAT(&ainfo)) {
        wfe->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        wfx->wBitsPerSample = wfe->Samples.wValidBitsPerSample = 32;
    } else {
        wfe->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        if (wfx->nChannels <= 2 && bpp <= 16 && depth == bpp)  {
            wfx->wFormatTag = WAVE_FORMAT_PCM;
            wfx->cbSize = 0;
        }
    }
    wfx->nBlockAlign = wfx->nChannels * wfx->wBitsPerSample/8;
    wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
    return TRUE;
}

static gboolean amt_from_gst_caps_video(GstCaps *caps, AM_MEDIA_TYPE *amt)
{
    VIDEOINFOHEADER *vih;
    BITMAPINFOHEADER *bih;
    gint32 width, height, nom, denom;
    GstVideoInfo vinfo;

    if (!gst_video_info_from_caps (&vinfo, caps))
        return FALSE;
    width = vinfo.width;
    height = vinfo.height;
    nom = vinfo.fps_n;
    denom = vinfo.fps_d;

    vih = heap_alloc(sizeof(*vih));
    bih = &vih->bmiHeader;

    amt->formattype = FORMAT_VideoInfo;
    amt->pbFormat = (BYTE*)vih;
    amt->cbFormat = sizeof(*vih);
    amt->bFixedSizeSamples = amt->bTemporalCompression = 1;
    amt->lSampleSize = 0;
    amt->pUnk = NULL;
    ZeroMemory(vih, sizeof(*vih));
    amt->majortype = MEDIATYPE_Video;
    if (GST_VIDEO_INFO_IS_RGB(&vinfo)) {
        bih->biBitCount = GST_VIDEO_FORMAT_INFO_BITS(vinfo.finfo);
        switch (bih->biBitCount) {
            case 16: amt->subtype = MEDIASUBTYPE_RGB555; break;
            case 24: amt->subtype = MEDIASUBTYPE_RGB24; break;
            case 32: amt->subtype = MEDIASUBTYPE_RGB32; break;
            default:
                FIXME("Unknown bpp %u\n", bih->biBitCount);
                heap_free(vih);
                return FALSE;
        }
        bih->biCompression = BI_RGB;
    } else {
        amt->subtype = MEDIATYPE_Video;
        if (!(amt->subtype.Data1 = gst_video_format_to_fourcc(vinfo.finfo->format))) {
            heap_free(vih);
            return FALSE;
        }
        switch (amt->subtype.Data1) {
            case mmioFOURCC('I','4','2','0'):
            case mmioFOURCC('Y','V','1','2'):
            case mmioFOURCC('N','V','1','2'):
            case mmioFOURCC('N','V','2','1'):
                bih->biBitCount = 12; break;
            case mmioFOURCC('Y','U','Y','2'):
            case mmioFOURCC('Y','V','Y','U'):
                bih->biBitCount = 16; break;
        }
        bih->biCompression = amt->subtype.Data1;
    }
    bih->biSizeImage = width * height * bih->biBitCount / 8;
    if ((vih->AvgTimePerFrame = (REFERENCE_TIME)MulDiv(10000000, denom, nom)) == -1)
        vih->AvgTimePerFrame = 0; /* zero division or integer overflow */
    vih->rcSource.left = 0;
    vih->rcSource.right = width;
    vih->rcSource.top = height;
    vih->rcSource.bottom = 0;
    vih->rcTarget = vih->rcSource;
    bih->biSize = sizeof(*bih);
    bih->biWidth = width;
    bih->biHeight = height;
    bih->biPlanes = 1;
    return TRUE;
}

static gboolean accept_caps_sink(GstPad *pad, GstCaps *caps)
{
    AM_MEDIA_TYPE amt;
    GstStructure *arg;
    const char *typename;
    gboolean ret;

    TRACE("%p %p\n", pad, caps);

    arg = gst_caps_get_structure(caps, 0);
    typename = gst_structure_get_name(arg);
    if (!strcmp(typename, "audio/x-raw")) {
        ret = amt_from_gst_caps_audio(caps, &amt);
        if (ret)
            FreeMediaType(&amt);
        TRACE("+%i\n", ret);
        return ret;
    } else if (!strcmp(typename, "video/x-raw")) {
        ret = amt_from_gst_caps_video(caps, &amt);
        if (ret)
            FreeMediaType(&amt);
        TRACE("-%i\n", ret);
        return ret;
    } else {
        FIXME("Unhandled type \"%s\"\n", typename);
        return FALSE;
    }
}

static gboolean setcaps_sink(GstPad *pad, GstCaps *caps)
{
    struct gstdemux_source *pin = gst_pad_get_element_private(pad);
    struct gstdemux *This = impl_from_strmbase_filter(pin->pin.pin.filter);
    AM_MEDIA_TYPE amt;
    GstStructure *arg;
    const char *typename;
    gboolean ret;

    TRACE("%p %p\n", pad, caps);

    arg = gst_caps_get_structure(caps, 0);
    typename = gst_structure_get_name(arg);
    if (!strcmp(typename, "audio/x-raw")) {
        ret = amt_from_gst_caps_audio(caps, &amt);
    } else if (!strcmp(typename, "video/x-raw")) {
        ret = amt_from_gst_caps_video(caps, &amt);
        if (ret)
            This->props.cbBuffer = max(This->props.cbBuffer, ((VIDEOINFOHEADER*)amt.pbFormat)->bmiHeader.biSizeImage);
    } else {
        FIXME("Unhandled type \"%s\"\n", typename);
        return FALSE;
    }
    TRACE("Linking returned %i for %s\n", ret, typename);
    if (!ret)
        return FALSE;
    FreeMediaType(pin->pmt);
    *pin->pmt = amt;
    SetEvent(pin->caps_event);
    return TRUE;
}

static gboolean query_sink(GstPad *pad, GstObject *parent, GstQuery *query)
{
    switch (GST_QUERY_TYPE (query)) {
        case GST_QUERY_ACCEPT_CAPS:
        {
            GstCaps *caps;
            gboolean res;
            gst_query_parse_accept_caps(query, &caps);
            res = accept_caps_sink(pad, caps);
            gst_query_set_accept_caps_result(query, res);
            return TRUE; /* FIXME */
        }
        default:
            return gst_pad_query_default (pad, parent, query);
    }
}

static gboolean gst_base_src_perform_seek(struct gstdemux *This, GstEvent *event)
{
    gboolean res = TRUE;
    gdouble rate;
    GstFormat seek_format;
    GstSeekFlags flags;
    GstSeekType cur_type, stop_type;
    gint64 cur, stop;
    gboolean flush;
    guint32 seqnum;
    GstEvent *tevent;
    BOOL thread = !!This->push_thread;

    TRACE("%p %p\n", This, event);

    gst_event_parse_seek(event, &rate, &seek_format, &flags,
                         &cur_type, &cur, &stop_type, &stop);

    if (seek_format != GST_FORMAT_BYTES) {
        FIXME("Not handling other format %i\n", seek_format);
        return FALSE;
    }

    flush = flags & GST_SEEK_FLAG_FLUSH;
    seqnum = gst_event_get_seqnum(event);

    /* send flush start */
    if (flush) {
        tevent = gst_event_new_flush_start();
        gst_event_set_seqnum(tevent, seqnum);
        gst_pad_push_event(This->my_src, tevent);
        if (This->reader)
            IAsyncReader_BeginFlush(This->reader);
        if (thread)
            gst_pad_set_active(This->my_src, 1);
    }

    This->nextofs = This->start = cur;

    /* and prepare to continue streaming */
    if (flush) {
        tevent = gst_event_new_flush_stop(TRUE);
        gst_event_set_seqnum(tevent, seqnum);
        gst_pad_push_event(This->my_src, tevent);
        if (This->reader)
            IAsyncReader_EndFlush(This->reader);
        if (thread)
            gst_pad_set_active(This->my_src, 1);
    }

    return res;
}

static gboolean event_src(GstPad *pad, GstObject *parent, GstEvent *event)
{
    struct gstdemux *This = gst_pad_get_element_private(pad);

    TRACE("%p %p\n", pad, event);

    switch (event->type) {
        case GST_EVENT_SEEK:
            return gst_base_src_perform_seek(This, event);
        case GST_EVENT_FLUSH_START:
            EnterCriticalSection(&This->filter.csFilter);
            if (This->reader)
                IAsyncReader_BeginFlush(This->reader);
            LeaveCriticalSection(&This->filter.csFilter);
            break;
        case GST_EVENT_FLUSH_STOP:
            EnterCriticalSection(&This->filter.csFilter);
            if (This->reader)
                IAsyncReader_EndFlush(This->reader);
            LeaveCriticalSection(&This->filter.csFilter);
            break;
        default:
            FIXME("%p (%u) stub\n", event, event->type);
        case GST_EVENT_TAG:
        case GST_EVENT_QOS:
        case GST_EVENT_RECONFIGURE:
            return gst_pad_event_default(pad, parent, event);
    }
    return TRUE;
}

static gboolean event_sink(GstPad *pad, GstObject *parent, GstEvent *event)
{
    struct gstdemux_source *pin = gst_pad_get_element_private(pad);

    TRACE("%p %p\n", pad, event);

    switch (event->type) {
        case GST_EVENT_SEGMENT: {
            gdouble rate, applied_rate;
            gint64 stop, pos;
            const GstSegment *segment;

            gst_event_parse_segment(event, &segment);

            pos = segment->position;
            stop = segment->stop;
            rate = segment->rate;
            applied_rate = segment->applied_rate;

            if (segment->format != GST_FORMAT_TIME) {
                FIXME("Ignoring new segment because of format %i\n", segment->format);
                return TRUE;
            }

            gst_segment_copy_into(segment, pin->segment);

            pos /= 100;

            if (stop > 0)
                stop /= 100;

            if (pin->pin.pin.pConnectedTo)
                IPin_NewSegment(pin->pin.pin.pConnectedTo, pos, stop, rate*applied_rate);

            return TRUE;
        }
        case GST_EVENT_EOS:
            if (pin->pin.pin.pConnectedTo)
                IPin_EndOfStream(pin->pin.pin.pConnectedTo);
            return TRUE;
        case GST_EVENT_FLUSH_START:
            if (impl_from_strmbase_filter(pin->pin.pin.filter)->ignore_flush) {
                /* gst-plugins-base prior to 1.7 contains a bug which causes
                 * our sink pins to receive a flush-start event when the
                 * decodebin changes from PAUSED to READY (including
                 * PLAYING->PAUSED->READY), but no matching flush-stop event is
                 * sent. See <gst-plugins-base.git:60bad4815db966a8e4). Here we
                 * unset the flushing flag to avoid the problem. */
                TRACE("Working around gst <1.7 bug, ignoring FLUSH_START\n");
                GST_PAD_UNSET_FLUSHING (pad);
                return TRUE;
            }
            if (pin->pin.pin.pConnectedTo)
                IPin_BeginFlush(pin->pin.pin.pConnectedTo);
            return TRUE;
        case GST_EVENT_FLUSH_STOP:
            gst_segment_init(pin->segment, GST_FORMAT_TIME);
            if (pin->pin.pin.pConnectedTo)
                IPin_EndFlush(pin->pin.pin.pConnectedTo);
            return TRUE;
        case GST_EVENT_CAPS: {
            GstCaps *caps;
            gst_event_parse_caps(event, &caps);
            return setcaps_sink(pad, caps);
        }
        default:
            TRACE("%p stub %s\n", event, gst_event_type_get_name(event->type));
            return gst_pad_event_default(pad, parent, event);
    }
}

static void release_sample(void *data)
{
    ULONG ret;
    ret = IMediaSample_Release((IMediaSample *)data);
    TRACE("Releasing %p returns %u\n", data, ret);
}

static DWORD CALLBACK push_data(LPVOID iface)
{
    LONGLONG maxlen, curlen;
    struct gstdemux *This = iface;
    IMediaSample *buf;
    DWORD_PTR user;
    HRESULT hr;

    IBaseFilter_AddRef(&This->filter.IBaseFilter_iface);

    if (!This->stop)
        IAsyncReader_Length(This->reader, &maxlen, &curlen);
    else
        maxlen = This->stop;

    TRACE("Starting..\n");
    for (;;) {
        REFERENCE_TIME tStart, tStop;
        ULONG len;
        GstBuffer *gstbuf;
        gsize bufsize;
        BYTE *data;
        int ret;

        hr = IMemAllocator_GetBuffer(This->alloc, &buf, NULL, NULL, 0);
        if (FAILED(hr))
            break;

        if (This->nextofs >= maxlen)
            break;
        len = IMediaSample_GetSize(buf);
        if (This->nextofs + len > maxlen)
            len = maxlen - This->nextofs;

        tStart = MEDIATIME_FROM_BYTES(This->nextofs);
        tStop = tStart + MEDIATIME_FROM_BYTES(len);
        IMediaSample_SetTime(buf, &tStart, &tStop);

        hr = IAsyncReader_Request(This->reader, buf, 0);
        if (FAILED(hr)) {
            IMediaSample_Release(buf);
            break;
        }
        This->nextofs += len;
        hr = IAsyncReader_WaitForNext(This->reader, -1, &buf, &user);
        if (FAILED(hr) || !buf) {
            if (buf)
                IMediaSample_Release(buf);
            break;
        }

        IMediaSample_GetPointer(buf, &data);
        bufsize = IMediaSample_GetActualDataLength(buf);
        gstbuf = gst_buffer_new_wrapped_full(0, data, bufsize, 0, bufsize, buf, release_sample_wrapper);
        IMediaSample_AddRef(buf);
        gst_mini_object_set_qdata(GST_MINI_OBJECT(gstbuf), g_quark_from_static_string(media_quark_string), buf, release_sample_wrapper);
        if (!gstbuf) {
            IMediaSample_Release(buf);
            break;
        }
        gstbuf->duration = gstbuf->pts = -1;
        ret = gst_pad_push(This->my_src, gstbuf);
        if (ret >= 0)
            hr = S_OK;
        else
            ERR("Sending returned: %i\n", ret);
        if (ret == GST_FLOW_ERROR)
            hr = E_FAIL;
        else if (ret == GST_FLOW_FLUSHING)
            hr = VFW_E_WRONG_STATE;
        if (hr != S_OK)
            break;
    }

    gst_pad_push_event(This->my_src, gst_event_new_eos());

    TRACE("Almost stopping.. %08x\n", hr);
    do {
        IAsyncReader_WaitForNext(This->reader, 0, &buf, &user);
        if (buf)
            IMediaSample_Release(buf);
    } while (buf);

    TRACE("Stopping.. %08x\n", hr);

    IBaseFilter_Release(&This->filter.IBaseFilter_iface);

    return 0;
}

static GstFlowReturn got_data_sink(GstPad *pad, GstObject *parent, GstBuffer *buf)
{
    struct gstdemux_source *pin = gst_pad_get_element_private(pad);
    struct gstdemux *This = impl_from_strmbase_filter(pin->pin.pin.filter);
    HRESULT hr;
    BYTE *ptr = NULL;
    IMediaSample *sample;
    GstMapInfo info;

    TRACE("%p %p\n", pad, buf);

    if (This->initial) {
        gst_buffer_unref(buf);
        return GST_FLOW_OK;
    }

    hr = BaseOutputPinImpl_GetDeliveryBuffer(&pin->pin, &sample, NULL, NULL, 0);

    if (hr == VFW_E_NOT_CONNECTED) {
        gst_buffer_unref(buf);
        return GST_FLOW_NOT_LINKED;
    }

    if (FAILED(hr)) {
        gst_buffer_unref(buf);
        ERR("Could not get a delivery buffer (%x), returning GST_FLOW_FLUSHING\n", hr);
        return GST_FLOW_FLUSHING;
    }

    gst_buffer_map(buf, &info, GST_MAP_READ);

    hr = IMediaSample_SetActualDataLength(sample, info.size);
    if(FAILED(hr)){
        WARN("SetActualDataLength failed: %08x\n", hr);
        return GST_FLOW_FLUSHING;
    }

    IMediaSample_GetPointer(sample, &ptr);

    memcpy(ptr, info.data, info.size);

    gst_buffer_unmap(buf, &info);

    if (GST_BUFFER_PTS_IS_VALID(buf)) {
        REFERENCE_TIME rtStart = gst_segment_to_running_time(pin->segment, GST_FORMAT_TIME, buf->pts);
        if (rtStart >= 0)
            rtStart /= 100;

        if (GST_BUFFER_DURATION_IS_VALID(buf)) {
            REFERENCE_TIME tStart = buf->pts / 100;
            REFERENCE_TIME tStop = (buf->pts + buf->duration) / 100;
            REFERENCE_TIME rtStop;
            rtStop = gst_segment_to_running_time(pin->segment, GST_FORMAT_TIME, buf->pts + buf->duration);
            if (rtStop >= 0)
                rtStop /= 100;
            TRACE("Current time on %p: %i to %i ms\n", pin, (int)(rtStart / 10000), (int)(rtStop / 10000));
            IMediaSample_SetTime(sample, &rtStart, rtStop >= 0 ? &rtStop : NULL);
            IMediaSample_SetMediaTime(sample, &tStart, &tStop);
        } else {
            IMediaSample_SetTime(sample, rtStart >= 0 ? &rtStart : NULL, NULL);
            IMediaSample_SetMediaTime(sample, NULL, NULL);
        }
    } else {
        IMediaSample_SetTime(sample, NULL, NULL);
        IMediaSample_SetMediaTime(sample, NULL, NULL);
    }

    IMediaSample_SetDiscontinuity(sample, GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DISCONT));
    IMediaSample_SetPreroll(sample, GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_LIVE));
    IMediaSample_SetSyncPoint(sample, !GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT));

    if (!pin->pin.pin.pConnectedTo)
        hr = VFW_E_NOT_CONNECTED;
    else
        hr = IMemInputPin_Receive(pin->pin.pMemInputPin, sample);

    TRACE("sending sample returned: %08x\n", hr);

    gst_buffer_unref(buf);
    IMediaSample_Release(sample);

    if (hr == VFW_E_NOT_CONNECTED)
        return GST_FLOW_NOT_LINKED;

    if (FAILED(hr))
        return GST_FLOW_FLUSHING;

    return GST_FLOW_OK;
}

static GstFlowReturn request_buffer_src(GstPad *pad, GstObject *parent, guint64 ofs, guint len, GstBuffer **buf)
{
    struct gstdemux *This = gst_pad_get_element_private(pad);
    HRESULT hr;
    GstMapInfo info;

    TRACE("%p %s %i %p\n", pad, wine_dbgstr_longlong(ofs), len, buf);

    *buf = NULL;
    if (ofs == GST_BUFFER_OFFSET_NONE)
        ofs = This->nextpullofs;
    if (ofs >= This->filesize) {
        WARN("Reading past eof: %s, %u\n", wine_dbgstr_longlong(ofs), len);
        return GST_FLOW_EOS;
    }
    if (len + ofs > This->filesize)
        len = This->filesize - ofs;
    This->nextpullofs = ofs + len;

    *buf = gst_buffer_new_and_alloc(len);
    gst_buffer_map(*buf, &info, GST_MAP_WRITE);
    hr = IAsyncReader_SyncRead(This->reader, ofs, len, info.data);
    gst_buffer_unmap(*buf, &info);
    if (FAILED(hr)) {
        ERR("Returned %08x\n", hr);
        return GST_FLOW_ERROR;
    }

    GST_BUFFER_OFFSET(*buf) = ofs;
    return GST_FLOW_OK;
}

static DWORD CALLBACK push_data_init(LPVOID iface)
{
    struct gstdemux *This = iface;
    DWORD64 ofs = 0;

    TRACE("Starting..\n");
    for (;;) {
        GstBuffer *buf;
        GstFlowReturn ret = request_buffer_src(This->my_src, NULL, ofs, 4096, &buf);
        if (ret < 0) {
            ERR("Obtaining buffer returned: %i\n", ret);
            break;
        }
        ret = gst_pad_push(This->my_src, buf);
        ofs += 4096;
        if (ret)
            TRACE("Sending returned: %i\n", ret);
        if (ret < 0)
            break;
    }
    TRACE("Stopping..\n");
    return 0;
}

static void removed_decoded_pad(GstElement *bin, GstPad *pad, gpointer user)
{
    struct gstdemux *This = user;
    int x;
    struct gstdemux_source *pin;

    TRACE("%p %p %p\n", This, bin, pad);

    EnterCriticalSection(&This->filter.csFilter);
    for (x = 0; x < This->cStreams; ++x) {
        if (This->ppPins[x]->their_src == pad)
            break;
    }
    if (x == This->cStreams)
        goto out;

    pin = This->ppPins[x];

    if(pin->flipfilter)
        gst_pad_unlink(pin->their_src, pin->flip_sink);
    else
        gst_pad_unlink(pin->their_src, pin->my_sink);

    gst_object_unref(pin->their_src);
    pin->their_src = NULL;
out:
    TRACE("Removed %i/%i\n", x, This->cStreams);
    LeaveCriticalSection(&This->filter.csFilter);
}

static void init_new_decoded_pad(GstElement *bin, GstPad *pad, struct gstdemux *This)
{
    const char *typename;
    char *name;
    AM_MEDIA_TYPE amt = {{0}};
    GstCaps *caps;
    GstStructure *arg;
    GstPad *mypad;
    struct gstdemux_source *pin;
    int ret;
    gchar my_name[1024];
    WCHAR nameW[128];

    TRACE("%p %p %p\n", This, bin, pad);

    name = gst_pad_get_name(pad);
    MultiByteToWideChar(CP_UNIXCP, 0, name, -1, nameW, ARRAY_SIZE(nameW) - 1);
    nameW[ARRAY_SIZE(nameW) - 1] = 0;
    TRACE("Name: %s\n", name);
    strcpy(my_name, "qz_sink_");
    strcat(my_name, name);
    g_free(name);

    caps = gst_pad_query_caps(pad, NULL);
    caps = gst_caps_make_writable(caps);
    arg = gst_caps_get_structure(caps, 0);
    typename = gst_structure_get_name(arg);

    mypad = gst_pad_new(my_name, GST_PAD_SINK);
    gst_pad_set_chain_function(mypad, got_data_sink_wrapper);
    gst_pad_set_event_function(mypad, event_sink_wrapper);
    gst_pad_set_query_function(mypad, query_sink_wrapper);

    if (strcmp(typename, "audio/x-raw") && strcmp(typename, "video/x-raw"))
    {
        FIXME("Unknown type \'%s\'\n", typename);
        return;
    }

    if (!create_pin(This, nameW, &amt))
    {
        ERR("Failed to allocate memory.\n");
        return;
    }

    pin = This->ppPins[This->cStreams - 1];
    gst_pad_set_element_private(mypad, pin);
    pin->my_sink = mypad;

    gst_segment_init(pin->segment, GST_FORMAT_TIME);

    if (!strcmp(typename, "video/x-raw"))
    {
        GstElement *vconv;

        TRACE("setting up videoflip filter for pin %p, my_sink: %p, their_src: %p\n",
                pin, pin->my_sink, pad);

        /* gstreamer outputs video top-down, but dshow expects bottom-up, so
         * make new transform filter to invert video */
        vconv = gst_element_factory_make("videoconvert", NULL);
        if(!vconv){
            ERR("Missing videoconvert filter?\n");
            ret = -1;
            goto exit;
        }

        pin->flipfilter = gst_element_factory_make("videoflip", NULL);
        if(!pin->flipfilter){
            ERR("Missing videoflip filter?\n");
            ret = -1;
            goto exit;
        }

        gst_util_set_object_arg(G_OBJECT(pin->flipfilter), "method", "vertical-flip");

        gst_bin_add(GST_BIN(This->container), vconv); /* bin takes ownership */
        gst_element_sync_state_with_parent(vconv);
        gst_bin_add(GST_BIN(This->container), pin->flipfilter); /* bin takes ownership */
        gst_element_sync_state_with_parent(pin->flipfilter);

        gst_element_link (vconv, pin->flipfilter);

        pin->flip_sink = gst_element_get_static_pad(vconv, "sink");
        if(!pin->flip_sink){
            WARN("Couldn't find sink on flip filter\n");
            pin->flipfilter = NULL;
            ret = -1;
            goto exit;
        }

        ret = gst_pad_link(pad, pin->flip_sink);
        if(ret < 0){
            WARN("gst_pad_link failed: %d\n", ret);
            gst_object_unref(pin->flip_sink);
            pin->flip_sink = NULL;
            pin->flipfilter = NULL;
            goto exit;
        }

        pin->flip_src = gst_element_get_static_pad(pin->flipfilter, "src");
        if(!pin->flip_src){
            WARN("Couldn't find src on flip filter\n");
            gst_object_unref(pin->flip_sink);
            pin->flip_sink = NULL;
            pin->flipfilter = NULL;
            ret = -1;
            goto exit;
        }

        ret = gst_pad_link(pin->flip_src, pin->my_sink);
        if(ret < 0){
            WARN("gst_pad_link failed: %d\n", ret);
            gst_object_unref(pin->flip_src);
            pin->flip_src = NULL;
            gst_object_unref(pin->flip_sink);
            pin->flip_sink = NULL;
            pin->flipfilter = NULL;
            goto exit;
        }
    } else
        ret = gst_pad_link(pad, mypad);

    gst_pad_set_active(mypad, 1);

exit:
    TRACE("Linking: %i\n", ret);

    if (ret >= 0) {
        pin->their_src = pad;
        gst_object_ref(pin->their_src);
    }
}

static void existing_new_pad(GstElement *bin, GstPad *pad, gpointer user)
{
    struct gstdemux *This = user;
    int x, ret;

    TRACE("%p %p %p\n", This, bin, pad);

    if (gst_pad_is_linked(pad))
        return;

    /* Still holding our own lock */
    if (This->initial) {
        init_new_decoded_pad(bin, pad, This);
        return;
    }

    EnterCriticalSection(&This->filter.csFilter);
    for (x = 0; x < This->cStreams; ++x) {
        struct gstdemux_source *pin = This->ppPins[x];
        if (!pin->their_src) {
            gst_segment_init(pin->segment, GST_FORMAT_TIME);

            if (pin->flipfilter)
                ret = gst_pad_link(pad, pin->flip_sink);
            else
                ret = gst_pad_link(pad, pin->my_sink);

            if (ret >= 0) {
                pin->their_src = pad;
                gst_object_ref(pin->their_src);
                TRACE("Relinked\n");
                LeaveCriticalSection(&This->filter.csFilter);
                return;
            }
        }
    }
    init_new_decoded_pad(bin, pad, This);
    LeaveCriticalSection(&This->filter.csFilter);
}

static gboolean query_function(GstPad *pad, GstObject *parent, GstQuery *query)
{
    struct gstdemux *This = gst_pad_get_element_private(pad);
    GstFormat format;
    int ret;
    LONGLONG duration;

    TRACE("%p %p %p\n", This, pad, query);

    switch (GST_QUERY_TYPE(query)) {
        case GST_QUERY_DURATION:
            gst_query_parse_duration (query, &format, NULL);
            if (format == GST_FORMAT_PERCENT) {
                gst_query_set_duration (query, GST_FORMAT_PERCENT, GST_FORMAT_PERCENT_MAX);
                return TRUE;
            }
            ret = gst_pad_query_convert (pad, GST_FORMAT_BYTES, This->filesize, format, &duration);
            gst_query_set_duration(query, format, duration);
            return ret;
        case GST_QUERY_SEEKING:
            gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
            TRACE("Seeking %i %i\n", format, GST_FORMAT_BYTES);
            if (format != GST_FORMAT_BYTES)
                return FALSE;
            gst_query_set_seeking(query, GST_FORMAT_BYTES, 1, 0, This->filesize);
            return TRUE;
        case GST_QUERY_SCHEDULING:
            gst_query_set_scheduling(query, GST_SCHEDULING_FLAG_SEEKABLE, 1, -1, 0);
            gst_query_add_scheduling_mode(query, GST_PAD_MODE_PUSH);
            gst_query_add_scheduling_mode(query, GST_PAD_MODE_PULL);
            return TRUE;
        default:
            TRACE("Unhandled query type: %s\n", GST_QUERY_TYPE_NAME(query));
            return FALSE;
    }
}

static gboolean activate_push(GstPad *pad, gboolean activate)
{
    struct gstdemux *This = gst_pad_get_element_private(pad);

    TRACE("%p %p %u\n", This, pad, activate);

    EnterCriticalSection(&This->filter.csFilter);
    if (!activate) {
        TRACE("Deactivating\n");
        if (!This->initial)
            IAsyncReader_BeginFlush(This->reader);
        if (This->push_thread) {
            WaitForSingleObject(This->push_thread, -1);
            CloseHandle(This->push_thread);
            This->push_thread = NULL;
        }
        if (!This->initial)
            IAsyncReader_EndFlush(This->reader);
        if (This->filter.state == State_Stopped)
            This->nextofs = This->start;
    } else if (!This->push_thread) {
        TRACE("Activating\n");
        if (This->initial)
            This->push_thread = CreateThread(NULL, 0, push_data_init, This, 0, NULL);
        else
            This->push_thread = CreateThread(NULL, 0, push_data, This, 0, NULL);
    }
    LeaveCriticalSection(&This->filter.csFilter);
    return TRUE;
}

static gboolean activate_mode(GstPad *pad, GstObject *parent, GstPadMode mode, gboolean activate)
{
    TRACE("%p %p 0x%x %u\n", pad, parent, mode, activate);
    switch (mode) {
      case GST_PAD_MODE_PULL:
        return TRUE;
      case GST_PAD_MODE_PUSH:
        return activate_push(pad, activate);
      default:
        return FALSE;
    }
    return FALSE;
}

static void no_more_pads(GstElement *decodebin, gpointer user)
{
    struct gstdemux *This = user;
    TRACE("%p %p\n", This, decodebin);
    SetEvent(This->no_more_pads_event);
}

static GstAutoplugSelectResult autoplug_blacklist(GstElement *bin, GstPad *pad, GstCaps *caps, GstElementFactory *fact, gpointer user)
{
    const char *name = gst_element_factory_get_longname(fact);

    if (strstr(name, "Player protection")) {
        WARN("Blacklisted a/52 decoder because it only works in Totem\n");
        return GST_AUTOPLUG_SELECT_SKIP;
    }
    if (!strcmp(name, "Fluendo Hardware Accelerated Video Decoder")) {
        WARN("Disabled video acceleration since it breaks in wine\n");
        return GST_AUTOPLUG_SELECT_SKIP;
    }
    TRACE("using \"%s\"\n", name);
    return GST_AUTOPLUG_SELECT_TRY;
}

static GstBusSyncReply watch_bus(GstBus *bus, GstMessage *msg, gpointer data)
{
    struct gstdemux *This = data;
    GError *err = NULL;
    gchar *dbg_info = NULL;

    TRACE("%p %p %p\n", This, bus, msg);

    if (GST_MESSAGE_TYPE(msg) & GST_MESSAGE_ERROR) {
        gst_message_parse_error(msg, &err, &dbg_info);
        ERR("%s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
        ERR("%s\n", dbg_info);
    } else if (GST_MESSAGE_TYPE(msg) & GST_MESSAGE_WARNING) {
        gst_message_parse_warning(msg, &err, &dbg_info);
        WARN("%s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
        WARN("%s\n", dbg_info);
    }
    if (err)
        g_error_free(err);
    g_free(dbg_info);
    return GST_BUS_DROP;
}

static void unknown_type(GstElement *bin, GstPad *pad, GstCaps *caps, gpointer user)
{
    gchar *strcaps = gst_caps_to_string(caps);
    ERR("Could not find a filter for caps: %s\n", debugstr_a(strcaps));
    g_free(strcaps);
}

static HRESULT GST_Connect(struct gstdemux *This, IPin *pConnectPin, ALLOCATOR_PROPERTIES *props)
{
    int ret, i;
    LONGLONG avail, duration;
    GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
        "quartz_src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS_ANY);
    GstElement *gstfilter;

    This->props = *props;
    IAsyncReader_Length(This->reader, &This->filesize, &avail);

    if (!This->bus) {
        This->bus = gst_bus_new();
        gst_bus_set_sync_handler(This->bus, watch_bus_wrapper, This, NULL);
    }

    This->container = gst_bin_new(NULL);
    gst_element_set_bus(This->container, This->bus);

    gstfilter = gst_element_factory_make("decodebin", NULL);
    if (!gstfilter) {
        ERR("Could not make source filter, are gstreamer-plugins-* installed for %u bits?\n",
              8 * (int)sizeof(void*));
        return E_FAIL;
    }

    gst_bin_add(GST_BIN(This->container), gstfilter);

    g_signal_connect(gstfilter, "pad-added", G_CALLBACK(existing_new_pad_wrapper), This);
    g_signal_connect(gstfilter, "pad-removed", G_CALLBACK(removed_decoded_pad_wrapper), This);
    g_signal_connect(gstfilter, "autoplug-select", G_CALLBACK(autoplug_blacklist_wrapper), This);
    g_signal_connect(gstfilter, "unknown-type", G_CALLBACK(unknown_type_wrapper), This);

    This->my_src = gst_pad_new_from_static_template(&src_template, "quartz-src");
    gst_pad_set_getrange_function(This->my_src, request_buffer_src_wrapper);
    gst_pad_set_query_function(This->my_src, query_function_wrapper);
    gst_pad_set_activatemode_function(This->my_src, activate_mode_wrapper);
    gst_pad_set_event_function(This->my_src, event_src_wrapper);
    gst_pad_set_element_private (This->my_src, This);
    This->their_sink = gst_element_get_static_pad(gstfilter, "sink");

    g_signal_connect(gstfilter, "no-more-pads", G_CALLBACK(no_more_pads_wrapper), This);
    ret = gst_pad_link(This->my_src, This->their_sink);
    if (ret < 0) {
        ERR("Returns: %i\n", ret);
        return E_FAIL;
    }
    This->start = This->nextofs = This->nextpullofs = This->stop = 0;

    /* Add initial pins */
    This->initial = TRUE;
    ResetEvent(This->no_more_pads_event);
    gst_element_set_state(This->container, GST_STATE_PLAYING);
    ret = gst_element_get_state(This->container, NULL, NULL, -1);

    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        ERR("GStreamer failed to play stream\n");
        return E_FAIL;
    }

    WaitForSingleObject(This->no_more_pads_event, INFINITE);

    gst_pad_query_duration(This->ppPins[0]->their_src, GST_FORMAT_TIME, &duration);
    for (i = 0; i < This->cStreams; ++i)
    {
        This->ppPins[i]->seek.llDuration = This->ppPins[i]->seek.llStop = duration / 100;
        This->ppPins[i]->seek.llCurrent = 0;
        if (!This->ppPins[i]->seek.llDuration)
            This->ppPins[i]->seek.dwCapabilities = 0;
        WaitForSingleObject(This->ppPins[i]->caps_event, INFINITE);
    }
    *props = This->props;

    This->ignore_flush = TRUE;
    gst_element_set_state(This->container, GST_STATE_READY);
    gst_element_get_state(This->container, NULL, NULL, -1);
    This->ignore_flush = FALSE;

    This->initial = FALSE;

    This->nextofs = This->nextpullofs = 0;
    return S_OK;
}

static inline struct gstdemux_source *impl_from_IMediaSeeking(IMediaSeeking *iface)
{
    return CONTAINING_RECORD(iface, struct gstdemux_source, seek.IMediaSeeking_iface);
}

static IPin *gstdemux_get_pin(struct strmbase_filter *base, unsigned int index)
{
    struct gstdemux *filter = impl_from_strmbase_filter(base);

    if (!index)
        return &filter->sink.IPin_iface;
    else if (index <= filter->cStreams)
        return &filter->ppPins[index - 1]->pin.pin.IPin_iface;
    return NULL;
}

static void gstdemux_destroy(struct strmbase_filter *iface)
{
    struct gstdemux *filter = impl_from_strmbase_filter(iface);
    HRESULT hr;

    CloseHandle(filter->no_more_pads_event);

    /* Don't need to clean up output pins, disconnecting input pin will do that */
    if (filter->sink.pConnectedTo)
    {
        hr = IPin_Disconnect(filter->sink.pConnectedTo);
        assert(hr == S_OK);
        hr = IPin_Disconnect(&filter->sink.IPin_iface);
        assert(hr == S_OK);
    }

    FreeMediaType(&filter->sink.mtCurrent);
    if (filter->alloc)
        IMemAllocator_Release(filter->alloc);
    filter->alloc = NULL;
    if (filter->reader)
        IAsyncReader_Release(filter->reader);
    filter->reader = NULL;
    filter->sink.IPin_iface.lpVtbl = NULL;

    if (filter->bus)
    {
        gst_bus_set_sync_handler(filter->bus, NULL, NULL, NULL);
        gst_object_unref(filter->bus);
    }
    strmbase_filter_cleanup(&filter->filter);
    heap_free(filter);
}

static const struct strmbase_filter_ops filter_ops =
{
    .filter_get_pin = gstdemux_get_pin,
    .filter_destroy = gstdemux_destroy,
};

static HRESULT WINAPI sink_CheckMediaType(BasePin *iface, const AM_MEDIA_TYPE *mt)
{
    if (IsEqualGUID(&mt->majortype, &MEDIATYPE_Stream))
        return S_OK;
    return S_FALSE;
}

static const BasePinFuncTable sink_ops =
{
    .pfnCheckMediaType = sink_CheckMediaType,
    .pfnGetMediaType = BasePinImpl_GetMediaType,
};

IUnknown * CALLBACK Gstreamer_Splitter_create(IUnknown *outer, HRESULT *phr)
{
    struct gstdemux *object;

    if (!init_gstreamer())
    {
        *phr = E_FAIL;
        return NULL;
    }

    mark_wine_thread();

    if (!(object = heap_alloc_zero(sizeof(*object))))
    {
        *phr = E_OUTOFMEMORY;
        return NULL;
    }

    strmbase_filter_init(&object->filter, &GST_Vtbl, outer, &CLSID_Gstreamer_Splitter, &filter_ops);

    object->no_more_pads_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    object->sink.dir = PINDIR_INPUT;
    object->sink.filter = &object->filter;
    lstrcpynW(object->sink.name, wcsInputPinName, ARRAY_SIZE(object->sink.name));
    object->sink.IPin_iface.lpVtbl = &GST_InputPin_Vtbl;
    object->sink.pFuncsTable = &sink_ops;
    *phr = S_OK;

    TRACE("Created GStreamer demuxer %p.\n", object);
    return &object->filter.IUnknown_inner;
}

static HRESULT WINAPI GST_Stop(IBaseFilter *iface)
{
    struct gstdemux *This = impl_from_IBaseFilter(iface);

    TRACE("(%p)\n", This);

    mark_wine_thread();

    if (This->container) {
        This->ignore_flush = TRUE;
        gst_element_set_state(This->container, GST_STATE_READY);
        gst_element_get_state(This->container, NULL, NULL, -1);
        This->ignore_flush = FALSE;
    }
    return S_OK;
}

static HRESULT WINAPI GST_Pause(IBaseFilter *iface)
{
    struct gstdemux *This = impl_from_IBaseFilter(iface);
    HRESULT hr = S_OK;
    GstState now;
    GstStateChangeReturn ret;

    TRACE("(%p)\n", This);

    if (!This->container)
        return VFW_E_NOT_CONNECTED;

    mark_wine_thread();

    gst_element_get_state(This->container, &now, NULL, -1);
    if (now == GST_STATE_PAUSED)
        return S_OK;
    if (now != GST_STATE_PLAYING)
        hr = IBaseFilter_Run(iface, -1);
    if (FAILED(hr))
        return hr;
    ret = gst_element_set_state(This->container, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_ASYNC)
        hr = S_FALSE;
    return hr;
}

static HRESULT WINAPI GST_Run(IBaseFilter *iface, REFERENCE_TIME tStart)
{
    struct gstdemux *This = impl_from_IBaseFilter(iface);
    HRESULT hr = S_OK;
    ULONG i;
    GstState now;
    HRESULT hr_any = VFW_E_NOT_CONNECTED;

    TRACE("(%p)->(%s)\n", This, wine_dbgstr_longlong(tStart));

    mark_wine_thread();

    if (!This->container)
        return VFW_E_NOT_CONNECTED;

    EnterCriticalSection(&This->filter.csFilter);
    This->filter.rtStreamStart = tStart;
    LeaveCriticalSection(&This->filter.csFilter);

    gst_element_get_state(This->container, &now, NULL, -1);
    if (now == GST_STATE_PLAYING)
        return S_OK;
    if (now == GST_STATE_PAUSED) {
        GstStateChangeReturn ret;
        ret = gst_element_set_state(This->container, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_ASYNC)
            return S_FALSE;
        return S_OK;
    }

    EnterCriticalSection(&This->filter.csFilter);
    gst_element_set_state(This->container, GST_STATE_PLAYING);
    This->filter.rtStreamStart = tStart;

    for (i = 0; i < This->cStreams; i++) {
        hr = BaseOutputPinImpl_Active(&This->ppPins[i]->pin);
        if (SUCCEEDED(hr)) {
            hr_any = hr;
        }
    }
    hr = hr_any;
    LeaveCriticalSection(&This->filter.csFilter);

    return hr;
}

static HRESULT WINAPI GST_GetState(IBaseFilter *iface, DWORD dwMilliSecsTimeout, FILTER_STATE *pState)
{
    struct gstdemux *This = impl_from_IBaseFilter(iface);
    HRESULT hr = S_OK;
    GstState now, pending;
    GstStateChangeReturn ret;

    TRACE("(%p)->(%d, %p)\n", This, dwMilliSecsTimeout, pState);

    mark_wine_thread();

    if (!This->container) {
        *pState = State_Stopped;
        return S_OK;
    }

    ret = gst_element_get_state(This->container, &now, &pending, dwMilliSecsTimeout == INFINITE ? -1 : dwMilliSecsTimeout * 1000);

    if (ret == GST_STATE_CHANGE_ASYNC)
        hr = VFW_S_STATE_INTERMEDIATE;
    else
        pending = now;

    switch (pending) {
        case GST_STATE_PAUSED: *pState = State_Paused; return hr;
        case GST_STATE_PLAYING: *pState = State_Running; return hr;
        default: *pState = State_Stopped; return hr;
    }
}

static const IBaseFilterVtbl GST_Vtbl = {
    BaseFilterImpl_QueryInterface,
    BaseFilterImpl_AddRef,
    BaseFilterImpl_Release,
    BaseFilterImpl_GetClassID,
    GST_Stop,
    GST_Pause,
    GST_Run,
    GST_GetState,
    BaseFilterImpl_SetSyncSource,
    BaseFilterImpl_GetSyncSource,
    BaseFilterImpl_EnumPins,
    BaseFilterImpl_FindPin,
    BaseFilterImpl_QueryFilterInfo,
    BaseFilterImpl_JoinFilterGraph,
    BaseFilterImpl_QueryVendorInfo
};

static HRESULT WINAPI GST_ChangeCurrent(IMediaSeeking *iface)
{
    struct gstdemux_source *This = impl_from_IMediaSeeking(iface);
    TRACE("(%p)\n", This);
    return S_OK;
}

static HRESULT WINAPI GST_ChangeStop(IMediaSeeking *iface)
{
    struct gstdemux_source *This = impl_from_IMediaSeeking(iface);
    TRACE("(%p)\n", This);
    return S_OK;
}

static HRESULT WINAPI GST_ChangeRate(IMediaSeeking *iface)
{
    struct gstdemux_source *This = impl_from_IMediaSeeking(iface);
    GstEvent *ev = gst_event_new_seek(This->seek.dRate, GST_FORMAT_TIME, 0, GST_SEEK_TYPE_NONE, -1, GST_SEEK_TYPE_NONE, -1);
    TRACE("(%p) New rate %g\n", This, This->seek.dRate);
    mark_wine_thread();
    gst_pad_push_event(This->my_sink, ev);
    return S_OK;
}

static HRESULT WINAPI GST_Seeking_QueryInterface(IMediaSeeking *iface, REFIID riid, void **ppv)
{
    struct gstdemux_source *This = impl_from_IMediaSeeking(iface);
    return IPin_QueryInterface(&This->pin.pin.IPin_iface, riid, ppv);
}

static ULONG WINAPI GST_Seeking_AddRef(IMediaSeeking *iface)
{
    struct gstdemux_source *This = impl_from_IMediaSeeking(iface);
    return IPin_AddRef(&This->pin.pin.IPin_iface);
}

static ULONG WINAPI GST_Seeking_Release(IMediaSeeking *iface)
{
    struct gstdemux_source *This = impl_from_IMediaSeeking(iface);
    return IPin_Release(&This->pin.pin.IPin_iface);
}

static HRESULT WINAPI GST_Seeking_GetCurrentPosition(IMediaSeeking *iface, REFERENCE_TIME *pos)
{
    struct gstdemux_source *This = impl_from_IMediaSeeking(iface);

    TRACE("(%p)->(%p)\n", This, pos);

    if (!pos)
        return E_POINTER;

    mark_wine_thread();

    if (!This->their_src) {
        *pos = This->seek.llCurrent;
        TRACE("Cached value\n");
        if (This->seek.llDuration)
            return S_OK;
        else
            return E_NOTIMPL;
    }

    if (!gst_pad_query_position(This->their_src, GST_FORMAT_TIME, pos)) {
        WARN("Could not query position\n");
        return E_NOTIMPL;
    }
    *pos /= 100;
    This->seek.llCurrent = *pos;
    return S_OK;
}

static GstSeekType type_from_flags(DWORD flags)
{
    switch (flags & AM_SEEKING_PositioningBitsMask) {
    case AM_SEEKING_NoPositioning:
        return GST_SEEK_TYPE_NONE;
    case AM_SEEKING_AbsolutePositioning:
    case AM_SEEKING_RelativePositioning:
        return GST_SEEK_TYPE_SET;
    case AM_SEEKING_IncrementalPositioning:
        return GST_SEEK_TYPE_END;
    }
    return GST_SEEK_TYPE_NONE;
}

static HRESULT WINAPI GST_Seeking_SetPositions(IMediaSeeking *iface,
        REFERENCE_TIME *pCur, DWORD curflags, REFERENCE_TIME *pStop,
        DWORD stopflags)
{
    HRESULT hr;
    struct gstdemux_source *This = impl_from_IMediaSeeking(iface);
    GstSeekFlags f = 0;
    GstSeekType curtype, stoptype;
    GstEvent *e;
    gint64 stop_pos = 0, curr_pos = 0;

    TRACE("(%p)->(%p, 0x%x, %p, 0x%x)\n", This, pCur, curflags, pStop, stopflags);

    mark_wine_thread();

    if (!This->seek.llDuration)
        return E_NOTIMPL;

    hr = SourceSeekingImpl_SetPositions(iface, pCur, curflags, pStop, stopflags);
    if (!This->their_src)
        return hr;

    curtype = type_from_flags(curflags);
    stoptype = type_from_flags(stopflags);
    if (curflags & AM_SEEKING_SeekToKeyFrame)
        f |= GST_SEEK_FLAG_KEY_UNIT;
    if (curflags & AM_SEEKING_Segment)
        f |= GST_SEEK_FLAG_SEGMENT;
    if (!(curflags & AM_SEEKING_NoFlush))
        f |= GST_SEEK_FLAG_FLUSH;

    if (((curflags & AM_SEEKING_PositioningBitsMask) == AM_SEEKING_RelativePositioning) ||
        ((stopflags & AM_SEEKING_PositioningBitsMask) == AM_SEEKING_RelativePositioning)) {
        gint64 tmp_pos;
        gst_pad_query_position (This->my_sink, GST_FORMAT_TIME, &tmp_pos);
        if ((curflags & AM_SEEKING_PositioningBitsMask) == AM_SEEKING_RelativePositioning)
            curr_pos = tmp_pos;
        if ((stopflags & AM_SEEKING_PositioningBitsMask) == AM_SEEKING_RelativePositioning)
            stop_pos = tmp_pos;
    }

    e = gst_event_new_seek(This->seek.dRate, GST_FORMAT_TIME, f, curtype, pCur ? curr_pos + *pCur * 100 : -1, stoptype, pStop ? stop_pos + *pStop * 100 : -1);
    if (gst_pad_push_event(This->my_sink, e))
        return S_OK;
    else
        return E_NOTIMPL;
}

static const IMediaSeekingVtbl GST_Seeking_Vtbl =
{
    GST_Seeking_QueryInterface,
    GST_Seeking_AddRef,
    GST_Seeking_Release,
    SourceSeekingImpl_GetCapabilities,
    SourceSeekingImpl_CheckCapabilities,
    SourceSeekingImpl_IsFormatSupported,
    SourceSeekingImpl_QueryPreferredFormat,
    SourceSeekingImpl_GetTimeFormat,
    SourceSeekingImpl_IsUsingTimeFormat,
    SourceSeekingImpl_SetTimeFormat,
    SourceSeekingImpl_GetDuration,
    SourceSeekingImpl_GetStopPosition,
    GST_Seeking_GetCurrentPosition,
    SourceSeekingImpl_ConvertTimeFormat,
    GST_Seeking_SetPositions,
    SourceSeekingImpl_GetPositions,
    SourceSeekingImpl_GetAvailable,
    SourceSeekingImpl_SetRate,
    SourceSeekingImpl_GetRate,
    SourceSeekingImpl_GetPreroll
};

static inline struct gstdemux_source *impl_from_IQualityControl( IQualityControl *iface )
{
    return CONTAINING_RECORD(iface, struct gstdemux_source, IQualityControl_iface);
}

static HRESULT WINAPI GST_QualityControl_QueryInterface(IQualityControl *iface, REFIID riid, void **ppv)
{
    struct gstdemux_source *pin = impl_from_IQualityControl(iface);
    return IPin_QueryInterface(&pin->pin.pin.IPin_iface, riid, ppv);
}

static ULONG WINAPI GST_QualityControl_AddRef(IQualityControl *iface)
{
    struct gstdemux_source *pin = impl_from_IQualityControl(iface);
    return IPin_AddRef(&pin->pin.pin.IPin_iface);
}

static ULONG WINAPI GST_QualityControl_Release(IQualityControl *iface)
{
    struct gstdemux_source *pin = impl_from_IQualityControl(iface);
    return IPin_Release(&pin->pin.pin.IPin_iface);
}

static HRESULT WINAPI GST_QualityControl_Notify(IQualityControl *iface, IBaseFilter *sender, Quality qm)
{
    struct gstdemux_source *pin = impl_from_IQualityControl(iface);
    GstEvent *evt;

    TRACE("(%p)->(%p, { 0x%x %u %s %s })\n", pin, sender,
            qm.Type, qm.Proportion,
            wine_dbgstr_longlong(qm.Late),
            wine_dbgstr_longlong(qm.TimeStamp));

    mark_wine_thread();

    if (qm.Type == Flood)
        qm.Late = 0;

    evt = gst_event_new_qos(qm.Type == Famine ? GST_QOS_TYPE_UNDERFLOW : GST_QOS_TYPE_OVERFLOW,
            qm.Proportion / 1000., qm.Late * 100, qm.TimeStamp * 100);

    if (!evt) {
        WARN("Failed to create QOS event\n");
        return E_INVALIDARG;
    }

    gst_pad_push_event(pin->my_sink, evt);

    return S_OK;
}

static HRESULT WINAPI GST_QualityControl_SetSink(IQualityControl *iface, IQualityControl *tonotify)
{
    struct gstdemux_source *pin = impl_from_IQualityControl(iface);
    TRACE("(%p)->(%p)\n", pin, pin);
    /* Do nothing */
    return S_OK;
}

static const IQualityControlVtbl GSTOutPin_QualityControl_Vtbl = {
    GST_QualityControl_QueryInterface,
    GST_QualityControl_AddRef,
    GST_QualityControl_Release,
    GST_QualityControl_Notify,
    GST_QualityControl_SetSink
};

static inline struct gstdemux_source *impl_source_from_IPin(IPin *iface)
{
    return CONTAINING_RECORD(iface, struct gstdemux_source, pin.pin.IPin_iface);
}

static HRESULT WINAPI GSTOutPin_QueryInterface(IPin *iface, REFIID riid, void **ppv)
{
    struct gstdemux_source *This = impl_source_from_IPin(iface);

    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown))
        *ppv = iface;
    else if (IsEqualIID(riid, &IID_IPin))
        *ppv = iface;
    else if (IsEqualIID(riid, &IID_IMediaSeeking))
        *ppv = &This->seek;
    else if (IsEqualIID(riid, &IID_IQualityControl))
        *ppv = &This->IQualityControl_iface;

    if (*ppv) {
        IUnknown_AddRef((IUnknown *)(*ppv));
        return S_OK;
    }
    FIXME("No interface for %s!\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static HRESULT WINAPI GSTOutPin_CheckMediaType(BasePin *base, const AM_MEDIA_TYPE *amt)
{
    FIXME("(%p) stub\n", base);
    return S_OK;
}

static HRESULT WINAPI GSTOutPin_GetMediaType(BasePin *iface, int iPosition, AM_MEDIA_TYPE *pmt)
{
    struct gstdemux_source *This = impl_source_from_IPin(&iface->IPin_iface);

    TRACE("(%p)->(%i, %p)\n", This, iPosition, pmt);

    if (iPosition < 0)
        return E_INVALIDARG;

    if (iPosition > 0)
        return VFW_S_NO_MORE_ITEMS;

    CopyMediaType(pmt, This->pmt);

    return S_OK;
}

static HRESULT WINAPI GSTOutPin_DecideBufferSize(struct strmbase_source *iface,
        IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *ppropInputRequest)
{
    struct gstdemux_source *This = impl_source_from_IPin(&iface->pin.IPin_iface);
    TRACE("(%p)->(%p, %p)\n", This, pAlloc, ppropInputRequest);
    /* Unused */
    return S_OK;
}

static HRESULT WINAPI GSTOutPin_DecideAllocator(struct strmbase_source *base,
        IMemInputPin *pPin, IMemAllocator **pAlloc)
{
    struct gstdemux_source *pin = impl_source_from_IPin(&base->pin.IPin_iface);
    struct gstdemux *filter = impl_from_strmbase_filter(pin->pin.pin.filter);
    HRESULT hr;

    TRACE("pin %p, peer %p, allocator %p.\n", pin, pPin, pAlloc);

    *pAlloc = NULL;
    if (filter->alloc)
    {
        hr = IMemInputPin_NotifyAllocator(pPin, filter->alloc, FALSE);
        if (SUCCEEDED(hr))
        {
            *pAlloc = filter->alloc;
            IMemAllocator_AddRef(*pAlloc);
        }
    }
    else
        hr = VFW_E_NO_ALLOCATOR;

    return hr;
}

static void free_source_pin(struct gstdemux_source *pin)
{
    if (pin->pin.pin.pConnectedTo)
    {
        if (SUCCEEDED(IMemAllocator_Decommit(pin->pin.pAllocator)))
            IPin_Disconnect(pin->pin.pin.pConnectedTo);
        IPin_Disconnect(&pin->pin.pin.IPin_iface);
    }

    if (pin->their_src)
    {
        if (pin->flipfilter)
        {
            gst_pad_unlink(pin->their_src, pin->flip_sink);
            gst_pad_unlink(pin->flip_src, pin->my_sink);
            gst_object_unref(pin->flip_src);
            gst_object_unref(pin->flip_sink);
            pin->flipfilter = NULL;
            pin->flip_src = pin->flip_sink = NULL;
        }
        else
            gst_pad_unlink(pin->their_src, pin->my_sink);
        gst_object_unref(pin->their_src);
    }
    gst_object_unref(pin->my_sink);
    CloseHandle(pin->caps_event);
    DeleteMediaType(pin->pmt);
    gst_segment_free(pin->segment);

    strmbase_source_cleanup(&pin->pin);
    heap_free(pin);
}

static const IPinVtbl GST_OutputPin_Vtbl = {
    GSTOutPin_QueryInterface,
    BasePinImpl_AddRef,
    BasePinImpl_Release,
    BaseOutputPinImpl_Connect,
    BaseOutputPinImpl_ReceiveConnection,
    BaseOutputPinImpl_Disconnect,
    BasePinImpl_ConnectedTo,
    BasePinImpl_ConnectionMediaType,
    BasePinImpl_QueryPinInfo,
    BasePinImpl_QueryDirection,
    BasePinImpl_QueryId,
    BasePinImpl_QueryAccept,
    BasePinImpl_EnumMediaTypes,
    BasePinImpl_QueryInternalConnections,
    BaseOutputPinImpl_EndOfStream,
    BaseOutputPinImpl_BeginFlush,
    BaseOutputPinImpl_EndFlush,
    BasePinImpl_NewSegment
};

static const struct strmbase_source_ops source_ops =
{
    {
        GSTOutPin_CheckMediaType,
        GSTOutPin_GetMediaType
    },
    BaseOutputPinImpl_AttemptConnection,
    GSTOutPin_DecideBufferSize,
    GSTOutPin_DecideAllocator,
};

static BOOL create_pin(struct gstdemux *filter, const WCHAR *name, const AM_MEDIA_TYPE *mt)
{
    struct gstdemux_source *pin, **new_array;

    if (!(new_array = heap_realloc(filter->ppPins, (filter->cStreams + 1) * sizeof(*new_array))))
        return FALSE;
    filter->ppPins = new_array;

    if (!(pin = heap_alloc_zero(sizeof(*pin))))
        return FALSE;

    strmbase_source_init(&pin->pin, &GST_OutputPin_Vtbl, &filter->filter, name,
            &source_ops);
    pin->pmt = heap_alloc(sizeof(AM_MEDIA_TYPE));
    CopyMediaType(pin->pmt, mt);
    pin->caps_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    pin->segment = gst_segment_new();
    pin->IQualityControl_iface.lpVtbl = &GSTOutPin_QualityControl_Vtbl;
    SourceSeeking_Init(&pin->seek, &GST_Seeking_Vtbl, GST_ChangeStop,
            GST_ChangeCurrent, GST_ChangeRate, &filter->filter.csFilter);
    BaseFilterImpl_IncrementPinVersion(&filter->filter);

    filter->ppPins[filter->cStreams++] = pin;
    return TRUE;
}

static HRESULT GST_RemoveOutputPins(struct gstdemux *This)
{
    ULONG i;

    TRACE("(%p)\n", This);
    mark_wine_thread();

    if (!This->container)
        return S_OK;
    gst_element_set_state(This->container, GST_STATE_NULL);
    gst_pad_unlink(This->my_src, This->their_sink);
    gst_object_unref(This->my_src);
    gst_object_unref(This->their_sink);
    This->my_src = This->their_sink = NULL;

    for (i = 0; i < This->cStreams; ++i)
        free_source_pin(This->ppPins[i]);

    This->cStreams = 0;
    heap_free(This->ppPins);
    This->ppPins = NULL;
    gst_element_set_bus(This->container, NULL);
    gst_object_unref(This->container);
    This->container = NULL;
    BaseFilterImpl_IncrementPinVersion(&This->filter);
    return S_OK;
}

static inline struct gstdemux *impl_from_sink_IPin(IPin *iface)
{
    return CONTAINING_RECORD(iface, struct gstdemux, sink.IPin_iface);
}

static HRESULT WINAPI GSTInPin_ReceiveConnection(IPin *iface, IPin *pReceivePin, const AM_MEDIA_TYPE *pmt)
{
    struct gstdemux *filter = impl_from_sink_IPin(iface);
    PIN_DIRECTION pindirReceive;
    HRESULT hr = S_OK;

    TRACE("filter %p, peer %p, mt %p.\n", filter, pReceivePin, pmt);
    dump_AM_MEDIA_TYPE(pmt);

    mark_wine_thread();

    EnterCriticalSection(&filter->filter.csFilter);
    if (!filter->sink.pConnectedTo)
    {
        ALLOCATOR_PROPERTIES props;
        IMemAllocator *pAlloc = NULL;

        props.cBuffers = 8;
        props.cbBuffer = 16384;
        props.cbAlign = 1;
        props.cbPrefix = 0;

        if (IPin_QueryAccept(iface, pmt) != S_OK)
            hr = VFW_E_TYPE_NOT_ACCEPTED;

        if (SUCCEEDED(hr)) {
            IPin_QueryDirection(pReceivePin, &pindirReceive);
            if (pindirReceive != PINDIR_OUTPUT) {
                ERR("Can't connect from non-output pin\n");
                hr = VFW_E_INVALID_DIRECTION;
            }
        }

        filter->reader = NULL;
        filter->alloc = NULL;
        if (SUCCEEDED(hr))
            hr = IPin_QueryInterface(pReceivePin, &IID_IAsyncReader, (LPVOID *)&filter->reader);
        if (SUCCEEDED(hr))
            hr = GST_Connect(filter, pReceivePin, &props);

        /* A certain IAsyncReader::RequestAllocator expects to be passed
           non-NULL preferred allocator */
        if (SUCCEEDED(hr))
            hr = CoCreateInstance(&CLSID_MemoryAllocator, NULL, CLSCTX_INPROC,
                                  &IID_IMemAllocator, (LPVOID *)&pAlloc);
        if (SUCCEEDED(hr)) {
            hr = IAsyncReader_RequestAllocator(filter->reader, pAlloc, &props, &filter->alloc);
            if (FAILED(hr))
                WARN("Can't get an allocator, got %08x\n", hr);
        }
        if (pAlloc)
            IMemAllocator_Release(pAlloc);
        if (SUCCEEDED(hr)) {
            CopyMediaType(&filter->sink.mtCurrent, pmt);
            filter->sink.pConnectedTo = pReceivePin;
            IPin_AddRef(pReceivePin);
            hr = IMemAllocator_Commit(filter->alloc);
        } else {
            GST_RemoveOutputPins(filter);
            if (filter->reader)
                IAsyncReader_Release(filter->reader);
            filter->reader = NULL;
            if (filter->alloc)
                IMemAllocator_Release(filter->alloc);
            filter->alloc = NULL;
        }
        TRACE("Size: %i\n", props.cbBuffer);
    } else
        hr = VFW_E_ALREADY_CONNECTED;
    LeaveCriticalSection(&filter->filter.csFilter);
    return hr;
}

static HRESULT WINAPI GSTInPin_Disconnect(IPin *iface)
{
    struct gstdemux *filter = impl_from_sink_IPin(iface);
    HRESULT hr;
    FILTER_STATE state;

    TRACE("filter %p.\n", filter);

    mark_wine_thread();

    hr = IBaseFilter_GetState(&filter->filter.IBaseFilter_iface, INFINITE, &state);
    EnterCriticalSection(&filter->filter.csFilter);
    if (filter->sink.pConnectedTo)
    {
        if (SUCCEEDED(hr) && state == State_Stopped) {
            IMemAllocator_Decommit(filter->alloc);
            IPin_Disconnect(filter->sink.pConnectedTo);
            IPin_Release(filter->sink.pConnectedTo);
            filter->sink.pConnectedTo = NULL;
            hr = GST_RemoveOutputPins(filter);
        } else
            hr = VFW_E_NOT_STOPPED;
    } else
        hr = S_FALSE;
    LeaveCriticalSection(&filter->filter.csFilter);
    return hr;
}

static HRESULT WINAPI GSTInPin_EndOfStream(IPin *iface)
{
    FIXME("iface %p, stub!\n", iface);
    return S_OK;
}

static HRESULT WINAPI GSTInPin_BeginFlush(IPin *iface)
{
    FIXME("iface %p, stub!\n", iface);
    return S_OK;
}

static HRESULT WINAPI GSTInPin_EndFlush(IPin *iface)
{
    FIXME("iface %p, stub!\n", iface);
    return S_OK;
}

static HRESULT WINAPI GSTInPin_NewSegment(IPin *iface, REFERENCE_TIME start,
        REFERENCE_TIME stop, double rate)
{
    FIXME("iface %p, start %s, stop %s, rate %.16e, stub!\n",
            iface, wine_dbgstr_longlong(start), wine_dbgstr_longlong(stop), rate);

    BasePinImpl_NewSegment(iface, start, stop, rate);
    return S_OK;
}

static HRESULT WINAPI GSTInPin_QueryInterface(IPin * iface, REFIID riid, LPVOID * ppv)
{
    struct gstdemux *filter = impl_from_sink_IPin(iface);

    TRACE("filter %p, riid %s, ppv %p.\n", filter, debugstr_guid(riid), ppv);

    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown))
        *ppv = iface;
    else if (IsEqualIID(riid, &IID_IPin))
        *ppv = iface;
    else if (IsEqualIID(riid, &IID_IMediaSeeking))
    {
        return IBaseFilter_QueryInterface(&filter->filter.IBaseFilter_iface, &IID_IMediaSeeking, ppv);
    }

    if (*ppv)
    {
        IUnknown_AddRef((IUnknown *)(*ppv));
        return S_OK;
    }

    FIXME("No interface for %s!\n", debugstr_guid(riid));

    return E_NOINTERFACE;
}

static const IPinVtbl GST_InputPin_Vtbl = {
    GSTInPin_QueryInterface,
    BasePinImpl_AddRef,
    BasePinImpl_Release,
    BaseInputPinImpl_Connect,
    GSTInPin_ReceiveConnection,
    GSTInPin_Disconnect,
    BasePinImpl_ConnectedTo,
    BasePinImpl_ConnectionMediaType,
    BasePinImpl_QueryPinInfo,
    BasePinImpl_QueryDirection,
    BasePinImpl_QueryId,
    BasePinImpl_QueryAccept,
    BasePinImpl_EnumMediaTypes,
    BasePinImpl_QueryInternalConnections,
    GSTInPin_EndOfStream,
    GSTInPin_BeginFlush,
    GSTInPin_EndFlush,
    GSTInPin_NewSegment
};

pthread_mutex_t cb_list_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cb_list_cond = PTHREAD_COND_INITIALIZER;
struct list cb_list = LIST_INIT(cb_list);

void CALLBACK perform_cb(TP_CALLBACK_INSTANCE *instance, void *user)
{
    struct cb_data *cbdata = user;

    TRACE("got cb type: 0x%x\n", cbdata->type);

    switch(cbdata->type)
    {
    case WATCH_BUS:
        {
            struct watch_bus_data *data = &cbdata->u.watch_bus_data;
            cbdata->u.watch_bus_data.ret = watch_bus(data->bus, data->msg, data->user);
            break;
        }
    case EXISTING_NEW_PAD:
        {
            struct existing_new_pad_data *data = &cbdata->u.existing_new_pad_data;
            existing_new_pad(data->bin, data->pad, data->user);
            break;
        }
    case QUERY_FUNCTION:
        {
            struct query_function_data *data = &cbdata->u.query_function_data;
            cbdata->u.query_function_data.ret = query_function(data->pad, data->parent, data->query);
            break;
        }
    case ACTIVATE_MODE:
        {
            struct activate_mode_data *data = &cbdata->u.activate_mode_data;
            cbdata->u.activate_mode_data.ret = activate_mode(data->pad, data->parent, data->mode, data->activate);
            break;
        }
    case NO_MORE_PADS:
        {
            struct no_more_pads_data *data = &cbdata->u.no_more_pads_data;
            no_more_pads(data->decodebin, data->user);
            break;
        }
    case REQUEST_BUFFER_SRC:
        {
            struct request_buffer_src_data *data = &cbdata->u.request_buffer_src_data;
            cbdata->u.request_buffer_src_data.ret = request_buffer_src(data->pad, data->parent,
                    data->ofs, data->len, data->buf);
            break;
        }
    case EVENT_SRC:
        {
            struct event_src_data *data = &cbdata->u.event_src_data;
            cbdata->u.event_src_data.ret = event_src(data->pad, data->parent, data->event);
            break;
        }
    case EVENT_SINK:
        {
            struct event_sink_data *data = &cbdata->u.event_sink_data;
            cbdata->u.event_sink_data.ret = event_sink(data->pad, data->parent, data->event);
            break;
        }
    case GOT_DATA_SINK:
        {
            struct got_data_sink_data *data = &cbdata->u.got_data_sink_data;
            cbdata->u.got_data_sink_data.ret = got_data_sink(data->pad, data->parent, data->buf);
            break;
        }
    case GOT_DATA:
        {
            struct got_data_data *data = &cbdata->u.got_data_data;
            cbdata->u.got_data_data.ret = got_data(data->pad, data->parent, data->buf);
            break;
        }
    case REMOVED_DECODED_PAD:
        {
            struct removed_decoded_pad_data *data = &cbdata->u.removed_decoded_pad_data;
            removed_decoded_pad(data->bin, data->pad, data->user);
            break;
        }
    case AUTOPLUG_BLACKLIST:
        {
            struct autoplug_blacklist_data *data = &cbdata->u.autoplug_blacklist_data;
            cbdata->u.autoplug_blacklist_data.ret = autoplug_blacklist(data->bin,
                    data->pad, data->caps, data->fact, data->user);
            break;
        }
    case UNKNOWN_TYPE:
        {
            struct unknown_type_data *data = &cbdata->u.unknown_type_data;
            unknown_type(data->bin, data->pad, data->caps, data->user);
            break;
        }
    case RELEASE_SAMPLE:
        {
            struct release_sample_data *data = &cbdata->u.release_sample_data;
            release_sample(data->data);
            break;
        }
    case TRANSFORM_PAD_ADDED:
        {
            struct transform_pad_added_data *data = &cbdata->u.transform_pad_added_data;
            Gstreamer_transform_pad_added(data->filter, data->pad, data->user);
            break;
        }
    case QUERY_SINK:
        {
            struct query_sink_data *data = &cbdata->u.query_sink_data;
            cbdata->u.query_sink_data.ret = query_sink(data->pad, data->parent,
                    data->query);
            break;
        }
    }

    pthread_mutex_lock(&cbdata->lock);
    cbdata->finished = 1;
    pthread_cond_broadcast(&cbdata->cond);
    pthread_mutex_unlock(&cbdata->lock);
}

static DWORD WINAPI dispatch_thread(void *user)
{
    struct cb_data *cbdata;

    pthread_mutex_lock(&cb_list_lock);

    while(1){
        pthread_cond_wait(&cb_list_cond, &cb_list_lock);

        while(!list_empty(&cb_list)){
            cbdata = LIST_ENTRY(list_head(&cb_list), struct cb_data, entry);
            list_remove(&cbdata->entry);

            TrySubmitThreadpoolCallback(&perform_cb, cbdata, NULL);
        }
    }

    pthread_mutex_unlock(&cb_list_lock);

    return 0;
}

void start_dispatch_thread(void)
{
    pthread_key_create(&wine_gst_key, NULL);
    CloseHandle(CreateThread(NULL, 0, &dispatch_thread, NULL, 0, NULL));
}
