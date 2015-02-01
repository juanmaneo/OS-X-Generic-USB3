//
//  Compatibility.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on September 10th 2014.
//  Copyright (c) 2014 Zenith432. All rights reserved.
//
//

#include "GenericUSBXHCI.h"

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Build-time Versioning Test
#pragma mark -

#ifdef REHABMAN_UNIVERSAL_BUILD
    #if __MAC_OS_X_VERSION_MAX_ALLOWED < 1080
        #error REHABMAN_UNIVERSAL_BUILD requires at least 10.8 SDK.
    #endif
#endif

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 101000 && !defined(REHABMAN_UNIVERSAL_BUILD)
	#if __MAC_OS_X_VERSION_MIN_REQUIRED < 101000
		#error OS 10.10 SDK may only be used to target OS version 10.10.
	#endif
#elif __MAC_OS_X_VERSION_MIN_REQUIRED < 1075
	#error Target OS version must be 10.7.5 or above.
#endif
