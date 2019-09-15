//
//  SceneDelegate.h
//  iSH
//
//  Created by Noah Peeters on 13.09.19.
//

#import <UIKit/UIKit.h>
#import "TerminalViewControllerDelegate.h"
#include "FeatureAvailability.h"

#if ENABLE_MULTIWINDOW
API_AVAILABLE(ios(13.0))
@interface SceneDelegate : UIResponder <UIWindowSceneDelegate, TerminalViewControllerDelegate>

@property (strong, nonatomic) UIWindow *window;

@end
#endif
