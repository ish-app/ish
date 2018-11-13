//
//  UserPreferences.h
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface UserPreferences : NSObject

@property (nonatomic) BOOL mapCapsLockAsControl;
@property (nonatomic, copy) NSNumber *fontSize;
@property (nonatomic, copy) NSString *foregroundColor;
@property (nonatomic, copy) NSString *backgroundColor;

+ (instancetype)shared;
- (NSString *)JSONDictionary;

@end

NS_ASSUME_NONNULL_END
