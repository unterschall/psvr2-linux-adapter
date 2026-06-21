// SPDX-License-Identifier: GPL-2.0
#include "device_provider.h"

#include "driverlog.h"

vr::EVRInitError Psvr2DeviceProvider::Init( vr::IVRDriverContext *pDriverContext )
{
	VR_INIT_SERVER_DRIVER_CONTEXT( pDriverContext );

	hmd_ = std::make_unique<Psvr2HmdDriver>();

	// Register the headset. The serial number must be unique and stable; we use
	// a fixed string since the adapter exposes a single headset (see roadmap M7:
	// multi-headset /dev naming is future work).
	if ( !vr::VRServerDriverHost()->TrackedDeviceAdded(
		     hmd_->GetSerialNumber().c_str(),
		     vr::TrackedDeviceClass_HMD,
		     hmd_.get() ) )
	{
		DriverLog( "psvr2: failed to add HMD device" );
		return vr::VRInitError_Driver_Unknown;
	}

	return vr::VRInitError_None;
}

void Psvr2DeviceProvider::Cleanup()
{
	hmd_ = nullptr;
}

const char *const *Psvr2DeviceProvider::GetInterfaceVersions()
{
	return vr::k_InterfaceVersions;
}

void Psvr2DeviceProvider::RunFrame()
{
	// Drain the server event queue; the HMD has no inputs yet, so we just discard.
	vr::VREvent_t event{};
	while ( vr::VRServerDriverHost()->PollNextEvent( &event, sizeof( event ) ) )
	{
		// Future: react to standby / IPD-change events here.
	}
}

bool Psvr2DeviceProvider::ShouldBlockStandbyMode()
{
	return false;
}

void Psvr2DeviceProvider::EnterStandby() {}
void Psvr2DeviceProvider::LeaveStandby() {}
