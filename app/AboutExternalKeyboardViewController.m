//
//  CapsLockMappingViewController.m
//  iSH
//
//  Created by Theodore Dubois on 12/2/18.
//

#import "AboutExternalKeyboardViewController.h"
#import "UserPreferences.h"
#import "NSObject+SaneKVO.h"

const int kCapsLockMappingSection = 0;

@interface AboutExternalKeyboardViewController ()

@property (weak, nonatomic) IBOutlet UISwitch *optionMetaSwitch;
@property (weak, nonatomic) IBOutlet UISwitch *backtickEscapeSwitch;
@property (weak, nonatomic) IBOutlet UISwitch *overrideControlSpaceSwitch;
@property (weak, nonatomic) IBOutlet UISwitch *hideExtraKeysWithExternalKeyboardSwitch;

@end

@implementation AboutExternalKeyboardViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [UserPreferences.shared observe:@[@"capsLockMapping", @"optionMapping"]
                            options:0 owner:self usingBlock:^(typeof(self) self) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.tableView reloadData];
        });
    }];
    [self _update];
}

- (void)_update {
    self.optionMetaSwitch.on = UserPreferences.shared.optionMapping == OptionMapEsc;
    self.backtickEscapeSwitch.on = UserPreferences.shared.backtickMapEscape;
    self.overrideControlSpaceSwitch.on = UserPreferences.shared.overrideControlSpace;
    self.hideExtraKeysWithExternalKeyboardSwitch.on = UserPreferences.shared.hideExtraKeysWithExternalKeyboard;
}

- (IBAction)optionMetaToggle:(UISwitch *)sender {
    UserPreferences.shared.optionMapping = sender.on ? OptionMapEsc : OptionMapNone;
}
- (IBAction)backtickEscapeToggle:(UISwitch *)sender {
    UserPreferences.shared.backtickMapEscape = sender.on;
}
- (IBAction)overrideControlSpaceToggle:(UISwitch *)sender {
    UserPreferences.shared.overrideControlSpace = sender.on;
}
- (IBAction)hideExtraKeysToggle:(UISwitch *)sender {
    UserPreferences.shared.hideExtraKeysWithExternalKeyboard = sender.on;
}

- (void)tableView:(UITableView *)tableView willDisplayCell:(UITableViewCell *)cell forRowAtIndexPath:(NSIndexPath *)indexPath {
    if (indexPath.section == kCapsLockMappingSection && cell.tag == UserPreferences.shared.capsLockMapping)
        cell.accessoryType = UITableViewCellAccessoryCheckmark;
    else
        cell.accessoryType = UITableViewCellAccessoryNone;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    if (indexPath.section == kCapsLockMappingSection) {
        UITableViewCell *cell = [tableView cellForRowAtIndexPath:indexPath];
        UserPreferences.shared.capsLockMapping = cell.tag;
    }
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    if (section == 0 && ![self.class capsLockMappingSupported])
        return 0;
    return [super tableView:tableView numberOfRowsInSection:section];
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    if (section == 0 && ![self.class capsLockMappingSupported])
        return @"Caps Lock mapping is broken in iOS 13.\n\n"
        @"Since iOS 13.4, Caps Lock can be remapped system-wide in Settings → General → Keyboard → Hardware Keyboard → Modifier Keys.";
    return [super tableView:tableView titleForFooterInSection:section];
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    if (section == 0 && ![self.class capsLockMappingSupported])
        return @"";
    return [super tableView:tableView titleForHeaderInSection:section];
}

+ (BOOL)capsLockMappingSupported {
    if (@available(iOS 13, *)) {
        return NO;
    }
    return YES;
}

@end
