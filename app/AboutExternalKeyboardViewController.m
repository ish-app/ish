//
//  CapsLockMappingViewController.m
//  iSH
//
//  Created by Theodore Dubois on 12/2/18.
//

#import "AboutExternalKeyboardViewController.h"
#import "UserPreferences.h"

const int kCapsLockMappingSection = 0;

@interface AboutExternalKeyboardViewController ()

@property (weak, nonatomic) IBOutlet UISwitch *optionMetaSwitch;
@property (weak, nonatomic) IBOutlet UISwitch *backtickEscapeSwitch;

@end

@implementation AboutExternalKeyboardViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [UserPreferences.shared addObserver:self forKeyPath:@"capsLockMapping" options:NSKeyValueObservingOptionNew context:nil];
    [UserPreferences.shared addObserver:self forKeyPath:@"optionMapping" options:NSKeyValueObservingOptionNew context:nil];
    [self _update];
}

- (void)dealloc {
    [UserPreferences.shared removeObserver:self forKeyPath:@"capsLockMapping"];
    [UserPreferences.shared removeObserver:self forKeyPath:@"optionMapping"];
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context {
    [self.tableView reloadData];
}

- (void)_update {
    self.optionMetaSwitch.on = UserPreferences.shared.optionMapping == OptionMapEsc;
    self.backtickEscapeSwitch.on = UserPreferences.shared.backtickMapEscape;
}

- (IBAction)optionMetaToggle:(UISwitch *)sender {
    UserPreferences.shared.optionMapping = sender.on ? OptionMapEsc : OptionMapNone;
}
- (IBAction)backtickEscapeToggle:(UISwitch *)sender {
    UserPreferences.shared.backtickMapEscape = sender.on;
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

@end
