/**
 *
 * ToxBlinkenwall
 * (C)Zoff <zoff@zoff.cc> in 2017 - 2019
 *
 * https://github.com/zoff99/ToxBlinkenwall
 *
 */
/**
 * @file omx.c     Raspberry Pi VideoCoreIV OpenMAX interface
 *
 * Copyright (C) 2016 - 2017 Creytiv.com
 * Copyright (C) 2016 - 2017 Jonathan Sieber
 * Copyright (C) 2018 - 2019 Zoff <zoff@zoff.cc>
 */


#include "omx.h"

/*
 * forward func declarations (acutal functions in toxblinkenwall.c)
 */
void dbg(int level, const char *fmt, ...);

void usleep_usec2(uint64_t usec)
{
    struct timespec ts;
    ts.tv_sec = usec / 1000000;
    ts.tv_nsec = (usec % 1000000) * 1000;
    nanosleep(&ts, NULL);
}


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Avoids a VideoCore header warning about clock_gettime() */
#include <time.h>
#include <sys/time.h>

#include <bcm_host.h>

/**
 * @defgroup omx omx
 *
 * TODO:
 *  * Proper sync OMX events across threads, instead of busy waiting
 */

static const int VIDEO_RENDER_PORT = 90;
static void *global_st_buffers = 0;

static OMX_ERRORTYPE EventHandler(OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
                                  OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2,
                                  OMX_PTR pEventData)
{
    (void) hComponent;

    switch (eEvent)
    {
        case OMX_EventCmdComplete:
            if (nData1 == OMX_CommandFlush)
            {
                printf("omx.EventHandler: OMX_CommandFlush command completed\n"
                    "d1=%x\td2=%x\teventData=%p\tappdata=%p\n",
                    nData1, nData2, pEventData, pAppData);
            }
            else if (nData1 == OMX_CommandStateSet)
            {
                printf("omx.EventHandler: OMX_CommandStateSet command completed\n"
                    "d1=%x\td2=%x\teventData=%p\tappdata=%p\n",
                    nData1, nData2, pEventData, pAppData);
            }
            else if (nData1 == OMX_CommandPortEnable)
            {
                printf("omx.EventHandler: OMX_CommandPortEnable command completed\n"
                    "d1=%x\td2=%x\teventData=%p\tappdata=%p\n",
                    nData1, nData2, pEventData, pAppData);
            }
            else if (nData1 == OMX_CommandPortDisable)
            {
                printf("omx.EventHandler: OMX_CommandPortDisable command completed\n"
                    "d1=%x\td2=%x\teventData=%p\tappdata=%p\n",
                    nData1, nData2, pEventData, pAppData);
            }
            else
            {
                printf("omx.EventHandler: Previous command completed\n"
                    "d1=%x\td2=%x\teventData=%p\tappdata=%p\n",
                    nData1, nData2, pEventData, pAppData);
            }

            break;

        case OMX_EventParamOrConfigChanged:
            printf("omx.EventHandler: Param_or_config changed event type "
                "hComponent:0x%08x\tdata1=%x\tdata2=%x\n",
                hComponent, nData1, nData2);
            break;

        case OMX_EventError:
            if (nData1 == OMX_CommandStateSet)
            {
                printf("omx.EventHandler: OMX_EventError: OMX_CommandStateSet\n");
            }
            else if (nData1 == OMX_CommandPortEnable)
            {
                printf("omx.EventHandler: OMX_EventError: OMX_CommandPortEnable\n");
            }

            printf("omx.EventHandler: Error event type "
                "hComponent:0x%08x\tdata1=%x\tdata2=%x\n",
                hComponent, nData1, nData2);
            break;

        case OMX_EventPortSettingsChanged:
            printf("omx.EventHandler: EventPortSettingsChanged event type "
                "data1=%x\tdata2=%x\n", nData1, nData2);
            break;

        default:
            printf("omx.EventHandler: Unknown event type %d\t"
                "data1=%x data2=%x\n", eEvent, nData1, nData2);
            return -1;
            break;
    }

    return 0;
}


static OMX_ERRORTYPE EmptyBufferDone(OMX_HANDLETYPE hComponent,
                                     OMX_PTR pAppData,
                                     OMX_BUFFERHEADERTYPE *pBuffer)
{
    (void) hComponent;
    (void) pAppData;
    (void) pBuffer;
    /* TODO: Wrap every call that can generate an event,
     * and panic if an unexpected event arrives */

    // printf("omx.EmptyBufferDone:called:ptr=%p pnum=%d\n", (void *)pBuffer, (int)((void *)pBuffer - (void *)global_st_buffers));

    return 0;
}


static OMX_ERRORTYPE FillBufferDone(OMX_HANDLETYPE hComponent,
                                    OMX_PTR pAppData, OMX_BUFFERHEADERTYPE *pBuffer)
{
    (void) hComponent;
    (void) pAppData;
    (void) pBuffer;
    // printf("FillBufferDone\n");
    return 0;
}


static struct OMX_CALLBACKTYPE callbacks =
{
    EventHandler,
    EmptyBufferDone,
    &FillBufferDone
};

static void free_omx_buffers(struct omx_state *st)
{
    if (!st)
    {
        return;
    }

    if (!st->video_render)
    {
        return;
    }

    OMX_ERRORTYPE err;

    // free OMX buffers
    if (st->buffers)
    {
        for (int i = 0; i < st->num_buffers; i++)
        {
            printf("OMX_FreeBuffer: %d\n", i);
            err = OMX_FreeBuffer(st->video_render, VIDEO_RENDER_PORT, st->buffers[i]);

            if (err != OMX_ErrorNone)
            {
                printf("OMX_FreeBuffer failed: %d\n", err);
            }
            else
            {
                printf("OMX_FreeBuffer OK: %d\n", err);
                usleep_usec2(1000);
            }
        }

        usleep_usec2(1000);
        printf("OMX_FreeBuffer *DONE*\n");
        free(st->buffers);
        st->buffers = NULL;
        st->num_buffers = 0;
        st->current_buffer = 0;
    }

    global_st_buffers = 0;
}

static void block_until_flushed(void)
{
    // TODO: write me: wait until buffers are actually flushed
    usleep_usec2(25000);
}

int omx_init(struct omx_state *st)
{
    OMX_ERRORTYPE err;
    bcm_host_init();

    if (!st)
    {
        return ENOENT;
    }
    st->buffers=NULL;

    err = OMX_Init();
    err |= OMX_GetHandle(&st->video_render, "OMX.broadcom.video_render", 0, &callbacks);

    if (!st->video_render || err != 0)
    {
        printf("omx: Failed to create OMX video_render component\n");
        return ENOENT;
    }
    else
    {
        printf("omx: created video_render component\n");
        return 0;
    }
}


/* Some busy loops to verify we're running in order */
static void block_until_state_changed(OMX_HANDLETYPE hComponent,   OMX_STATETYPE wanted_eState)
{
    OMX_STATETYPE eState;
    unsigned int i = 0;
    uint32_t loop_counter = 0;

    while (i++ == 0 || eState != wanted_eState)
    {
        OMX_GetState(hComponent, &eState);

        if (eState != wanted_eState)
        {
            usleep_usec2(1000);
        }

        loop_counter++;

        if (loop_counter > 200)
        {
            printf("OMX: block_until_state_changed: TIMEOUT\n");
            // we don't want to get stuck here
            break;
        }
    }
}


void omx_deinit(struct omx_state *st)
{
    if (!st)
    {
        return;
    }

    if (!st->video_render)
    {
        return;
    }

    printf("omx_deinit:005\n");
    OMX_FreeHandle(st->video_render);
    usleep_usec2(20000);
    printf("omx_deinit:006\n");
    OMX_Deinit();
    printf("omx_deinit:099\n");
}

static void block_until_port_changed(OMX_HANDLETYPE hComponent,
                                     OMX_U32 nPortIndex, OMX_BOOL bEnabled)
{
    OMX_ERRORTYPE r;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    OMX_U32 i = 0;
    memset(&portdef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    portdef.nVersion.nVersion = OMX_VERSION;
    portdef.nPortIndex = nPortIndex;
    uint32_t    loop_counter = 0;

    while (i++ == 0 || portdef.bEnabled != bEnabled)
    {
        r = OMX_GetParameter(hComponent,
                             OMX_IndexParamPortDefinition, &portdef);

        if (r != OMX_ErrorNone)
        {
            printf("OMX: block_until_port_changed: OMX_GetParameter "
                " failed with Result=%d\n", r);
        }

        if (portdef.bEnabled != bEnabled)
        {
            usleep_usec2(1000);
        }

        loop_counter++;

        if (loop_counter > 200)
        {
            printf("OMX: block_until_port_changed: TIMEOUT\n");
            // we don't want to get stuck here
            break;
        }
    }
}

void omx_display_disable(struct omx_state *st)
{
    printf("omx_display_disable\n");
    OMX_ERRORTYPE err;
    OMX_CONFIG_DISPLAYREGIONTYPE config;

    if (!st)
    {
        return;
    }

    if (!st->video_render)
    {
        return;
    }

    printf("OMX flush buffers\n");

    if ((err = OMX_SendCommand(st->video_render, OMX_CommandFlush, VIDEO_RENDER_PORT, NULL)) != OMX_ErrorNone)
    {
        printf("OMX Failed to flush buffers: %d\n", (int)err);
    }

    block_until_flushed();
    //
    //
    //
    //
    //
    //
    printf("OMX switch state of the encoder component to idle\n");

    if ((err = OMX_SendCommand(st->video_render, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone)
    {
        printf("OMX: Failed to switch state of the encoder component to idle: %d\n", (int)err);
    }

    block_until_state_changed(st->video_render, OMX_StateIdle);
    //
    //
    //
    //
    //
    //
    // Disable the port
    printf("OMX disable port\n");

    if ((err = OMX_SendCommand(st->video_render, OMX_CommandPortDisable, VIDEO_RENDER_PORT, NULL)) != OMX_ErrorNone)
    {
        printf("OMX Failed to disable port: %d\n", (int)err);
    }

    //
    //
    //
    printf("OMX free buffers\n");
    free_omx_buffers(st);
    //
    //
    //
    //
    //
    //
    block_until_port_changed(st->video_render, VIDEO_RENDER_PORT, OMX_FALSE);
    //
    //
    //
    printf("OMX switch state of the encoder component to loaded\n");

    if ((err = OMX_SendCommand(st->video_render, OMX_CommandStateSet, OMX_StateLoaded, NULL)) != OMX_ErrorNone)
    {
        printf("OMX: Failed to switch state of the encoder component to loaded: %d\n", (int)err);
    }

    block_until_state_changed(st->video_render, OMX_StateLoaded);
}

#ifdef DEBUG_OMX_TEST_PROGRAM

int omx_display_xy(int flag, struct omx_state *st,
                   int width, int height, int stride)
{
    unsigned int i;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    OMX_CONFIG_DISPLAYREGIONTYPE config;
    OMX_ERRORTYPE err = 0;
    printf("omx_update_size %d %d %d\n", width, height, stride);
    memset(&config, 0, sizeof(OMX_CONFIG_DISPLAYREGIONTYPE));
    config.nSize = sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
    config.nVersion.nVersion = OMX_VERSION;
    config.nPortIndex = VIDEO_RENDER_PORT;
    //
    //
    //
    //
    config.dest_rect.x_offset  = 0;
    config.dest_rect.y_offset  = 0;
    config.dest_rect.width     = (int)(1920 / 2);
    config.dest_rect.height    = (int)(1080 / 2);
    config.mode = OMX_DISPLAY_MODE_LETTERBOX;

    if (flag == 0)
    {
        config.transform = OMX_DISPLAY_ROT0;
    }
    else
    {
        config.transform = OMX_DISPLAY_ROT90;
    }

    config.fullscreen = OMX_FALSE;
    config.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_TRANSFORM | OMX_DISPLAY_SET_DEST_RECT |
                                      OMX_DISPLAY_SET_FULLSCREEN | OMX_DISPLAY_SET_MODE);
    //
    printf("OMX_SetParameter\n");
    err = OMX_SetParameter(st->video_render,
                           OMX_IndexConfigDisplayRegion, &config);
    return err;
}

#endif


int omx_change_video_out_rotation(struct omx_state *st, int angle)
{
    if (!st)
    {
        return 1;
    }

    if (!st->video_render)
    {
        return 1;
    }

    unsigned int i;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    OMX_CONFIG_DISPLAYREGIONTYPE config;
    OMX_ERRORTYPE err = 0;
    memset(&config, 0, sizeof(OMX_CONFIG_DISPLAYREGIONTYPE));
    config.nSize = sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
    config.nVersion.nVersion = OMX_VERSION;
    config.nPortIndex = VIDEO_RENDER_PORT;

    if (angle == 90)
    {
        config.transform = OMX_DISPLAY_ROT90;
    }
    else if (angle == 180)
    {
        config.transform = OMX_DISPLAY_ROT180;
    }
    else if (angle == 270)
    {
        config.transform = OMX_DISPLAY_ROT270;
    }
    else
    {
        // for any other angle value just assume "0" degrees
        config.transform = OMX_DISPLAY_ROT0;
    }

    //
    config.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_TRANSFORM);
    //
    printf("OMX_SetParameter(2)\n");
    err |= OMX_SetParameter(st->video_render,
                            OMX_IndexConfigDisplayRegion, &config);
    return err;
}

int omx_display_enable(struct omx_state *st, int width, int height, int stride)
{
    unsigned int i;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    OMX_CONFIG_DISPLAYREGIONTYPE config;
    OMX_ERRORTYPE err = 0;
    //
    printf("OMX command state set IDLE\n");

    if ((err = OMX_SendCommand(st->video_render, OMX_CommandStateSet, OMX_StateIdle, NULL)) != OMX_ErrorNone)
    {
        printf("OMX Failed to set IDLE state: %d\n", (int)err);
    }

    //
    printf("omx_update_size %d %d %d\n", width, height, stride);
    memset(&config, 0, sizeof(OMX_CONFIG_DISPLAYREGIONTYPE));
    config.nSize = sizeof(OMX_CONFIG_DISPLAYREGIONTYPE);
    config.nVersion.nVersion = OMX_VERSION;
    config.nPortIndex = VIDEO_RENDER_PORT;
    //
    config.fullscreen = OMX_TRUE;
    config.mode = OMX_DISPLAY_MODE_LETTERBOX;
    config.set = (OMX_DISPLAYSETTYPE)(OMX_DISPLAY_SET_FULLSCREEN | OMX_DISPLAY_SET_MODE);
    //
    printf("OMX_SetParameter(3)\n");
    err = OMX_SetParameter(st->video_render, OMX_IndexConfigDisplayRegion, &config);

    if (err != 0)
    {
        printf("omx_display_enable: couldn't configure display region: %d\n", (int)err);
    }

    memset(&portdef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    portdef.nVersion.nVersion = OMX_VERSION;
    portdef.nPortIndex = VIDEO_RENDER_PORT;
    /* specify buffer requirements */
    printf("OMX_GetParameter\n");
    err = OMX_GetParameter(st->video_render,  OMX_IndexParamPortDefinition, &portdef);

    if (err != 0)
    {
        printf("omx_display_enable: couldn't retrieve port def: %d\n", (int)err);
        err = ENOMEM;
        goto exit;
    }

    //
    printf("omx port definition(before): h=%d w=%d s=%d sh=%d enable=%d\n",
        portdef.format.video.nFrameWidth,
        portdef.format.video.nFrameHeight,
        portdef.format.video.nStride,
        portdef.format.video.nSliceHeight,
        portdef.bEnabled);
    //



    portdef.format.video.nFrameWidth = width;
    portdef.format.video.nFrameHeight = height;
    portdef.format.video.nStride = stride;
    portdef.format.video.nSliceHeight = height;
    //
    portdef.bEnabled = 1;
    //
    printf("omx port definition(after): h=%d w=%d s=%d sh=%d enable=%d\n",
        portdef.format.video.nFrameWidth,
        portdef.format.video.nFrameHeight,
        portdef.format.video.nStride,
        portdef.format.video.nSliceHeight,
        portdef.bEnabled);
    //

    int want_buffers = 4;

    portdef.nBufferCountActual = want_buffers;
    if ((int)portdef.nBufferCountActual < (int)portdef.nBufferCountMin)
    {
        portdef.nBufferCountActual = portdef.nBufferCountMin;
        want_buffers = portdef.nBufferCountMin;
    }

    printf("OMX_SetParameter(4)\n");
    err = OMX_SetParameter(st->video_render, OMX_IndexParamPortDefinition, &portdef);

    if (err)
    {
        printf("omx_display_enable: could not set port definition: %d\n", (int)err);
        err = EINVAL;
        goto exit;
    }

    printf("OMX_GetParameter(2)\n");
    err = OMX_GetParameter(st->video_render, OMX_IndexParamPortDefinition, &portdef);

    if (portdef.format.video.nFrameWidth != width || portdef.format.video.nFrameHeight != height || portdef.format.video.nStride != stride)
    {
        printf("OMX: could not set requested resolution: %d\n", (int)err);
        err = EINVAL;
        goto exit;
    }
    if(portdef.bEnabled != 1){//如果端口关闭就打开 by aphero
		err |=OMX_SendCommand(st->video_render,OMX_CommandPortEnable, VIDEO_RENDER_PORT, NULL);//前面关闭了，一定要打开,aphero
	}
    if (!st->buffers)
    {
        printf("OMX_Buffer_counts:nBufferCountActual=%d want_buffers=%d nBufferCountMin=%d\n",
        (int)portdef.nBufferCountActual,
        (int)want_buffers,
        (int)portdef.nBufferCountMin);

        st->buffers = calloc(1, want_buffers * sizeof(void *));
        global_st_buffers = (void *)st->buffers;
        st->num_buffers = want_buffers;
        st->current_buffer = 0;

        for (i = 0; i < want_buffers; i++)
        {
            printf("OMX_AllocateBuffer\n");
            err = OMX_AllocateBuffer(st->video_render, &st->buffers[i], VIDEO_RENDER_PORT, st, portdef.nBufferSize);

            if (err)
            {
                printf("OMX_AllocateBuffer failed: %d\n", (int)err);
                err = ENOMEM;
                goto exit;
            }
        }
    }

    //
    //
    //
    block_until_state_changed(st->video_render, OMX_StateIdle);
    //
    //
    //
    printf("OMX: ===================\n");
    printf("OMX: ===================\n");
    printf("OMX: ===================\n");
    printf("OMX: command port enable VIDEO RENDER PORT\n");

    if ((err = OMX_SendCommand(st->video_render, OMX_CommandPortEnable, VIDEO_RENDER_PORT, NULL)) != OMX_ErrorNone)
    {
        printf("OMX Failed to enable port: %d\n", (int)err);
    }

    block_until_port_changed(st->video_render, VIDEO_RENDER_PORT, OMX_TRUE);
    usleep_usec2(10000);
    printf("OMX: ===================\n");
    printf("OMX: ===================\n");
    printf("OMX: ===================\n");
    printf("OMX: omx_update_size: send to execute state\n");
    OMX_SendCommand(st->video_render, OMX_CommandStateSet,
                    OMX_StateExecuting, NULL);

    if (err)
    {
        printf("omx_update_size: send to execute state ERROR: %d\n", (int)err);
    }

    block_until_state_changed(st->video_render, OMX_StateExecuting);
exit:
    return err;
}

int omx_display_input_buffer(struct omx_state *st,
                                 void **pbuf, uint32_t *plen)
{
    int buf_idx;

    if (!st)
    {
        return EINVAL;
    }

    if (!st->buffers)
    {
        return EINVAL;
    }

    st->current_buffer = (st->current_buffer + 1) % st->num_buffers;
    buf_idx = st->current_buffer;
    *pbuf = st->buffers[buf_idx]->pBuffer;
    *plen = st->buffers[buf_idx]->nAllocLen;
    st->buffers[buf_idx]->nFilledLen = *plen;
    st->buffers[buf_idx]->nOffset = 0;

    // printf("omx_get_display_input_buffer:idx=%d ptr=%p pnum=%d\n", buf_idx, (void *)st->buffers[buf_idx], (int)((void *)st->buffers[buf_idx] - (void *)st->buffers));

    return 0;
}

static OMX_TICKS to_omx_ticks(int64_t value)
{
    OMX_TICKS s;
    s.nLowPart  = value & 0xffffffff;
    s.nHighPart = value >> 32;
    return s;
}

int omx_display_flush_buffer(struct omx_state *st, uint32_t data_len, uint32_t ms_offset)
{
    if (!st)
    {
        return 1;
    }

    if (!st->video_render)
    {
        return 1;
    }

    int buf_idx = st->current_buffer;
    st->buffers[buf_idx]->nFlags = OMX_BUFFERFLAG_STARTTIME; // OMX_BUFFERFLAG_ENDOFFRAME; // OMX_BUFFERFLAG_STARTTIME;
    st->buffers[buf_idx]->nOffset = 0;
    st->buffers[buf_idx]->nFilledLen = data_len;
    st->buffers[buf_idx]->nTimeStamp = omx_ticks_from_s64(0); // to_omx_ticks(ms_offset * 1000); // omx_ticks_from_s64(ms_offset);

    if (OMX_EmptyThisBuffer(st->video_render, st->buffers[buf_idx]) != OMX_ErrorNone)
    {
        printf("OMX_EmptyThisBuffer error\n");
    }
    else
    {
        // printf("omx_display_flush_buffer:idx=%d ptr=%p pnum=%d\n", buf_idx, (void *)st->buffers[buf_idx], (int)((void *)st->buffers[buf_idx] - (void *)st->buffers));
    }

    return 0;
}