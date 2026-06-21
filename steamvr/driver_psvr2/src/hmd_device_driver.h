// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "openvr_driver.h"
#include "pose_source.h"

// Geometry of the PSVR2 panel as the compositor needs to see it. Defaults are
// the native panel (4000x2040, two 2000x2040 eyes); they can be overridden from
// resources/settings/default.vrsettings.
struct Psvr2DisplayConfig
{
	int32_t window_x = 0;          // top-left of the panel in the X desktop
	int32_t window_y = 0;
	int32_t window_width = 4000;   // full stereo panel
	int32_t window_height = 2040;
	int32_t render_width = 2000;   // per-eye render target
	int32_t render_height = 2040;
	float   fov_tan = 1.30f;       // tan(half-FOV); ~104deg total — refine from references/PSVR2

	// Direct mode (default): report the panel as a real, NON-desktop display so
	// SteamVR's compositor acquires it directly via DRM leasing (no TTY, desktop
	// keeps running). When false, the panel is treated as an extended desktop
	// monitor instead (the bring-up scaffold — see docs/steamvr.md).
	bool    direct_mode = true;
};

// IVRDisplayComponent: tells vrcompositor where/how to render. Extended-display
// mode for now (the panel is a normal desktop output we light up via KMS);
// direct mode is future work (roadmap M7).
class Psvr2DisplayComponent : public vr::IVRDisplayComponent
{
public:
	explicit Psvr2DisplayComponent( const Psvr2DisplayConfig &config );

	bool IsDisplayOnDesktop() override;
	bool IsDisplayRealDisplay() override;
	void GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight ) override;
	void GetEyeOutputViewport( vr::EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) override;
	void GetProjectionRaw( vr::EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom ) override;
	vr::DistortionCoordinates_t ComputeDistortion( vr::EVREye eEye, float fU, float fV ) override;
	void GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight ) override;
	bool ComputeInverseDistortion( vr::HmdVector2_t *pResult, vr::EVREye eEye, uint32_t unChannel, float fU, float fV ) override;

private:
	Psvr2DisplayConfig config_;
};

// The HMD itself.
class Psvr2HmdDriver : public vr::ITrackedDeviceServerDriver
{
public:
	Psvr2HmdDriver();

	vr::EVRInitError Activate( uint32_t unObjectId ) override;
	void Deactivate() override;
	void EnterStandby() override;
	void *GetComponent( const char *pchComponentNameAndVersion ) override;
	void DebugRequest( const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize ) override;
	vr::DriverPose_t GetPose() override;

	const std::string &GetSerialNumber() const { return serial_number_; }

private:
	void PoseThread();

	std::unique_ptr<Psvr2DisplayComponent> display_;
	std::unique_ptr<PoseSource> pose_source_;

	std::string model_number_;
	std::string serial_number_;
	float display_frequency_ = 90.0f;
	bool direct_mode_ = true;

	std::atomic<bool> active_{ false };
	std::atomic<uint32_t> device_index_{ vr::k_unTrackedDeviceIndexInvalid };
	vr::DriverPose_t last_pose_{};
	std::thread pose_thread_;
};
