// SPDX-License-Identifier: GPL-2.0
#include "hmd_device_driver.h"

#include <chrono>
#include <cstring>

#include "driverlog.h"

static const char *kSettingsSection = "driver_psvr2";

// PSVR2 panel EDID identity, decoded from the Sony SIE EDID
// (references/psvr2-sie-kernel-modules .../mtk_wrapper_edid.h): manufacturer
// bytes 0x4D 0xD9 = "SNY", product id 0xA205, monitor name "SIE  VRH". SteamVR
// uses these to match and DRM-lease the physical connector in direct mode.
static constexpr int32_t kEdidVendorId = 0x4DD9;   // "SNY"
static constexpr int32_t kEdidProductId = 0xA205;

Psvr2HmdDriver::Psvr2HmdDriver()
{
	// Defaults can be overridden in resources/settings/default.vrsettings.
	// IVRSettings::GetString has no default-value arg, so read then fall back.
	char buf[256] = { 0 };
	vr::EVRSettingsError err = vr::VRSettingsError_None;
	vr::VRSettings()->GetString( kSettingsSection, "model_number", buf, sizeof( buf ), &err );
	model_number_ = ( err == vr::VRSettingsError_None && buf[0] ) ? buf : "PSVR2";

	buf[0] = 0;
	err = vr::VRSettingsError_None;
	vr::VRSettings()->GetString( kSettingsSection, "serial_number", buf, sizeof( buf ), &err );
	serial_number_ = ( err == vr::VRSettingsError_None && buf[0] ) ? buf : "PSVR2-0001";

	const float hz = vr::VRSettings()->GetFloat( kSettingsSection, "display_frequency" );
	if ( hz > 0.0f )
		display_frequency_ = hz;

	Psvr2DisplayConfig cfg{};
	// Each field falls back to the struct default when the setting is absent (0).
	if ( int32_t v = vr::VRSettings()->GetInt32( kSettingsSection, "window_x" ) )      cfg.window_x = v;
	if ( int32_t v = vr::VRSettings()->GetInt32( kSettingsSection, "window_y" ) )      cfg.window_y = v;
	if ( int32_t v = vr::VRSettings()->GetInt32( kSettingsSection, "window_width" ) )  cfg.window_width = v;
	if ( int32_t v = vr::VRSettings()->GetInt32( kSettingsSection, "window_height" ) ) cfg.window_height = v;
	cfg.render_width = cfg.window_width / 2;
	cfg.render_height = cfg.window_height;

	// Direct mode is the default; an explicit "direct_mode": false in settings
	// switches to the extended-desktop scaffold.
	vr::EVRSettingsError dm_err = vr::VRSettingsError_None;
	const bool direct_mode = vr::VRSettings()->GetBool( kSettingsSection, "direct_mode", &dm_err );
	cfg.direct_mode = ( dm_err == vr::VRSettingsError_None ) ? direct_mode : true;
	direct_mode_ = cfg.direct_mode;

	display_ = std::make_unique<Psvr2DisplayComponent>( cfg );
	pose_source_ = std::make_unique<PoseSource>();

	if ( !pose_source_->HasPose() )
		DriverLog( "psvr2: /dev/psvr2-pose not found — HMD will not track (is the module loaded?)" );
}

vr::EVRInitError Psvr2HmdDriver::Activate( uint32_t unObjectId )
{
	device_index_ = unObjectId;

	vr::PropertyContainerHandle_t c = vr::VRProperties()->TrackedDeviceToPropertyContainer( unObjectId );
	vr::VRProperties()->SetStringProperty( c, vr::Prop_ModelNumber_String, model_number_.c_str() );
	vr::VRProperties()->SetStringProperty( c, vr::Prop_ManufacturerName_String, "Sony" );

	const float ipd = vr::VRSettings()->GetFloat( vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_IPD_Float );
	vr::VRProperties()->SetFloatProperty( c, vr::Prop_UserIpdMeters_Float, ipd );

	// Required for the compositor to start.
	vr::VRProperties()->SetFloatProperty( c, vr::Prop_DisplayFrequency_Float, display_frequency_ );
	vr::VRProperties()->SetFloatProperty( c, vr::Prop_UserHeadToEyeDepthMeters_Float, 0.0f );
	vr::VRProperties()->SetFloatProperty( c, vr::Prop_SecondsFromVsyncToPhotons_Float, 0.011f );

	// EDID identity lets SteamVR's compositor find the physical connector and
	// (in direct mode) DRM-lease it. In extended mode it's harmless.
	vr::VRProperties()->SetInt32Property( c, vr::Prop_EdidVendorID_Int32, kEdidVendorId );
	vr::VRProperties()->SetInt32Property( c, vr::Prop_EdidProductID_Int32, kEdidProductId );

	// IsOnDesktop must agree with the display component: false => SteamVR
	// acquires the panel directly (direct mode); true => extended desktop.
	vr::VRProperties()->SetBoolProperty( c, vr::Prop_IsOnDesktop_Bool, !direct_mode_ );

	DriverLog( "psvr2: display mode = %s (EDID %04X:%04X)",
	           direct_mode_ ? "direct (DRM lease)" : "extended desktop",
	           kEdidVendorId, kEdidProductId );

	active_ = true;
	pose_thread_ = std::thread( &Psvr2HmdDriver::PoseThread, this );
	return vr::VRInitError_None;
}

void Psvr2HmdDriver::Deactivate()
{
	if ( active_.exchange( false ) && pose_thread_.joinable() )
		pose_thread_.join();
	device_index_ = vr::k_unTrackedDeviceIndexInvalid;
}

void Psvr2HmdDriver::EnterStandby() {}

void *Psvr2HmdDriver::GetComponent( const char *pchComponentNameAndVersion )
{
	if ( std::strcmp( pchComponentNameAndVersion, vr::IVRDisplayComponent_Version ) == 0 )
		return display_.get();
	return nullptr;
}

void Psvr2HmdDriver::DebugRequest( const char *, char *pchResponseBuffer, uint32_t unResponseBufferSize )
{
	if ( unResponseBufferSize >= 1 )
		pchResponseBuffer[0] = 0;
}

vr::DriverPose_t Psvr2HmdDriver::GetPose()
{
	return last_pose_;
}

void Psvr2HmdDriver::PoseThread()
{
	while ( active_ )
	{
		vr::DriverPose_t pose{};
		if ( pose_source_->ReadPose( pose ) )
		{
			last_pose_ = pose;
			if ( device_index_ != vr::k_unTrackedDeviceIndexInvalid )
				vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
					device_index_, pose, sizeof( pose ) );
		}
		else
		{
			// No node / timeout: don't spin hot.
			std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
		}
	}
}

// ---------------------------------------------------------------------------
// Display component
// ---------------------------------------------------------------------------

Psvr2DisplayComponent::Psvr2DisplayComponent( const Psvr2DisplayConfig &config )
	: config_( config )
{
}

bool Psvr2DisplayComponent::IsDisplayOnDesktop()
{
	// Direct mode (default): NOT on the desktop, so SteamVR's compositor
	// acquires the panel directly via DRM leasing. Extended mode: on the desktop
	// as an ordinary monitor. See docs/steamvr.md.
	return !config_.direct_mode;
}

bool Psvr2DisplayComponent::IsDisplayRealDisplay()
{
	return true;
}

void Psvr2DisplayComponent::GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight )
{
	*pnWidth = config_.render_width;
	*pnHeight = config_.render_height;
}

void Psvr2DisplayComponent::GetEyeOutputViewport( vr::EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight )
{
	*pnY = 0;
	*pnWidth = config_.window_width / 2;
	*pnHeight = config_.window_height;
	*pnX = ( eEye == vr::Eye_Left ) ? 0 : config_.window_width / 2;
}

void Psvr2DisplayComponent::GetProjectionRaw( vr::EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom )
{
	// Symmetric FOV placeholder. TODO: per-eye asymmetric values from the
	// headset FOV params in references/PSVR2.
	*pfLeft = -config_.fov_tan;
	*pfRight = config_.fov_tan;
	*pfTop = -config_.fov_tan;
	*pfBottom = config_.fov_tan;
}

vr::DistortionCoordinates_t Psvr2DisplayComponent::ComputeDistortion( vr::EVREye eEye, float fU, float fV )
{
	// Identity (no distortion mesh yet). The PSVR2 lenses need a real mesh; until
	// then SteamVR renders undistorted. TODO: import a distortion model.
	vr::DistortionCoordinates_t c{};
	c.rfRed[0] = c.rfGreen[0] = c.rfBlue[0] = fU;
	c.rfRed[1] = c.rfGreen[1] = c.rfBlue[1] = fV;
	return c;
}

void Psvr2DisplayComponent::GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight )
{
	*pnX = config_.window_x;
	*pnY = config_.window_y;
	*pnWidth = config_.window_width;
	*pnHeight = config_.window_height;
}

bool Psvr2DisplayComponent::ComputeInverseDistortion( vr::HmdVector2_t *, vr::EVREye, uint32_t, float, float )
{
	// Let SteamVR infer the inverse from ComputeDistortion.
	return false;
}
