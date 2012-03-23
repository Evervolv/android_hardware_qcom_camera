/*
 * Copyright (C) 2012 Raviprasad V Mummidi.
 * Copyright (c) 2011 Code Aurora Forum. All rights reserved.
 *
 * Modified by Andrew Sutherland <dr3wsuth3rland@gmail.com>
 *              for The Evervolv Project's qsd8k lineup
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "QcomCamera"

#include <hardware/hardware.h>
#include <binder/IMemory.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/msm_mdp.h>
#include <gralloc_priv.h>

#include "CameraHardwareInterface.h"
/* include QCamera Hardware Interface Header*/
#include "QcomCamera.h"
//#include "QualcommCameraHardware.h"

extern "C" {
#include <sys/time.h>
}

/* HAL function implementation goes here*/

/**
 * The functions need to be provided by the camera HAL.
 *
 * If getNumberOfCameras() returns N, the valid cameraId for getCameraInfo()
 * and openCameraHardware() is 0 to N-1.
 */

struct qcom_mdp_rect {
   uint32_t x;
   uint32_t y;
   uint32_t w;
   uint32_t h;
};

struct qcom_mdp_img {
   uint32_t width;
   int32_t  height;
   int32_t  format;
   int32_t  offset;
   int      memory_id; /* The file descriptor */
};

struct qcom_mdp_blit_req {
   struct   qcom_mdp_img src;
   struct   qcom_mdp_img dst;
   struct   qcom_mdp_rect src_rect;
   struct   qcom_mdp_rect dst_rect;
   uint32_t alpha;
   uint32_t transp_mask;
   uint32_t flags;
};

struct blitreq {
   unsigned int count;
   struct qcom_mdp_blit_req req;
};

/* Prototypes and extern functions. */
extern "C" android::sp<android::CameraHardwareInterface> openCameraHardware(int id);

/* Global variables. */
camera_notify_callback         origNotify_cb    = NULL;
camera_data_callback           origData_cb      = NULL;
camera_data_timestamp_callback origDataTS_cb    = NULL;
camera_request_memory          origCamReqMemory = NULL;

android::CameraParameters camSettings;
preview_stream_ops_t      *mWindow = NULL;
android::sp<android::CameraHardwareInterface> qCamera;


static hw_module_methods_t camera_module_methods = {
   open: camera_device_open
};

static hw_module_t camera_common  = {
  tag: HARDWARE_MODULE_TAG,
  version_major: 1,
  version_minor: 0,
  id: CAMERA_HARDWARE_MODULE_ID,
  name: "Camera HAL for ICS",
  author: "Raviprasad V Mummidi",
  methods: &camera_module_methods,
  dso: NULL,
  reserved: {0},
};

camera_module_t HAL_MODULE_INFO_SYM = {
  common: camera_common,
  get_number_of_cameras: get_number_of_cameras,
  get_camera_info: get_camera_info,
};

#if 0 //TODO: use this instead of declaring in camera_device_open
              it works fine with this but segfaults when
              closing the camera app, so that needs to be addressed.

camera_device_ops_t camera_ops = {
  set_preview_window:         android::set_preview_window,
  set_callbacks:              android::set_callbacks,
  enable_msg_type:            android::enable_msg_type,
  disable_msg_type:           android::disable_msg_type,
  msg_type_enabled:           android::msg_type_enabled,

  start_preview:              android::start_preview,
  stop_preview:               android::stop_preview,
  preview_enabled:            android::preview_enabled,
  store_meta_data_in_buffers: android::store_meta_data_in_buffers,

  start_recording:            android::start_recording,
  stop_recording:             android::stop_recording,
  recording_enabled:          android::recording_enabled,
  release_recording_frame:    android::release_recording_frame,

  auto_focus:                 android::auto_focus,
  cancel_auto_focus:          android::cancel_auto_focus,

  take_picture:               android::take_picture,
  cancel_picture:             android::cancel_picture,

  set_parameters:             android::set_parameters,
  get_parameters:             android::get_parameters,
  put_parameters:             android::put_parameters,
  send_command:               android::send_command,

  release:                    android::release,
  dump:                       android::dump,
};
#endif

namespace android {

/* HAL helper functions. */
void
CameraHAL_CopyBuffers_Hw(int srcFd, int destFd,
                         size_t srcOffset, size_t destOffset,
                         int srcFormat, int destFormat,
                         int x, int y, int w, int h)
{
    struct blitreq blit;
    int    fb_fd = open("/dev/graphics/fb0", O_RDWR);

    if (fb_fd < 0) {
       LOGE("CameraHAL_CopyBuffers_Hw: Error opening /dev/graphics/fb0");
       return;
    }

    LOGE("CameraHAL_CopyBuffers_Hw: srcFD:%d destFD:%d srcOffset:%#x"
         " destOffset:%#x x:%d y:%d w:%d h:%d", srcFd, destFd, srcOffset,
         destOffset, x, y, w, h);

    memset(&blit, 0, sizeof(blit));
    blit.count = 1;

    blit.req.flags       = 0;
    blit.req.alpha       = 0xff;
    blit.req.transp_mask = 0xffffffff;

    blit.req.src.width     = w;
    blit.req.src.height    = h;
    blit.req.src.offset    = srcOffset;
    blit.req.src.memory_id = srcFd;
    blit.req.src.format    = srcFormat;

    blit.req.dst.width     = w;
    blit.req.dst.height    = h;
    blit.req.dst.offset    = destOffset;
    blit.req.dst.memory_id = destFd;
    blit.req.dst.format    = destFormat;

    blit.req.src_rect.x = blit.req.dst_rect.x = x;
    blit.req.src_rect.y = blit.req.dst_rect.y = y;
    blit.req.src_rect.w = blit.req.dst_rect.w = w;
    blit.req.src_rect.h = blit.req.dst_rect.h = h;

    if (ioctl(fb_fd, MSMFB_BLIT, &blit)) {
       LOGE("CameraHAL_CopyBuffers_Hw: MSMFB_BLIT failed = %d %s",
            errno, strerror(errno));
    }
    close(fb_fd);
}

void
CameraHAL_HandlePreviewData(const sp<IMemory>& dataPtr,
                            preview_stream_ops_t *mWindow,
                            camera_request_memory getMemory,
                            int32_t previewWidth, int32_t previewHeight)
{
   if (mWindow != NULL && getMemory != NULL) {
      ssize_t  offset;
      size_t   size;
      int32_t  previewFormat = MDP_Y_CBCR_H2V2;
      int32_t  destFormat    = MDP_RGBX_8888;

      status_t retVal;
      sp<IMemoryHeap> mHeap = dataPtr->getMemory(&offset,
                                                                   &size);

      LOGE("CameraHAL_HandlePreviewData: previewWidth:%d previewHeight:%d "
           "offset:%#x size:%#x base:%p", previewWidth, previewHeight,
           (unsigned)offset, size, mHeap != NULL ? mHeap->base() : 0);

      retVal = mWindow->set_buffers_geometry(mWindow,
                                             previewWidth, previewHeight,
                                             HAL_PIXEL_FORMAT_RGBX_8888);
      if (retVal == NO_ERROR) {
         int32_t          stride;
         buffer_handle_t *bufHandle = NULL;

         retVal = mWindow->dequeue_buffer(mWindow, &bufHandle, &stride);
         if (retVal == NO_ERROR) {
            retVal = mWindow->lock_buffer(mWindow, bufHandle);
            if (retVal == NO_ERROR) {
               private_handle_t const *privHandle =
                  reinterpret_cast<private_handle_t const *>(*bufHandle);
               CameraHAL_CopyBuffers_Hw(mHeap->getHeapID(), privHandle->fd,
                                        offset, privHandle->offset,
                                        previewFormat, destFormat,
                                        0, 0, previewWidth, previewHeight);
               mWindow->enqueue_buffer(mWindow, bufHandle);
            } else {
               LOGE("CameraHAL_HandlePreviewData: ERROR locking the buffer");
               mWindow->cancel_buffer(mWindow, bufHandle);
            }
         } else {
            LOGE("CameraHAL_HandlePreviewData: ERROR dequeueing the buffer");
         }
      }
   }
}

camera_memory_t * CameraHAL_GenClientData(const sp<IMemory> &dataPtr,
                        camera_request_memory reqClientMemory,
                        void *user)
{
   ssize_t          offset;
   size_t           size;
   camera_memory_t *clientData = NULL;
   sp<IMemoryHeap> mHeap = dataPtr->getMemory(&offset, &size);

   LOGE("CameraHAL_GenClientData: offset:%#x size:%#x base:%p",
        (unsigned)offset, size, mHeap != NULL ? mHeap->base() : 0);

   clientData = reqClientMemory(-1, size, 1, user);
   if (clientData != NULL) {
      memcpy(clientData->data, (char *)(mHeap->base()) + offset, size);
   } else {
      LOGE("CameraHAL_GenClientData: ERROR allocating memory from client");
   }
   return clientData;
}

void CameraHAL_FixupParams(CameraParameters &settings)
{
   const char *preview_sizes =
      "1280x720,800x480,768x432,720x480,640x480,576x432,480x320,384x288,352x288,320x240,240x160,176x144";
   const char *video_sizes =
      "1280x720,800x480,720x480,640x480,352x288,320x240,176x144";
   const char *preferred_size       = "640x480";
   const char *preview_frame_rates  = "30,27,24,15";
   const char *preferred_frame_rate = "15";

   settings.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT,
                CameraParameters::PIXEL_FORMAT_YUV420SP);

   if (!settings.get(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES)) {
      settings.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
                   preview_sizes);
   }

   if (!settings.get(CameraParameters::KEY_SUPPORTED_VIDEO_SIZES)) {
      settings.set(CameraParameters::KEY_SUPPORTED_VIDEO_SIZES,
                   video_sizes);
   }

   if (!settings.get(CameraParameters::KEY_VIDEO_SIZE)) {
      settings.set(CameraParameters::KEY_VIDEO_SIZE, preferred_size);
   }

   if (!settings.get(CameraParameters::KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO)) {
      settings.set(CameraParameters::KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO,
                   preferred_size);
   }

   if (!settings.get(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES)) {
      settings.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,
                   preview_frame_rates);
   }

   if (!settings.get(CameraParameters::KEY_PREVIEW_FRAME_RATE)) {
      settings.set(CameraParameters::KEY_PREVIEW_FRAME_RATE,
                   preferred_frame_rate);
   }
}

static void camera_release_memory(struct camera_memory *mem) { }

void cam_notify_callback(int32_t msg_type, int32_t ext1,
                   int32_t ext2, void *user)
{
   LOGE("cam_notify_callback: msg_type:%d ext1:%d ext2:%d user:%p",
        msg_type, ext1, ext2, user);
   if (origNotify_cb != NULL) {
      origNotify_cb(msg_type, ext1, ext2, user);
   }
}

static void cam_data_callback(int32_t msgType,
                              const sp<IMemory>& dataPtr,
                              void* user)
{
   LOGE("cam_data_callback: msgType:%d user:%p", msgType, user);
   if (msgType == CAMERA_MSG_PREVIEW_FRAME) {
      int32_t previewWidth, previewHeight;
      CameraParameters hwParameters = qCamera->getParameters();
      hwParameters.getPreviewSize(&previewWidth, &previewHeight);
      CameraHAL_HandlePreviewData(dataPtr, mWindow, origCamReqMemory,
                                  previewWidth, previewHeight);
   } else if (origData_cb != NULL && origCamReqMemory != NULL) {
      camera_memory_t *clientData = CameraHAL_GenClientData(dataPtr,
                                       origCamReqMemory, user);
      if (clientData != NULL) {
         LOGE("cam_data_callback: Posting data to client");
         origData_cb(msgType, clientData, 0, NULL, user);
      }
   }
}

static void cam_data_callback_timestamp(nsecs_t timestamp,
                                        int32_t msgType,
                                        const sp<IMemory>& dataPtr,
                                        void* user)

{
   LOGE("cam_data_callback_timestamp: timestamp:%lld msgType:%d user:%p",
        timestamp /1000, msgType, user);

   if (origDataTS_cb != NULL && origCamReqMemory != NULL) {
      camera_memory_t *clientData = CameraHAL_GenClientData(dataPtr,
                                       origCamReqMemory, user);
      if (clientData != NULL) {
         LOGE("cam_data_callback_timestamp: Posting data to client timestamp:%lld",
              systemTime());
         origDataTS_cb(timestamp, msgType, clientData, 0, user);
         qCamera->releaseRecordingFrame(dataPtr);
      } else {
         LOGE("cam_data_callback_timestamp: ERROR allocating memory from client");
      }
   }
}

extern "C" int get_number_of_cameras(void)
{
   LOGE("get_number_of_cameras:");
   return 1;

//    LOGE("Q%s: E", __func__);
//    return android::HAL_getNumberOfCameras( );
}

extern "C" int get_camera_info(int camera_id, struct camera_info *info)
{
   LOGE("get_camera_info:");
   info->facing      = CAMERA_FACING_BACK;
   info->orientation = 90;
   return NO_ERROR;
}

extern "C" int camera_device_open(const hw_module_t* module, const char* name,
                   hw_device_t** device)
{

   int cameraId = atoi(name);

   LOGE("camera_device_open: name:%s device:%p cameraId:%d",
        name, device, cameraId);

   qCamera = openCameraHardware(cameraId);
   camera_device_t* camera_device = NULL;
   camera_device_ops_t* camera_ops = NULL;

   camera_device = (camera_device_t*)malloc(sizeof(*camera_device));
   camera_ops = (camera_device_ops_t*)malloc(sizeof(*camera_ops));
   memset(camera_device, 0, sizeof(*camera_device));
   memset(camera_ops, 0, sizeof(*camera_ops));

   camera_device->common.tag              = HARDWARE_DEVICE_TAG;
   camera_device->common.version          = 0;
   camera_device->common.module           = (hw_module_t *)(module);
   camera_device->common.close            = close_camera_device;
   camera_device->ops                     = camera_ops;

   camera_ops->set_preview_window         = set_preview_window;
   camera_ops->set_callbacks              = set_callbacks;
   camera_ops->enable_msg_type            = enable_msg_type;
   camera_ops->disable_msg_type           = disable_msg_type;
   camera_ops->msg_type_enabled           = msg_type_enabled;
   camera_ops->start_preview              = start_preview;
   camera_ops->stop_preview               = stop_preview;
   camera_ops->preview_enabled            = preview_enabled;
   camera_ops->store_meta_data_in_buffers = store_meta_data_in_buffers;
   camera_ops->start_recording            = start_recording;
   camera_ops->stop_recording             = stop_recording;
   camera_ops->recording_enabled          = recording_enabled;
   camera_ops->release_recording_frame    = release_recording_frame;
   camera_ops->auto_focus                 = auto_focus;
   camera_ops->cancel_auto_focus          = cancel_auto_focus;
   camera_ops->take_picture               = take_picture;
   camera_ops->cancel_picture             = cancel_picture;

   camera_ops->set_parameters             = set_parameters;
   camera_ops->get_parameters             = get_parameters;
   camera_ops->put_parameters             = put_parameters;
   camera_ops->send_command               = send_command;
   camera_ops->release                    = release;
   camera_ops->dump                       = dump;

   *device = &camera_device->common;
   return NO_ERROR;
}

extern "C" int close_camera_device(hw_device_t* device)
{
   int rc = -EINVAL;
   LOGE("close_camera_device");
   camera_device_t *cameraDev = (camera_device_t *)device;
   if (cameraDev) {
      camera_device_ops_t *camera_ops = cameraDev->ops;
      if (camera_ops) {
         if (qCamera != NULL) {
            qCamera.clear();
         }
         free(camera_ops);
      }
      free(cameraDev);
      rc = NO_ERROR;
   }
   return rc;
}

int set_preview_window(struct camera_device * device,
                           struct preview_stream_ops *window)
{
   LOGE("set_preview_window : Window :%p", window);
   if (device == NULL) {
      LOGE("set_preview_window : Invalid device.");
      return -EINVAL;
   } else {
      LOGE("set_preview_window : window :%p", window);
      mWindow = window;
      return 0;
   }
}

void set_callbacks(struct camera_device * device,
                      camera_notify_callback notify_cb,
                      camera_data_callback data_cb,
                      camera_data_timestamp_callback data_cb_timestamp,
                      camera_request_memory get_memory, void *user)
{
   LOGE("set_callbacks: notify_cb: %p, data_cb: %p "
        "data_cb_timestamp: %p, get_memory: %p, user :%p",
        notify_cb, data_cb, data_cb_timestamp, get_memory, user);

   origNotify_cb    = notify_cb;
   origData_cb      = data_cb;
   origDataTS_cb    = data_cb_timestamp;
   origCamReqMemory = get_memory;
   qCamera->setCallbacks(cam_notify_callback, cam_data_callback,
                         cam_data_callback_timestamp, user);
}

void enable_msg_type(struct camera_device * device, int32_t msg_type)
{
   LOGE("enable_msg_type: msg_type:%d", msg_type);
   qCamera->enableMsgType(msg_type);
}

void disable_msg_type(struct camera_device * device, int32_t msg_type)
{
   LOGE("disable_msg_type: msg_type:%d", msg_type);
   qCamera->disableMsgType(msg_type);
}

int msg_type_enabled(struct camera_device * device, int32_t msg_type)
{
   LOGE("msg_type_enabled: msg_type:%d", msg_type);
   return qCamera->msgTypeEnabled(msg_type);
}

int start_preview(struct camera_device * device)
{
   LOGE("start_preview: Enabling CAMERA_MSG_PREVIEW_FRAME");

   /* TODO: Remove hack. */
   qCamera->enableMsgType(CAMERA_MSG_PREVIEW_FRAME);
   return qCamera->startPreview();
}

void stop_preview(struct camera_device * device)
{
   LOGE("stop_preview:");

   /* TODO: Remove hack. */
   qCamera->disableMsgType(CAMERA_MSG_PREVIEW_FRAME);
   return qCamera->stopPreview();
}

int preview_enabled(struct camera_device * device)
{
   LOGE("preview_enabled:");
   return qCamera->previewEnabled() ? 1 : 0;
}

int store_meta_data_in_buffers(struct camera_device * device, int enable)
{
   LOGE("store_meta_data_in_buffers:");
   return NO_ERROR;
}

int start_recording(struct camera_device * device)
{
   LOGE("start_recording");

   /* TODO: Remove hack. */
   qCamera->enableMsgType(CAMERA_MSG_VIDEO_FRAME);
   qCamera->startRecording();
   return NO_ERROR;
}

void stop_recording(struct camera_device * device)
{
   LOGE("stop_recording:");

   /* TODO: Remove hack. */
   qCamera->disableMsgType(CAMERA_MSG_VIDEO_FRAME);
   qCamera->stopRecording();
}

int recording_enabled(struct camera_device * device)
{
   LOGE("recording_enabled:");
   return (int)qCamera->recordingEnabled();
}

void release_recording_frame(struct camera_device * device,
                                const void *opaque)
{
   /*
    * We release the frame immediately in cam_data_callback_timestamp after making a
    * copy. So, this is just a NOP.
    */
   LOGE("release_recording_frame: opaque:%p", opaque);
}

int auto_focus(struct camera_device * device)
{
   LOGE("auto_focus:");
   qCamera->autoFocus();
   return NO_ERROR;
}

int cancel_auto_focus(struct camera_device * device)
{
   LOGE("cancel_auto_focus:");
   qCamera->cancelAutoFocus();
   return NO_ERROR;
}

int take_picture(struct camera_device * device)
{
   LOGE("take_picture:");

   /* TODO: Remove hack. */
   qCamera->enableMsgType(CAMERA_MSG_SHUTTER |
                         CAMERA_MSG_POSTVIEW_FRAME |
                         CAMERA_MSG_RAW_IMAGE |
                         CAMERA_MSG_COMPRESSED_IMAGE);

   qCamera->takePicture();
   return NO_ERROR;
}

int cancel_picture(struct camera_device * device)
{
   LOGE("cancel_picture:");
   qCamera->cancelPicture();
   return NO_ERROR;
}

//CameraParameters g_param;
String8 g_str;
int set_parameters(struct camera_device * device, const char *params)
{
   LOGE("set_parameters: %s", params);
   g_str = String8(params);
   camSettings.unflatten(g_str);
   qCamera->setParameters(camSettings);
   return NO_ERROR;
}

char * get_parameters(struct camera_device * device)
{
   char *rc = NULL;
   LOGE("get_parameters");
   camSettings = qCamera->getParameters();
   LOGE("get_parameters: after calling qCamera->getParameters()");
   CameraHAL_FixupParams(camSettings);
   g_str = camSettings.flatten();
   rc = strdup((char *)g_str.string());
   LOGE("get_parameters: returning rc:%p :%s",
        rc, (rc != NULL) ? rc : "EMPTY STRING");
   return rc;
}

void put_parameters(struct camera_device *device, char *params)
{
   LOGE("put_parameters: params:%p %s", params, params);
   free(params);
}


int send_command(struct camera_device * device, int32_t cmd,
                        int32_t arg0, int32_t arg1)
{
   LOGE("send_command: cmd:%d arg0:%d arg1:%d",
        cmd, arg0, arg1);
   return NO_ERROR;
}

void release(struct camera_device * device)
{
   LOGE("release:");
   qCamera->release();
}

int dump(struct camera_device * device, int fd)
{
   LOGE("dump:");
   Vector<String16> args;
   return qCamera->dump(fd, args);
}

}; // namespace android
