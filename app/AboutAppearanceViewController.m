//
//  ThemeViewController.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import "AboutAppearanceViewController.h"
#import "FontPickerViewController.h"
#import "UserPreferences.h"

static NSString *const ThemeNameCellIdentifier = @"Theme Name";
static NSString *const FontSizeCellIdentifier = @"Font Size";
static NSString *const PreviewCellIdentifier = @"Preview";

@interface AboutAppearanceViewController ()

@end

@implementation AboutAppearanceViewController

- (void)awakeFromNib {
    [super awakeFromNib];
    [[UserPreferences shared] addObserver:self forKeyPath:@"theme" options:NSKeyValueObservingOptionNew context:nil];
    [[UserPreferences shared] addObserver:self forKeyPath:@"fontSize" options:NSKeyValueObservingOptionNew context:nil];
    [[UserPreferences shared] addObserver:self forKeyPath:@"fontFamily" options:NSKeyValueObservingOptionNew context:nil];
}

- (void)dealloc {
    [[UserPreferences shared] removeObserver:self forKeyPath:@"theme"];
    [[UserPreferences shared] removeObserver:self forKeyPath:@"fontSize"];
    [[UserPreferences shared] removeObserver:self forKeyPath:@"fontFamily"];
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
    [self.tableView reloadData];
    [self setNeedsStatusBarAppearanceUpdate];
}

#pragma mark - Table view data source

enum {
    ThemeNameSection,
    FontSection,
    PreviewSection,
    NumberOfSections,
};

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return NumberOfSections;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    switch (section) {
        case ThemeNameSection: return Theme.presetNames.count;
        case FontSection: return 2;
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

- (NSString *)reuseIdentifierForIndexPath:(NSIndexPath *)indexPath {
    switch (indexPath.section) {
        case ThemeNameSection: return @"Theme Name";
        case FontSection: return @[@"Font", @"Font Size"][indexPath.row];
        case PreviewSection: return @"Preview";
        default: return nil;
    }
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UserPreferences *prefs = [UserPreferences shared];
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:[self reuseIdentifierForIndexPath:indexPath] forIndexPath:indexPath];
    
    switch (indexPath.section) {
        case ThemeNameSection:
            cell.textLabel.text = Theme.presetNames[indexPath.row];
            if ([prefs.theme isEqual:[self _themeForRow:indexPath.row]]) {
                cell.accessoryType = UITableViewCellAccessoryCheckmark;
            } else {
                cell.accessoryType = UITableViewCellAccessoryNone;
            }
            break;
            
        case FontSection:
            if (indexPath.row == 0) {
                cell.detailTextLabel.text = UserPreferences.shared.fontFamily;
            } else if (indexPath.row == 1) {
                UserPreferences *prefs = [UserPreferences shared];
                UILabel *label = [cell viewWithTag:1];
                UIStepper *stepper = [cell viewWithTag:2];
                label.text = prefs.fontSize.stringValue;
                stepper.value = prefs.fontSize.doubleValue;
            }
            break;
            
        case PreviewSection:
            cell.backgroundColor = prefs.theme.backgroundColor;
            cell.textLabel.textColor = prefs.theme.foregroundColor;
            cell.textLabel.font = [UIFont fontWithName:UserPreferences.shared.fontFamily size:prefs.fontSize.doubleValue];
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
        case FontSection:
            if (indexPath.row == 0) // font family
                [self selectFont:nil];
    }
}

- (IBAction)selectFont:(id)sender {
    if (@available(iOS 13, *)) {
        UIFontPickerViewControllerConfiguration *config = [UIFontPickerViewControllerConfiguration new];
        config.filteredTraits = UIFontDescriptorTraitMonoSpace;
        UIFontPickerViewController *fontPicker = [[UIFontPickerViewController alloc] initWithConfiguration:config];
        fontPicker.delegate = self;
        [self presentViewController:fontPicker animated:YES completion:nil];
        return;
    }
    
    FontPickerViewController *fontPicker = [self.storyboard instantiateViewControllerWithIdentifier:@"FontPicker"];
    [self.navigationController pushViewController:fontPicker animated:YES];
}

- (void)fontPickerViewControllerDidPickFont:(UIFontPickerViewController *)viewController API_AVAILABLE(ios(13.0)) {
    UserPreferences.shared.fontFamily = viewController.selectedFontDescriptor.fontAttributes[UIFontDescriptorFamilyAttribute];
}

- (IBAction)fontSizeChanged:(UIStepper *)sender {
    UserPreferences.shared.fontSize = @((int) sender.value);
}

@end
