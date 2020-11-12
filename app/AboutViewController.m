//
//  AboutViewController.m
//  iSH
//
//  Created by Theodore Dubois on 9/23/18.
//

#import "UIApplication+OpenURL.h"
#import "AboutViewController.h"
#import "UserPreferences.h"
#import "AppGroup.h"
#import "NSObject+SaneKVO.h"

@interface AboutViewController ()
@property (weak, nonatomic) IBOutlet UITableViewCell *capsLockMappingCell;
@property (weak, nonatomic) IBOutlet UITableViewCell *themeCell;
@property (weak, nonatomic) IBOutlet UISwitch *disableDimmingSwitch;
@property (weak, nonatomic) IBOutlet UITextField *launchCommandField;
@property (weak, nonatomic) IBOutlet UITextField *bootCommandField;

@property (weak, nonatomic) IBOutlet UITableViewCell *sendFeedback;
@property (weak, nonatomic) IBOutlet UITableViewCell *openGithub;
@property (weak, nonatomic) IBOutlet UITableViewCell *openTwitter;
@property (weak, nonatomic) IBOutlet UITableViewCell *openDiscord;

@property (weak, nonatomic) IBOutlet UITableViewCell *exportContainerCell;

@property (weak, nonatomic) IBOutlet UILabel *versionLabel;

@end

@implementation AboutViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [self _updatePreferenceUI];
    if (self.recoveryMode) {
        self.includeDebugPanel = YES;
        self.navigationItem.title = @"Recovery Mode";
        self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Exit"
                                                                                  style:UIBarButtonItemStyleDone
                                                                                 target:self
                                                                                 action:@selector(exitRecovery:)];
        self.navigationItem.leftBarButtonItem = nil;
    }
    _versionLabel.text = [NSString stringWithFormat:@"iSH %@ (Build %@)",
                          [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"],
                          [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleVersion"]];

    [UserPreferences.shared observe:@[@"capsLockMapping", @"fontSize", @"launchCommand", @"bootCommand"]
                            options:0 owner:self usingBlock:^(typeof(self) self) {
        [self _updatePreferenceUI];
    }];
}

- (IBAction)dismiss:(id)sender {
    [self dismissViewControllerAnimated:self completion:nil];
}

- (void)exitRecovery:(id)sender {
    [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"recovery"];
    exit(0);
}

- (void)_updatePreferenceUI {
    UserPreferences *prefs = UserPreferences.shared;
    self.themeCell.detailTextLabel.text = prefs.theme.presetName;
    self.disableDimmingSwitch.on = UserPreferences.shared.shouldDisableDimming;
    self.launchCommandField.text = [UserPreferences.shared.launchCommand componentsJoinedByString:@" "];
    self.bootCommandField.text = [UserPreferences.shared.bootCommand componentsJoinedByString:@" "];
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView cellForRowAtIndexPath:indexPath];
    if (cell == self.sendFeedback) {
        [UIApplication openURL:@"mailto:tblodt@icloud.com?subject=Feedback%20for%20iSH"];
    } else if (cell == self.openGithub) {
        [UIApplication openURL:@"https://github.com/ish-app/ish"];
    } else if (cell == self.openTwitter) {
        [UIApplication openURL:@"https://twitter.com/tblodt"];
    } else if (cell == self.openDiscord) {
        [UIApplication openURL:@"https://discord.gg/HFAXj44"];
    } else if (cell == self.exportContainerCell) {
        // copy the files to the app container so they can be extracted from iTunes file sharing
        NSURL *container = ContainerURL();
        NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask][0];
        [NSFileManager.defaultManager removeItemAtURL:[documents URLByAppendingPathComponent:@"roots copy"] error:nil];
        [NSFileManager.defaultManager copyItemAtURL:[container URLByAppendingPathComponent:@"roots"]
                                              toURL:[documents URLByAppendingPathComponent:@"roots copy"]
                                              error:nil];
    }
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    NSInteger sections = [super numberOfSectionsInTableView:tableView];
    if (!self.includeDebugPanel)
        sections--;
    return sections;
}

- (IBAction)disableDimmingChanged:(id)sender {
    UserPreferences.shared.shouldDisableDimming = self.disableDimmingSwitch.on;
}

- (IBAction)textBoxSubmit:(id)sender {
    [sender resignFirstResponder];
}

- (IBAction)launchCommandChanged:(id)sender {
    UserPreferences.shared.launchCommand = [self.launchCommandField.text componentsSeparatedByString:@" "];
}

- (IBAction)bootCommandChanged:(id)sender {
    UserPreferences.shared.bootCommand = [self.bootCommandField.text componentsSeparatedByString:@" "];
}

@end
