/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef __QCAMERAFOVCONTROL_H__
#define __QCAMERAFOVCONTROL_H__

#include <utils/Mutex.h>
#include "cam_intf.h"

using namespace android;

namespace qcamera {

typedef enum {
    AE_SETTLED,
    AE_CONVERGING
} ae_status;

typedef enum {
    AF_VALID,
    AF_INVALID
} af_status;

typedef enum {
    CAM_POSITION_LEFT,
    CAM_POSITION_RIGHT
} cam_relative_position;

typedef enum {
    STATE_WIDE,
    STATE_TRANSITION_WIDE_TO_TELE,
    STATE_TELE,
    STATE_TRANSITION_TELE_TO_WIDE
} dual_cam_state;


typedef struct {
    ae_status status;
    uint16_t  lux;
} ae_info;

typedef struct {
    af_status status;
    uint16_t  focusDistCm;
} af_info;

typedef struct {
    ae_info ae;
    af_info af;
} status_3A_t;

typedef struct {
    status_3A_t main;
    status_3A_t aux;
} dual_cam_3A_status_t;

typedef struct {
    uint8_t         status;
    uint32_t        shiftHorz;
    uint32_t        shiftVert;
    uint32_t        activeCamState;
    uint8_t         camMasterPreview;
    uint8_t         camMaster3A;
} spatial_align_result_t;

typedef struct {
    float    cropRatio;
    float    cutOverFactor;
    float    cutOverMainToAux;
    float    cutOverAuxToMain;
    float    transitionHigh;
    float    transitionLow;
    uint32_t waitTimeForHandoffMs;
} dual_cam_transition_params_t;

typedef struct {
    uint32_t                     zoomMain;
    uint32_t                     zoomAux;
    uint32_t                    *zoomRatioTable;
    uint32_t                     zoomRatioTableCount;
    cam_sync_type_t              camWide;
    cam_sync_type_t              camTele;
    dual_cam_state               camState;
    dual_cam_3A_status_t         status3A;
    cam_dimension_t              previewSize;
    spatial_align_result_t       spatialAlignResult;
    uint32_t                     availableSpatialAlignSolns;
    uint32_t                     shiftHorzAdjMain;
    float                        camMainWidthMargin;
    float                        camMainHeightMargin;
    float                        camAuxWidthMargin;
    float                        camAuxHeightMargin;
    bool                         camcorderMode;
    float                        basicFovRatio;
    dual_cam_transition_params_t transitionParams;
} fov_control_data_t;

typedef struct {
    float    percentMarginHysterisis;
    float    percentMarginAux;
    float    percentMarginMain;
    uint32_t waitTimeForHandoffMs;
} fov_control_config_t;

typedef struct{
    uint32_t sensorStreamWidth;
    uint32_t sensorStreamHeight;
    float    focalLengthMm;
    float    pixelPitchUm;
} intrinsic_cam_params_t;

typedef struct {
    uint32_t               minFocusDistanceCm;
    float                  baselineMm;
    float                  rollDegrees;
    float                  pitchDegrees;
    float                  yawDegrees;
    cam_relative_position  positionAux;
    intrinsic_cam_params_t paramsMain;
    intrinsic_cam_params_t paramsAux;
} dual_cam_params_t;

typedef struct {
    cam_sync_type_t camMasterPreview;
    cam_sync_type_t camMaster3A;
    uint32_t        activeCamState;
    bool            snapshotPostProcess;
} fov_control_result_t;


class QCameraFOVControl {
public:
    ~QCameraFOVControl();
    static QCameraFOVControl* create(cam_capability_t *capsMainCam, cam_capability_t* capsAuxCam);
    int32_t updateConfigSettings(parm_buffer_t* paramsMainCam, parm_buffer_t* paramsAuxCam);
    cam_capability_t consolidateCapabilities(cam_capability_t* capsMainCam,
            cam_capability_t* capsAuxCam);
    int32_t translateInputParams(parm_buffer_t* paramsMainCam, parm_buffer_t *paramsAuxCam);
    metadata_buffer_t* processResultMetadata(metadata_buffer_t* metaMainCam,
            metadata_buffer_t* metaAuxCam);
    fov_control_result_t getFovControlResult();

private:
    QCameraFOVControl();
    bool validateAndExtractParameters(cam_capability_t  *capsMainCam,
            cam_capability_t  *capsAuxCam);
    bool calculateBasicFovRatio();
    bool combineFovAdjustment();
    void  calculateDualCamTransitionParams();
    void convertUserZoomToMainAndAux(uint32_t zoom);
    uint32_t readjustZoomForAux(uint32_t zoomMain);
    uint32_t readjustZoomForMain(uint32_t zoomAux);
    uint32_t findZoomRatio(uint32_t zoom);
    inline uint32_t findZoomValue(uint32_t zoomRatio);
    cam_face_detection_data_t translateRoiFD(cam_face_detection_data_t faceDetectionInfo);
    cam_roi_info_t translateFocusAreas(cam_roi_info_t roiAfMain);
    cam_set_aec_roi_t translateMeteringAreas(cam_set_aec_roi_t roiAecMain);
    void convertDisparityForInputParams();

    Mutex                           mMutex;
    fov_control_config_t            mFovControlConfig;
    fov_control_data_t              mFovControlData;
    fov_control_result_t            mFovControlResult;
    dual_cam_params_t               mDualCamParams;
};

}; // namespace qcamera

#endif /* __QCAMERAFOVCONTROL_H__ */
