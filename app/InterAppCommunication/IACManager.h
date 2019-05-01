//
//  IACManager.h
//  IACSample
//
//  Created by Antonio Cabezuelo Vivo on 09/02/13.
//  Copyright (c) 2013 Antonio Cabezuelo Vivo. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#import "IACDelegate.h"

@protocol IACDelegate;
@class IACRequest;

// Error domains that this framework will use in error callbacks
extern NSString * const IACErrorDomain;
extern NSString * const IACClientErrorDomain;

// Predefined error codes
typedef NS_ENUM(NSInteger, IACError) {
    IACErrorAppNotInstalled    = 1,
    IACErrorNotSupportedAction = 2
};

// Block template for action handlers
typedef void(^IACActionHandlerBlock)(NSDictionary* inputParameters, IACSuccessBlock success, IACFailureBlock failure);


@interface IACManager : NSObject

// Delegate to be called when an x-callback-url API call is made for this app 
@property (weak, nonatomic) id<IACDelegate> delegate;

// The URL scheme the app is listening on. It must be defined in Info.plist. If your app is not listening or do not expect callbacks you can leave this empty
@property (copy, nonatomic) NSString *callbackURLScheme;


+ (IACManager*)sharedManager;

/* Method to use in app delegate url handler methods.
   Handles the URL parsing and invocation of the different handlers and delegate methods.
   The IACManager should be initialized with the URL scheme that you want to respond to before make any call that expect callbacks.
*/
- (BOOL)handleOpenURL:(NSURL*)url;

/* Method to add action handlers for your x-callback-url APIs
*/
- (void)handleAction:(NSString*)action withBlock:(IACActionHandlerBlock)handler;

/* Method to send request to external apps
*/
- (void)sendIACRequest:(IACRequest*)request;

@end
