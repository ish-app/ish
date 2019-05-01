//
//  IACClient.h
//  IACSample
//
//  Created by Antonio Cabezuelo Vivo on 09/02/13.
//  Copyright (c) 2013 Antonio Cabezuelo Vivo. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "IACManager.h"

/* This is the class used to make calls to external apps. Use this class as a superclass to create classes for your own apps. Thjis way you can offer a clean API to your app that can meke interact with it easier for other apps.
*/
@interface IACClient : NSObject

// URL scheme that the external app is listenig to. This is mandatory.
@property (copy, nonatomic) NSString *URLScheme;

// The manager to use for calls from this client. If not set, IACManager shared instance will be used.
@property (weak, nonatomic) IACManager *manager;

// Initializers
+ (instancetype)client;
+ (instancetype)clientWithURLScheme:(NSString*)scheme;
- (instancetype)initWithURLScheme:(NSString*)scheme;

/* Utility method to test if the app  that responds to the URLScheme is installed in the device.
*/
- (BOOL)isAppInstalled;


/* Method that transforms from x-callback-url errorCode parameter to a NSInteger to be used in NSError's code.
   The default implementation return [code integerValue].
   If you create a subclass for your app and your app return string error codes you must implement this method to transform from your error codes to integer values.
*/
- (NSInteger)NSErrorCodeForXCUErrorCode:(NSString*)code;

/* Convenient methods to make call to external apps. If you create a subclass for your app, call these methods to launch the external app.
*/
- (void)performAction:(NSString*)action;

- (void)performAction:(NSString*)action
           parameters:(NSDictionary*)params;

- (void)performAction:(NSString*)action
           parameters:(NSDictionary*)params
            onSuccess:(void(^)(NSDictionary*result))success
            onFailure:(void(^)(NSError*))failure;

@end
