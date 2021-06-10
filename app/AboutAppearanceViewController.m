//
//  ThemeViewController.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import "AboutAppearanceViewController.h"
#import "AboutThemeSelector.h"
#import "FontPickerViewController.h"
#import "UserPreferences.h"
#import "NSObject+SaneKVO.h"

static NSString *const ThemeNameCellIdentifier = @"Theme Name";
static NSString *const FontSizeCellIdentifier = @"Font Size";
static NSString *const PreviewCellIdentifier = @"Preview";

@interface AboutAppearanceViewController ()

@property UIFontPickerViewController *fontPicker API_AVAILABLE(ios(13));
@property (nonatomic) NSMutableArray<NSIndexPath *> *themePaths;
@property AboutThemeSelector *themeSelector;

@end

@implementation AboutAppearanceViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [UserPreferences.shared observe:@[@"theme", @"fontSize", @"fontFamily"]
                            options:0 owner:self usingBlock:^(typeof(self) self) {
        [self.tableView reloadData];
        [self setNeedsStatusBarAppearanceUpdate];
    }];
    _themeSelector = [[AboutThemeSelector alloc] initWithFrame:CGRectMake(0, 0, self.view.frame.size.width, 400)];
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
        case ThemeNameSection: return 1;
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


- (NSString *)reuseIdentifierForIndexPath:(NSIndexPath *)indexPath {
    switch (indexPath.section) {
        case ThemeNameSection: return @"Theme Card";
        case FontSection: return @[@"Font", @"Font Size"][indexPath.row];
        case PreviewSection: return @"Preview";
        default: return nil;
    }
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UserPreferences *prefs = [UserPreferences shared];
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:[self reuseIdentifierForIndexPath:indexPath] forIndexPath:indexPath];
    switch (indexPath.section) {
        case ThemeNameSection: {
            if (![_themeSelector isDescendantOfView:cell.contentView]) {
                [cell.contentView addSubview:_themeSelector];
                cell.textLabel.text = @"";
            }
            cell.selectionStyle = UITableViewCellSelectionStyleNone;
//            cell.backgroundColor = [UIColor clearColor];
//            cell.contentView.backgroundColor = [UIColor clearColor];
            break;
        }
        case FontSection: {
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
        }
        case PreviewSection: {
            cell.backgroundColor = prefs.theme.backgroundColor;
            cell.textLabel.textColor = prefs.theme.foregroundColor;
            cell.textLabel.font = [UIFont fontWithName:UserPreferences.shared.fontFamily size:prefs.fontSize.doubleValue];
            cell.textLabel.text = [NSString stringWithFormat:@"%@:~# ps aux", [UIDevice currentDevice].name];
            cell.selectionStyle = UITableViewCellSelectionStyleNone;
            break;
        }
    }
    
    return cell;
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath {
    switch (indexPath.section) {
        case ThemeNameSection: {
            return _themeSelector.frame.size.height;
        }
        default: {
            return [super tableView:tableView heightForRowAtIndexPath:indexPath];
        }
    }
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    UserPreferences *prefs = [UserPreferences shared];
    NSString *tappedThemeName = Theme.themeNames[indexPath.row]; // Yes I know this isn't neccessary but I keep code DRY
    switch (indexPath.section) {
        case ThemeNameSection:
            if (prefs.theme.name != tappedThemeName) {
                [[UserPreferences shared] setThemeTo:tappedThemeName];
            }
            break;
        case FontSection:
            if (indexPath.row == 0) // font family
                [self selectFont:nil];
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

- (IBAction)fontSizeChanged:(UIStepper *)sender {
    UserPreferences.shared.fontSize = @((int) sender.value);
}

@end
