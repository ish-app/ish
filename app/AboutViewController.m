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
@property (weak, nonatomic) IBOutlet UITableViewCell *sendFeedback;
@property (weak, nonatomic) IBOutlet UITableViewCell *openGithub;
@property (weak, nonatomic) IBOutlet UITableViewCell *openTwitter;
@property (weak, nonatomic) IBOutlet UITableViewCell *fontSizeCell;
@property (weak, nonatomic) IBOutlet UITableViewCell *foregroundColorCell;
@property (weak, nonatomic) IBOutlet UITableViewCell *backgroundColorCell;
@end

@implementation AboutViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [self _addObservers];
    [self _updatePreferenceUI];
}

- (void)_addObservers {
    UserPreferences *prefs = [UserPreferences shared];
    NSKeyValueObservingOptions opts = NSKeyValueObservingOptionNew;
    
    [prefs addObserver:self forKeyPath:@"mapCapsLockAsControl" options:opts context:nil];
    [prefs addObserver:self forKeyPath:@"fontSize" options:opts context:nil];
    [prefs addObserver:self forKeyPath:@"foregroundColor" options:opts context:nil];
    [prefs addObserver:self forKeyPath:@"backgroundColor" options:opts context:nil];
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
    _fontSizeCell.detailTextLabel.text = prefs.fontSize.stringValue;
    _foregroundColorCell.detailTextLabel.text = prefs.foregroundColor;
    _backgroundColorCell.detailTextLabel.text = prefs.backgroundColor;
}

- (IBAction)didSwitchCapsLock:(id)sender {
    
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView cellForRowAtIndexPath:indexPath];
    if (cell == self.sendFeedback) {
        [UIApplication openURL:@"mailto:tblodt@icloud.com"];
    } else if (cell == self.openGithub) {
        [UIApplication openURL:@"https://github.com/tbodt/ish"];
    } else if (cell == self.openTwitter) {
        [UIApplication openURL:@"https://twitter.com/tblodt"];
    } else if (cell == self.fontSizeCell ||
               cell == self.foregroundColorCell ||
               cell == self.backgroundColorCell) {
        [self _showInputForCell:cell];
    }
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
}

- (void)_showInputForCell:(UITableViewCell *)cell {
    UIAlertController *alert = [UIAlertController
                                alertControllerWithTitle:cell.textLabel.text
                                message:@"(Enter a CSS compatible value)"
                                preferredStyle:UIAlertControllerStyleAlert];
    [alert addTextFieldWithConfigurationHandler:^(UITextField * _Nonnull textField) {
        textField.placeholder = cell.detailTextLabel.text;
    }];
    [alert addAction:[UIAlertAction actionWithTitle:@"Save" style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
        NSString *value = [[[alert textFields] firstObject] text];
        if ([value isEqualToString:@""]) {
            return;
        }

        UserPreferences *prefs = [UserPreferences shared];
        
        if (cell == self.fontSizeCell) {
            NSUInteger fontSize = [value integerValue];
            if (fontSize) {
                [prefs setFontSize:@(fontSize)];
            }
        } else if (cell == self.foregroundColorCell) {
            [prefs setForegroundColor:value];
        } else if (cell == self.backgroundColorCell) {
            [prefs setBackgroundColor:value];
        }
    }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

@end
