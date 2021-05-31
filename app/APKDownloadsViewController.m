//
//  APKDownloadsViewController.m
//  iSH
//
//  Created by Theodore Dubois on 11/24/20.
//

#import "APKDownloadsViewController.h"
#import "APKFilesystem.h"
#import "NSObject+SaneKVO.h"

@interface APKDownloadsViewController ()

@property NSMutableArray<NSBundleResourceRequest *> *downloads;

@property (nonatomic) BOOL onscreen;
@property (nonatomic) NSTimer *appearTimer;

@property (weak, nonatomic) IBOutlet UIView *messageView;
@property (weak, nonatomic) IBOutlet UIProgressView *progress;
@property (weak, nonatomic) IBOutlet UILabel *label;
@property NSLayoutConstraint *topConstraint;
@property NSLayoutConstraint *offscreenConstraint;

@end

@implementation APKDownloadsViewController

- (void)viewDidLoad {
    NSLog(@"load");
    [super viewDidLoad];

    // These are manually created copies of constraints defined in IB, because if any constraints are disabled by default in IB, they get reset to however they were defined at various unpredictable points in time.
    self.topConstraint = [NSLayoutConstraint constraintWithItem:self.messageView
                                                      attribute:NSLayoutAttributeTop
                                                      relatedBy:NSLayoutRelationEqual
                                                         toItem:self.view.safeAreaLayoutGuide
                                                      attribute:NSLayoutAttributeTop
                                                     multiplier:1 constant:20];
    self.offscreenConstraint = [NSLayoutConstraint constraintWithItem:self.view
                                                            attribute:NSLayoutAttributeTop
                                                            relatedBy:NSLayoutRelationEqual
                                                               toItem:self.messageView
                                                            attribute:NSLayoutAttributeBottom
                                                           multiplier:1 constant:20];

    self.downloads = [NSMutableArray new];
    self.onscreen = NO;
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(downloadStarted:) name:APKDownloadStartedNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(downloadFinished:) name:APKDownloadFinishedNotification object:nil];
}

- (void)downloadStarted:(NSNotification *)n {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSLog(@"download start %@", n);
        NSBundleResourceRequest *request = n.object;
        [request.progress observe:@[@"fractionCompleted"] options:0 owner:self usingBlock:^(id self) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self _update];
            });
        }];
        [self.downloads addObject:request];
        [self _update];
    });
}
- (void)downloadFinished:(NSNotification *)n {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSLog(@"download finished %@", n);
        NSBundleResourceRequest *request = n.object;
        [self.downloads removeObject:request];
        [self _update];
    });
}

- (void)_update {
    if (self.downloads.count) {
        if (!self.appearTimer.valid) {
            self.appearTimer = [NSTimer scheduledTimerWithTimeInterval:2 repeats:NO block:^(NSTimer * _Nonnull timer) {
                [self setOnscreen:YES animated:YES];
            }];
        }
    } else {
        [self.appearTimer invalidate];
        [self setOnscreen:NO animated:YES];
        return;
    }
    
    NSBundleResourceRequest *request = self.downloads.firstObject;
    if (!request.progress.cancelled) {
        [self.progress setProgress:request.progress.fractionCompleted animated:YES];
        NSString *path = [request.tags.anyObject stringByReplacingOccurrencesOfString:@":" withString:@"/"];
        NSString *package = path.lastPathComponent;
        self.label.text = [NSString stringWithFormat:@"Downloading %@ (%d%%)", package, (int) (request.progress.fractionCompleted * 100)];
    }
}

- (IBAction)cancel:(id)sender {
    [self.downloads.firstObject.progress cancel];
}

- (void)setOnscreen:(BOOL)onscreen {
    _onscreen = onscreen;
    self.topConstraint.active = self.offscreenConstraint.active = NO;
    self.topConstraint.active = onscreen;
    self.offscreenConstraint.active = !onscreen;
}

- (void)setOnscreen:(BOOL)onscreen animated:(BOOL)animate {
    if (onscreen == _onscreen)
        return;
    [self.view layoutIfNeeded];
    [UIView animateWithDuration:animate ? 0.3 : 0 animations:^{
        self.onscreen = onscreen;
        [self.view layoutIfNeeded];
    }];
}

@end
