//
//  FeatureAvailability.h
//  iSH
//
//  Created by Noah Peeters on 15.09.19.
//

#import "Availability.h"

#ifndef FeatureAvailability_h
#define FeatureAvailability_h

#ifdef __IPHONE_13_0
    #define IOS13_SDK_AVAILABLE 1
#else
    #define IOS13_SDK_AVAILABLE 0
#endif

#define ENABLE_MULTIWINDOW IOS13_SDK_AVAILABLE

#endif /* FeatureAvailability_h */
