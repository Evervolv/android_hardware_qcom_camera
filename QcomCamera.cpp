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
#include <ui/Rect.h>
#include <ui/GraphicBufferMapper.h>
#include <dlfcn.h>

#include "CameraHardwareInterface.h"
/* include QCamera Hardware Interface Header*/
#include "QcomCamera.h"
//#include "QualcommCameraHardware.h"

extern "C" {
#include <sys/time.h>
}

#ifdef PREVIEW_MSM7K
#define GRALLOC_USAGE_PMEM_PRIVATE_ADSP GRALLOC_USAGE_PRIVATE_0
#endif

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
android::sp<android::CameraHardwareInterface> (*LINK_openCameraHardware)(int id);
int (*LINK_getNumberofCameras)(void);
void (*LINK_getCameraInfo)(int cameraId, struct camera_info *info);

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
bool
CameraHAL_CopyBuffers_Hw(int srcFd, int destFd,
                         size_t srcOffset, size_t destOffset,
                         int srcFormat, int destFormat,
                         int x, int y, int w, int h)
{
    struct blitreq blit;
    bool   success = true;
    int    fb_fd = open("/dev/graphics/fb0", O_RDWR);

    if (fb_fd < 0) {
       ALOGE("CameraHAL_CopyBuffers_Hw: Error opening /dev/graphics/fb0");
       return false;
    }

    ALOGV("CameraHAL_CopyBuffers_Hw: srcFD:%d destFD:%d srcOffset:%#x"
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
       ALOGE("CameraHAL_CopyBuffers_Hw: MSMFB_BLIT failed = %d %s",
            errno, strerror(errno));
       success = false;
    }
    close(fb_fd);
    return success;
}

void
CameraHal_Decode_Sw(unsigned int* rgb, char* yuv420sp, int width, int height)
{
   int frameSize = width * height;

   if (!qCamera->previewEnabled()) return;

   for (int j = 0, yp = 0; j < height; j++) {
      int uvp = frameSize + (j >> 1) * width, u = 0, v = 0;
      for (int i = 0; i < width; i++, yp++) {
         int y = (0xff & ((int) yuv420sp[yp])) - 16;
         if (y < 0) y = 0;
         if ((i & 1) == 0) {
            v = (0xff & yuv420sp[uvp++]) - 128;
            u = (0xff & yuv420sp[uvp++]) - 128;
         }

         int y1192 = 1192 * y;
         int r = (y1192 + 1634 * v);
         int g = (y1192 - 833 * v - 400 * u);
         int b = (y1192 + 2066 * u);

         if (r < 0) r = 0; else if (r > 262143) r = 262143;
         if (g < 0) g = 0; else if (g > 262143) g = 262143;
         if (b < 0) b = 0; else if (b > 262143) b = 262143;

         rgb[yp] = 0xff000000 | ((b << 6) & 0xff0000) |
                   ((g >> 2) & 0xff00) | ((r >> 10) & 0xff);
      }
   }
}

void
CameraHAL_CopyBuffers_Sw(char *dest, char *src, int size)
{
   int       i;
   int       numWords  = size / sizeof(unsigned);
   unsigned *srcWords  = (unsigned *)src;
   unsigned *destWords = (unsigned *)dest;

   for (i = 0; i < numWords; i++) {
      if ((i % 8) == 0 && (i + 8) < numWords) {
         __builtin_prefetch(srcWords  + 8, 0, 0);
         __builtin_prefetch(destWords + 8, 1, 0);
      }
      *destWords++ = *srcWords++;
   }
   if (__builtin_expect((size - (numWords * sizeof(unsigned))) > 0, 0)) {
      int numBytes = size - (numWords * sizeof(unsigned));
      char *destBytes = (char *)destWords;
      char *srcBytes  = (char *)srcWords;
      for (i = 0; i < numBytes; i++) {
         *destBytes++ = *srcBytes++;
      }
   }
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

      ALOGV("CameraHAL_HandlePreviewData: previewWidth:%d previewHeight:%d "
           "offset:%#x size:%#x base:%p", previewWidth, previewHeight,
           (unsigned)offset, size, mHeap != NULL ? mHeap->base() : 0);

      mWindow->set_usage(mWindow,
#ifdef PREVIEW_MSM7K
                         GRALLOC_USAGE_PMEM_PRIVATE_ADSP |
#endif
                         GRALLOC_USAGE_SW_READ_OFTEN);

      retVal = mWindow->set_buffers_geometry(mWindow,
                                             previewWidth, previewHeight,
                                             HAL_PIXEL_FORMAT_RGBX_8888);
      if (retVal == NO_ERROR) {
         int32_t          stride;
         buffer_handle_t *bufHandle = NULL;

         ALOGV("CameraHAL_HandlePreviewData: dequeueing buffer");
         retVal = mWindow->dequeue_buffer(mWindow, &bufHandle, &stride);
         if (retVal == NO_ERROR) {
            retVal = mWindow->lock_buffer(mWindow, bufHandle);
            if (retVal == NO_ERROR) {
               private_handle_t const *privHandle =
                  reinterpret_cast<private_handle_t const *>(*bufHandle);
               if (!CameraHAL_CopyBuffers_Hw(mHeap->getHeapID(), privHandle->fd,
                                             offset, privHandle->offset,
                                             previewFormat, destFormat,
                                             0, 0, previewWidth,
                                             previewHeight)) {
                  void *bits;
                  Rect bounds;
                  GraphicBufferMapper &mapper = GraphicBufferMapper::get();

                  bounds.left   = 0;
                  bounds.top    = 0;
                  bounds.right  = previewWidth;
                  bounds.bottom = previewHeight;

                  mapper.lock(*bufHandle, GRALLOC_USAGE_SW_READ_OFTEN, bounds,
                              &bits);
                  ALOGV("CameraHAL_HPD: w:%d h:%d bits:%p",
                       previewWidth, previewHeight, bits);
                  CameraHal_Decode_Sw((unsigned int *)bits, (char *)mHeap->base() + offset,
                                      previewWidth, previewHeight);

                  // unlock buffer before sending to display
                  mapper.unlock(*bufHandle);
               }

               mWindow->enqueue_buffer(mWindow, bufHandle);
               ALOGV("CameraHAL_HandlePreviewData: enqueued buffer");
            } else {
               ALOGE("CameraHAL_HandlePreviewData: ERROR locking the buffer");
               mWindow->cancel_buffer(mWindow, bufHandle);
            }
         } else {
            ALOGE("CameraHAL_HandlePreviewData: ERROR dequeueing the buffer");
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

   ALOGV("CameraHAL_GenClientData: offset:%#x size:%#x base:%p",
        (unsigned)offset, size, mHeap != NULL ? mHeap->base() : 0);

   clientData = reqClientMemory(-1, size, 1, user);
   if (clientData != NULL) {
      CameraHAL_CopyBuffers_Sw((char *)clientData->data,
                               (char *)(mHeap->base()) + offset, size);
   } else {
      ALOGE("CameraHAL_GenClientData: ERROR allocating memory from client");
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
   const char *frame_rate_range     = "(15,30)";

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

   if (!settings.get(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE)) {
      settings.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE,
                   frame_rate_range);
   }
}

static void camera_release_memory(struct camera_memory *mem) { }

void cam_notify_callback(int32_t msg_type, int32_t ext1,
                   int32_t ext2, void *user)
{
   ALOGV("cam_notify_callback: msg_type:%d ext1:%d ext2:%d user:%p",
        msg_type, ext1, ext2, user);
   if (origNotify_cb != NULL) {
      origNotify_cb(msg_type, ext1, ext2, user);
   }
}

static void cam_data_callback(int32_t msgType,
                              const sp<IMemory>& dataPtr,
                              void* user)
{
   ALOGV("cam_data_callback: msgType:%d user:%p", msgType, user);
   if (msgType == CAMERA_MSG_PREVIEW_FRAME) {
      int32_t previewWidth, previewHeight;
      CameraParameters hwParameters = qCamera->getParameters();
      hwParameters.getPreviewSize(&previewWidth, &previewHeight);
      CameraHAL_HandlePreviewData(dataPtr, mWindow, origCamReqMemory,
                                  previewWidth, previewHeight);
   }
   if (origData_cb != NULL && origCamReqMemory != NULL) {
      camera_memory_t *clientData = CameraHAL_GenClientData(dataPtr,
                                       origCamReqMemory, user);
      if (clientData != NULL) {
         ALOGV("cam_data_callback: Posting data to client");
         origData_cb(msgType, clientData, 0, NULL, user);
         clientData->release(clientData);
      }
   }
}

static void cam_data_callback_timestamp(nsecs_t timestamp,
                                        int32_t msgType,
                                        const sp<IMemory>& dataPtr,
                                        void* user)

{
   ALOGV("cam_data_callback_timestamp: timestamp:%lld msgType:%d user:%p",
        timestamp /1000, msgType, user);

   if (origDataTS_cb != NULL && origCamReqMemory != NULL) {
      camera_memory_t *clientData = CameraHAL_GenClientData(dataPtr,
                                       origCamReqMemory, user);
      if (clientData != NULL) {
         ALOGV("cam_data_callback_timestamp: Posting data to client timestamp:%lld",
              systemTime());
         origDataTS_cb(timestamp, msgType, clientData, 0, user);
         qCamera->releaseRecordingFrame(dataPtr);
         clientData->release(clientData);
      } else {
         ALOGE("cam_data_callback_timestamp: ERROR allocating memory from client");
      }
   }
}

extern "C" int get_number_of_cameras(void)
{
   int numCameras = 1;

   ALOGV("get_number_of_cameras:");
   void *libcameraHandle = ::dlopen("libcamera.so", RTLD_NOW);
   ALOGD("HAL_get_number_of_cameras: loading libcamera at %p", libcameraHandle);
   if (!libcameraHandle) {
       ALOGE("FATAL ERROR: could not dlopen libcamera.so: %s", dlerror());
   } else {
      if (::dlsym(libcameraHandle, "HAL_getNumberOfCameras") != NULL) {
         *(void**)&LINK_getNumberofCameras =
                  ::dlsym(libcameraHandle, "HAL_getNumberOfCameras");
         numCameras = LINK_getNumberofCameras();
         ALOGD("HAL_get_number_of_cameras: numCameras:%d", numCameras);
      }
      dlclose(libcameraHandle);
   }
   return numCameras;
}

extern "C" int get_camera_info(int camera_id, struct camera_info *info)
{
   bool dynamic = false;
   ALOGV("get_camera_info:");
   void *libcameraHandle = ::dlopen("libcamera.so", RTLD_NOW);
   ALOGD("HAL_get_camera_info: loading libcamera at %p", libcameraHandle);
   if (!libcameraHandle) {
       ALOGE("FATAL ERROR: could not dlopen libcamera.so: %s", dlerror());
       return EINVAL;
   } else {
      if (::dlsym(libcameraHandle, "HAL_getCameraInfo") != NULL) {
         *(void**)&LINK_getCameraInfo =
                  ::dlsym(libcameraHandle, "HAL_getCameraInfo");
         LINK_getCameraInfo(camera_id, info);
         dynamic = true;
      }
      dlclose(libcameraHandle);
   }
   if (!dynamic) {
      info->facing      = CAMERA_FACING_BACK;
      info->orientation = 90;
   }
   return NO_ERROR;
}

extern "C" int camera_device_open(const hw_module_t* module, const char* name,
                   hw_device_t** device)
{

   void *libcameraHandle;
   int cameraId = atoi(name);

   ALOGV("camera_device_open: name:%s device:%p cameraId:%d",
        name, device, cameraId);

   libcameraHandle = ::dlopen("libcamera.so", RTLD_NOW);
   ALOGD("loading libcamera at %p", libcameraHandle);
   if (!libcameraHandle) {
       ALOGE("FATAL ERROR: could not dlopen libcamera.so: %s", dlerror());
       return false;
   }

   if (::dlsym(libcameraHandle, "openCameraHardware") != NULL) {
      *(void**)&LINK_openCameraHardware =
               ::dlsym(libcameraHandle, "openCameraHardware");
   } else if (::dlsym(libcameraHandle, "HAL_openCameraHardware") != NULL) {
      *(void**)&LINK_openCameraHardware =
               ::dlsym(libcameraHandle, "HAL_openCameraHardware");
   } else {
      ALOGE("FATAL ERROR: Could not find openCameraHardware");
      dlclose(libcameraHandle);
      return false;
   }

   qCamera = LINK_openCameraHardware(cameraId);
   ::dlclose(libcameraHandle);

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
   ALOGV("close_camera_device");
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
   ALOGV("set_preview_window : Window :%p", window);
   if (device == NULL) {
      ALOGE("set_preview_window : Invalid device.");
      return -EINVAL;
   } else {
      ALOGV("set_preview_window : window :%p", window);
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
   ALOGV("set_callbacks: notify_cb: %p, data_cb: %p "
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
   ALOGV("enable_msg_type: msg_type:%#x", msg_type);
   if (msg_type == 0xfff) {
      msg_type = 0x1ff;
   } else {
      msg_type &= ~(CAMERA_MSG_PREVIEW_METADATA | CAMERA_MSG_RAW_IMAGE_NOTIFY);
   }
   qCamera->enableMsgType(msg_type);
}

void disable_msg_type(struct camera_device * device, int32_t msg_type)
{
   ALOGV("disable_msg_type: msg_type:%#x", msg_type);
   if (msg_type == 0xfff) {
      msg_type = 0x1ff;
   }
   qCamera->disableMsgType(msg_type);
}

int msg_type_enabled(struct camera_device * device, int32_t msg_type)
{
   ALOGV("msg_type_enabled: msg_type:%d", msg_type);
   return qCamera->msgTypeEnabled(msg_type);
}

int start_preview(struct camera_device * device)
{
   ALOGV("start_preview: Enabling CAMERA_MSG_PREVIEW_FRAME");

   /* TODO: Remove hack. */
   ALOGV("qcamera_start_preview: Preview enabled:%d msg enabled:%d",
        qCamera->previewEnabled(),
        qCamera->msgTypeEnabled(CAMERA_MSG_PREVIEW_FRAME));
   if (!qCamera->msgTypeEnabled(CAMERA_MSG_PREVIEW_FRAME)) {
      qCamera->enableMsgType(CAMERA_MSG_PREVIEW_FRAME);
   }
   return qCamera->startPreview();
}

void stop_preview(struct camera_device * device)
{
   ALOGV("stop_preview: msgenabled:%d",
         qCamera->msgTypeEnabled(CAMERA_MSG_PREVIEW_FRAME));

   /* TODO: Remove hack. */
   if (qCamera->msgTypeEnabled(CAMERA_MSG_PREVIEW_FRAME)) {
      qCamera->disableMsgType(CAMERA_MSG_PREVIEW_FRAME);
   }
   return qCamera->stopPreview();
}

int preview_enabled(struct camera_device * device)
{
   ALOGV("preview_enabled:");
   return qCamera->previewEnabled() ? 1 : 0;
}

int store_meta_data_in_buffers(struct camera_device * device, int enable)
{
   ALOGV("store_meta_data_in_buffers:");
   return NO_ERROR;
}

int start_recording(struct camera_device * device)
{
   ALOGV("start_recording");

   /* TODO: Remove hack. */
   qCamera->enableMsgType(CAMERA_MSG_VIDEO_FRAME);
   qCamera->startRecording();
   return NO_ERROR;
}

void stop_recording(struct camera_device * device)
{
   ALOGV("stop_recording:");

   /* TODO: Remove hack. */
   qCamera->disableMsgType(CAMERA_MSG_VIDEO_FRAME);
   qCamera->stopRecording();
}

int recording_enabled(struct camera_device * device)
{
   ALOGV("recording_enabled:");
   return (int)qCamera->recordingEnabled();
}

void release_recording_frame(struct camera_device * device,
                                const void *opaque)
{
   /*
    * We release the frame immediately in cam_data_callback_timestamp after making a
    * copy. So, this is just a NOP.
    */
   ALOGV("release_recording_frame: opaque:%p", opaque);
}

int auto_focus(struct camera_device * device)
{
   ALOGV("auto_focus:");
   qCamera->autoFocus();
   return NO_ERROR;
}

int cancel_auto_focus(struct camera_device * device)
{
   ALOGV("cancel_auto_focus:");
   qCamera->cancelAutoFocus();
   return NO_ERROR;
}

int take_picture(struct camera_device * device)
{
   ALOGV("take_picture:");

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
   ALOGV("cancel_picture:");
   qCamera->cancelPicture();
   return NO_ERROR;
}

//CameraParameters g_param;
String8 g_str;
int set_parameters(struct camera_device * device, const char *params)
{
   ALOGV("set_parameters: %s", params);
   g_str = String8(params);
   camSettings.unflatten(g_str);
   qCamera->setParameters(camSettings);
   return NO_ERROR;
}

char * get_parameters(struct camera_device * device)
{
   char *rc = NULL;
   ALOGV("get_parameters");
   camSettings = qCamera->getParameters();
   ALOGV("get_parameters: after calling qCamera->getParameters()");
   CameraHAL_FixupParams(camSettings);
   g_str = camSettings.flatten();
   rc = strdup((char *)g_str.string());
   ALOGV("get_parameters: returning rc:%p :%s",
        rc, (rc != NULL) ? rc : "EMPTY STRING");
   return rc;
}

void put_parameters(struct camera_device *device, char *params)
{
   ALOGV("put_parameters: params:%p %s", params, params);
   free(params);
}


int send_command(struct camera_device * device, int32_t cmd,
                        int32_t arg0, int32_t arg1)
{
   ALOGV("send_command: cmd:%d arg0:%d arg1:%d",
        cmd, arg0, arg1);
   return qCamera->sendCommand(cmd, arg0, arg1);
}

void release(struct camera_device * device)
{
   ALOGV("release:");
   qCamera->release();
}

int dump(struct camera_device * device, int fd)
{
   ALOGV("dump:");
   Vector<String16> args;
   return qCamera->dump(fd, args);
}

}; // namespace android
