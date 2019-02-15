//
//  IACRequest.h
//  IACSample
//
//  Created by Antonio Cabezuelo Vivo on 09/02/13.
//  Copyright (c) 2013 Antonio Cabezuelo Vivo. All rights reserved.
//

#import <Foundation/Foundation.h>

@class IACClient;

@interface IACRequest : NSObject

@property (copy, readonly, nonatomic) NSString *requestID;
@property (strong, nonatomic) IACClient *client;
@property (copy, nonatomic) NSString *action;
@property (strong, nonatomic) NSDictionary *parameters;
@property (copy, nonatomic) void(^successCalback)(NSDictionary*);
@property (copy, nonatomic) void(^errorCalback)(NSError*);

@end
