// SPDX-License-Identifier: GPL-2.0
#pragma once

#include "openvr_driver.h"

// Bridges libpsvr2's 6DoF stream to an OpenVR DriverPose_t. Owns the libpsvr2
// handle and converts the device-native frame into the OpenVR frame.
class PoseSource
{
public:
	PoseSource();
	~PoseSource();

	// True once the /dev/psvr2-pose node was found and opened.
	bool HasPose() const;

	// Block (up to a short timeout) for the next sample and fill `out` in the
	// OpenVR frame. Returns true if `out` was updated, false on timeout/EOF.
	bool ReadPose( vr::DriverPose_t &out );

private:
	struct psvr2 *dev_;
};
