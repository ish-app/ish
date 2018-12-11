//
//  AboutViewController.m
//  iSH
//
//  Created by Theodore Dubois on 9/23/18.
//

#import "UIApplication+OpenURL.h"
#import "AboutViewController.h"
#import "UserPreferences.h"

@interface AboutViewController ()
@property (weak, nonatomic) IBOutlet UITableViewCell *capsLockMappingCell;
@property (weak, nonatomic) IBOutlet UILabel *fontSizeLabel;
@property (weak, nonatomic) IBOutlet UIStepper *fontSizeStepper;
@property (weak, nonatomic) IBOutlet UITableViewCell *themeCell;
@property (weak, nonatomic) IBOutlet UISwitch *disableDimmingSwitch;

@property (weak, nonatomic) IBOutlet UITableViewCell *sendFeedback;
@property (weak, nonatomic) IBOutlet UITableViewCell *openGithub;
@property (weak, nonatomic) IBOutlet UITableViewCell *openTwitter;
@end

@implementation AboutViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [self _addObservers];
    [self _updatePreferenceUI];
}

- (void)dealloc {
    [self _removeObservers];
}

- (void)_addObservers {
    UserPreferences *prefs = [UserPreferences shared];
    NSKeyValueObservingOptions opts = NSKeyValueObservingOptionNew;
    
    [prefs addObserver:self forKeyPath:@"capsLockMapping" options:opts context:nil];
    [prefs addObserver:self forKeyPath:@"fontSize" options:opts context:nil];
    [prefs addObserver:self forKeyPath:@"theme" options:opts context:nil];
}

- (void)_removeObservers {
    @try {
        UserPreferences *prefs = [UserPreferences shared];
        [prefs removeObserver:self forKeyPath:@"capsLockMapping"];
        [prefs removeObserver:self forKeyPath:@"fontSize"];
        [prefs removeObserver:self forKeyPath:@"theme"];
    } @catch (NSException * __unused exception) {}
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
    if ([object isKindOfClass:[UserPreferences class]]) {
        [self _updatePreferenceUI];
    } else {
        [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
    }
}

- (void)_updatePreferenceUI {
    UserPreferences *prefs = [UserPreferences shared];
    self.fontSizeLabel.text = prefs.fontSize.stringValue;
    self.fontSizeStepper.value = prefs.fontSize.doubleValue;
    self.themeCell.detailTextLabel.text = prefs.theme.presetName;
    NSString *capsLockMappingDescr;
    switch (prefs.capsLockMapping) {
        case CapsLockMapNone:
            capsLockMappingDescr = @"None"; break;
        case CapsLockMapControl:
            capsLockMappingDescr = @"Control"; break;
        case CapsLockMapEscape:
            capsLockMappingDescr = @"Escape"; break;
    }
    self.capsLockMappingCell.detailTextLabel.text = capsLockMappingDescr;
    self.disableDimmingSwitch.on = UserPreferences.shared.shouldDisableDimming;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView cellForRowAtIndexPath:indexPath];
    if (cell == self.sendFeedback) {
        [UIApplication openURL:@"mailto:tblodt@icloud.com"];
    } else if (cell == self.openGithub) {
        [UIApplication openURL:@"https://github.com/tbodt/ish"];
    } else if (cell == self.openTwitter) {
        [UIApplication openURL:@"https://twitter.com/tblodt"];
    }
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
}

- (IBAction)fontSizeChanged:(id)sender {
    UserPreferences.shared.fontSize = @((int) self.fontSizeStepper.value);
}

- (IBAction)disableDimmingChanged:(id)sender {
    UserPreferences.shared.shouldDisableDimming = self.disableDimmingSwitch.on;
}

@end
