//
//  ThemeViewController.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import "AppearanceViewController.h"
#import "UserPreferences.h"

static NSString *const ThemeNameCellIdentifier = @"Theme Name";
static NSString *const FontSizeCellIdentifier = @"Font Size";
static NSString *const PreviewCellIdentifier = @"Preview";

@interface AppearanceViewController ()

@end

@implementation AppearanceViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [[UserPreferences shared] addObserver:self forKeyPath:@"theme" options:NSKeyValueObservingOptionNew context:nil];
    [[UserPreferences shared] addObserver:self forKeyPath:@"fontSize" options:NSKeyValueObservingOptionNew context:nil];
}

- (void)dealloc {
    @try {
        [[UserPreferences shared] removeObserver:self forKeyPath:@"theme"];
        [[UserPreferences shared] removeObserver:self forKeyPath:@"fontSize"];
    } @catch (NSException * __unused exception) {}
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
    [self.tableView reloadData];
    [self setNeedsStatusBarAppearanceUpdate];
}

#pragma mark - Table view data source

enum {
    ThemeNameSection,
    FontSizeSection,
    PreviewSection,
    NumberOfSections,
};

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return NumberOfSections;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    switch (section) {
        case ThemeNameSection: return Theme.presetNames.count;
        case FontSizeSection: return 1;
        case PreviewSection: return 1;
        default: NSAssert(NO, @"unhandled section"); return 0;
    }
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    switch (section) {
        case ThemeNameSection: return @"Theme";
        case PreviewSection: return @"Preview";
        default: return nil;
    }
}

- (Theme *)_themeForRow:(NSUInteger)row {
    return [Theme presetThemeNamed:Theme.presetNames[row]];
}

- (NSString *)reuseIdentifierForSection:(NSInteger)section {
    switch (section) {
        case ThemeNameSection: return @"Theme Name";
        case FontSizeSection: return @"Font Size";
        case PreviewSection: return @"Preview";
        default: return nil;
    }
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UserPreferences *prefs = [UserPreferences shared];
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:[self reuseIdentifierForSection:indexPath.section] forIndexPath:indexPath];
    
    switch (indexPath.section) {
        case ThemeNameSection:
            cell.textLabel.text = Theme.presetNames[indexPath.row];
            if ([prefs.theme isEqual:[self _themeForRow:indexPath.row]]) {
                cell.accessoryType = UITableViewCellAccessoryCheckmark;
            } else {
                cell.accessoryType = UITableViewCellAccessoryNone;
            }
            break;
            
        case FontSizeSection: {
            UserPreferences *prefs = [UserPreferences shared];
            UILabel *label = [cell viewWithTag:1];
            UIStepper *stepper = [cell viewWithTag:2];
            label.text = prefs.fontSize.stringValue;
            stepper.value = prefs.fontSize.doubleValue;
            break;
        }
            
        case PreviewSection:
            cell.backgroundColor = prefs.theme.backgroundColor;
            cell.textLabel.textColor = prefs.theme.foregroundColor;
            cell.textLabel.font = [UIFont fontWithName:@"Menlo-Regular" size:prefs.fontSize.doubleValue];
            cell.textLabel.text = [NSString stringWithFormat:@"%@:~# ps aux", [UIDevice currentDevice].name];
            cell.selectionStyle = UITableViewCellSelectionStyleNone;
            break;
    }
    
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    
    switch (indexPath.section) {
        case ThemeNameSection:
            UserPreferences.shared.theme = [self _themeForRow:indexPath.row];
            break;
    }
}

- (IBAction)fontSizeChanged:(UIStepper *)sender {
    UserPreferences.shared.fontSize = @((int) sender.value);
}

@end
