//
//  ProgressReportViewController.h
//  iSH
//
//  Created by Theodore Dubois on 6/18/20.
//

#import <UIKit/UIKit.h>
#import "Roots.h"

NS_ASSUME_NONNULL_BEGIN

@interface ProgressReportViewController : UIViewController <ProgressReporter>

- (void)updateProgress:(double)progressFraction message:(NSString *)progressMessage;

@end

NS_ASSUME_NONNULL_END
