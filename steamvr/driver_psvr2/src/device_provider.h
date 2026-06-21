// SPDX-License-Identifier: GPL-2.0
#pragma once

#include <memory>

#include "hmd_device_driver.h"
#include "openvr_driver.h"

// The IServerTrackedDeviceProvider is vrserver's handle on our driver: it owns
// the device lifecycle and is pumped once per server frame.
class Psvr2DeviceProvider : public vr::IServerTrackedDeviceProvider
{
public:
	vr::EVRInitError Init( vr::IVRDriverContext *pDriverContext ) override;
	void Cleanup() override;
	const char *const *GetInterfaceVersions() override;

	void RunFrame() override;

	bool ShouldBlockStandbyMode() override;
	void EnterStandby() override;
	void LeaveStandby() override;

private:
	std::unique_ptr<Psvr2HmdDriver> hmd_;
};
