//
//  IACDelegate.h
//  IACSample
//
//  Created by Antonio Cabezuelo Vivo on 11/02/13.
//  Copyright (c) 2013 Antonio Cabezuelo Vivo. All rights reserved.
//

#import <Foundation/Foundation.h>


// Block templates
typedef void(^IACSuccessBlock)(NSDictionary* returnParams,BOOL cancelled);
typedef void(^IACFailureBlock)(NSError* error);


@protocol IACDelegate <NSObject>

/* Method invoqued to see if an action is handled by the delegate
*/
- (BOOL)supportsIACAction:(NSString*)action;

/* Method invoqued by the manager to perform an action.
   The parameters dictionary does not contain any x-callback-url parameter except 'x-source'.
   success and failure are the blocks you must call after you perform the action to support callbacks to the calling app. If the action does not support callbacks you can ignore this blocks.
*/
- (void)performIACAction:(NSString*)action
              parameters:(NSDictionary*)parameters
               onSuccess:(IACSuccessBlock)success
               onFailure:(IACFailureBlock)failure;

@end
