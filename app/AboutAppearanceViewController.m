//
//  ThemeViewController.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import "AboutAppearanceViewController.h"
#import "FontPickerViewController.h"
#import "UserPreferences.h"
#import "NSObject+SaneKVO.h"
#import "UIColor+additions.h"
#import "EditThemeViewController.h"

static NSString *const ThemeNameCellIdentifier = @"Theme Name";
static NSString *const FontSizeCellIdentifier = @"Font Size";
static NSString *const PreviewCellIdentifier = @"Preview";

@interface AboutAppearanceViewController ()
@property UIFontPickerViewController *fontPicker API_AVAILABLE(ios(13));
@end
@implementation AboutAppearanceViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [UserPreferences.shared observe:@[@"theme", @"fontSize", @"fontFamily"]
                            options:0 owner:self usingBlock:^(typeof(self) self) {
        [self.tableView reloadData];
        [self setNeedsStatusBarAppearanceUpdate];
    }];
}

- (void)viewDidAppear:(BOOL)animated {
    if (@available(iOS 13, *)) {
        // Initialize the font picker ASAP, as it takes about a quarter second to initialize (XPC crap) and appears invisible until then.
        // Re-initialize it after navigating away from it, to reset the table view highlight.
        UIFontPickerViewControllerConfiguration *config = [UIFontPickerViewControllerConfiguration new];
        config.filteredTraits = UIFontDescriptorTraitMonoSpace;
        self.fontPicker = [[UIFontPickerViewController alloc] initWithConfiguration:config];
        // Prevent the font picker from resizing the popup when it appears
        self.fontPicker.preferredContentSize = CGSizeZero;
        self.fontPicker.navigationItem.title = @"Font";
        self.fontPicker.delegate = self;
    }
}

#pragma mark - Table view data source

enum {
    CustomizationSection,
};

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return Theme.themeNames.count + 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    switch (section) {
        case CustomizationSection: return 2;
        default: return 1;
    }
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    switch (section) {
        case CustomizationSection: return @"Customization";
        default: return Theme.themeNames[section - 1];
    }
}


- (NSString *)reuseIdentifierForIndexPath:(NSIndexPath *)indexPath {
    switch (indexPath.section) {
        case CustomizationSection: {
            return @[@"Font", @"Font Size"][indexPath.row];
        }
        default: {
            return @"Theme Card";
        }
    }
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:[self reuseIdentifierForIndexPath:indexPath] forIndexPath:indexPath];
    UserPreferences *prefs = [UserPreferences shared];
    switch (indexPath.section) {
        case CustomizationSection: {
            if (indexPath.row == 0) {
                cell.detailTextLabel.text = UserPreferences.shared.fontFamily;
            } else if (indexPath.row == 1) {
                UILabel *label = [cell viewWithTag:1];
                UIStepper *stepper = [cell viewWithTag:2];
                label.text = prefs.fontSize.stringValue;
                stepper.value = prefs.fontSize.doubleValue;
            }
            break;
        }
        default: {
            Theme *currentTheme = [prefs themeFromName:Theme.themeNames[indexPath.section - 1]];
            switch (indexPath.row) {
                case 0: {
                    if (prefs.theme.name == currentTheme.name) {
                        cell.accessoryType = UITableViewCellAccessoryCheckmark;
                    } else {
                        cell.accessoryType = UITableViewCellAccessoryNone;
                    }
                    cell.backgroundColor = currentTheme.backgroundColor;
                    cell.textLabel.textColor = currentTheme.foregroundColor;
                    cell.textLabel.font = [UIFont fontWithName:UserPreferences.shared.fontFamily size:prefs.fontSize.doubleValue];
                    cell.textLabel.text = [NSString stringWithFormat:@"%@:~# ps aux", [UIDevice currentDevice].name];
                    cell.selectionStyle = UITableViewCellSelectionStyleNone;
                }
            }
            break;
        }
            
    }
    
    return cell;
}

- (NSArray<UITableViewRowAction *> *)tableView:(UITableView *)tableView editActionsForRowAtIndexPath:(NSIndexPath *)indexPath {
    switch (indexPath.section) {
        case CustomizationSection:
            return nil;
        default: {
            NSString *themeName = Theme.themeNames[indexPath.section - 1];
            Theme *currentTheme = [UserPreferences.shared themeFromName:themeName];
            UITableViewRowAction *editAction = [UITableViewRowAction rowActionWithStyle:UITableViewRowActionStyleNormal title:@"Edit" handler:^(UITableViewRowAction *action, NSIndexPath *indexPath) {
                [self editTheme:currentTheme.name];
            }];
            UITableViewRowAction *deleteAction = [UITableViewRowAction rowActionWithStyle:UITableViewRowActionStyleNormal title:@"Delete"  handler:^(UITableViewRowAction *action, NSIndexPath *indexPath) {
                [tableView beginUpdates];
                [tableView deleteSections:[NSIndexSet indexSetWithIndex:indexPath.section] withRowAnimation:UITableViewRowAnimationAutomatic];
                [UserPreferences.shared deleteTheme:themeName];
                [tableView endUpdates];

            }];
            deleteAction.backgroundColor = [self adjustColor:currentTheme.foregroundColor];
            editAction.backgroundColor = [self adjustColor:currentTheme.backgroundColor];
            if ([[Theme.presets allKeys] containsObject:themeName]) {
                return @[editAction];
            } else {
                if (UserPreferences.shared.theme.name == themeName) {
                    return @[editAction];
                } else {
                    return @[deleteAction, editAction];
                }
            }
            return nil;
        }
    }
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    
    if (indexPath.section == CustomizationSection) {
        if (indexPath.row == 0) [self selectFont:nil];
    } else if (indexPath.section > CustomizationSection) {
        NSString *currentName = Theme.themeNames[indexPath.section - 1];
        [UserPreferences.shared setThemeToName:currentName];
    }
}

- (void)selectFont:(id)sender {
    if (@available(iOS 13, *)) {
        [self.navigationController pushViewController:self.fontPicker animated:YES];
        return;
    }
    
    FontPickerViewController *fontPicker = [self.storyboard instantiateViewControllerWithIdentifier:@"FontPicker"];
    [self.navigationController pushViewController:fontPicker animated:YES];
}

- (void)fontPickerViewControllerDidPickFont:(UIFontPickerViewController *)viewController API_AVAILABLE(ios(13.0)) {
    UserPreferences.shared.fontFamily = viewController.selectedFontDescriptor.fontAttributes[UIFontDescriptorFamilyAttribute];
    [self.navigationController popToViewController:self animated:YES];
}

- (void) editTheme:(NSString *)themeName {
    EditThemeViewController *themeEditor = [self.storyboard instantiateViewControllerWithIdentifier:@"ThemeEditor"];
    themeEditor.navigationItem.title = [NSString stringWithFormat:@"Edit %@", themeName];
    [themeEditor setThemeName:themeName];
    themeEditor.delegate = self;
    [self.navigationController pushViewController:themeEditor animated:YES];
}

- (void) themeChanged {
    [[self tableView] reloadData];
}

- (UIColor *) adjustColor:(UIColor *)color {
    CGFloat hue, saturation, oldBrightness, alpha;
    [color getHue:&hue saturation:&saturation brightness:&oldBrightness alpha:&alpha];
    CGFloat newBrightness = color.isLight ? oldBrightness * 0.8 : oldBrightness * 2;
    return [UIColor colorWithHue:hue saturation:saturation brightness:newBrightness alpha:alpha];
}

- (IBAction)fontSizeChanged:(UIStepper *)sender {
    UserPreferences.shared.fontSize = @((int) sender.value);
}

@end
