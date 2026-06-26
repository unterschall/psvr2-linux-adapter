// SPDX-License-Identifier: GPL-2.0
// Entry point dlopen'd by SteamVR's vrserver. Mirrors the OpenVR SDK's
// `simplehmd` sample driver — see docs/steamvr.md and docs/references.md.
#include <cstring>

#include "device_provider.h"
#include "openvr_driver.h"

#if defined( _WIN32 )
#define HMD_DLL_EXPORT extern "C" __declspec( dllexport )
#elif defined( __GNUC__ ) || defined( __APPLE__ )
#define HMD_DLL_EXPORT extern "C" __attribute__( ( visibility( "default" ) ) )
#else
#error "Unsupported platform."
#endif

static Psvr2DeviceProvider g_device_provider;

// vrserver calls this to retrieve our IServerTrackedDeviceProvider.
HMD_DLL_EXPORT void *HmdDriverFactory( const char *pInterfaceName, int *pReturnCode )
{
	if ( std::strcmp( vr::IServerTrackedDeviceProvider_Version, pInterfaceName ) == 0 )
		return &g_device_provider;

	if ( pReturnCode )
		*pReturnCode = vr::VRInitError_Init_InterfaceNotFound;

	return nullptr;
}
