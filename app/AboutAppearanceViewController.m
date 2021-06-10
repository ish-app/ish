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

@interface AboutAppearanceViewController () {
    CGFloat selectorHeight;
}

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
    self.tableView.separatorStyle = UITableViewCellSeparatorStyleNone;
    selectorHeight = (cardVerticalPadding + cardSize) * Theme.themeNames.count;
    _themeSelector = [[AboutThemeSelector alloc] initWithFrame:CGRectMake(0, 0, self.view.frame.size.width, selectorHeight)];
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
    NumberOfSections,
};

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return NumberOfSections;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    switch (section) {
        case ThemeNameSection: return 3;
        default: NSAssert(NO, @"unhandled section"); return 0;
    }
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    switch (section) {
        case ThemeNameSection: return @"Customization";
        default: return nil;
    }
}


- (NSString *)reuseIdentifierForIndexPath:(NSIndexPath *)indexPath {
    return @[@"Font", @"Font Size", @"Theme Card"][indexPath.row];
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:[self reuseIdentifierForIndexPath:indexPath] forIndexPath:indexPath];
    switch (indexPath.section) {
        case ThemeNameSection: {
            if (indexPath.row == 0) {
                cell.detailTextLabel.text = UserPreferences.shared.fontFamily;
            } else if (indexPath.row == 1) {
                UserPreferences *prefs = [UserPreferences shared];
                UILabel *label = [cell viewWithTag:1];
                UIStepper *stepper = [cell viewWithTag:2];
                label.text = prefs.fontSize.stringValue;
                stepper.value = prefs.fontSize.doubleValue;
            } else {
                if (![_themeSelector isDescendantOfView:cell.contentView]) {
                    [cell.contentView addSubview:_themeSelector];
                    cell.textLabel.text = @"";
                }
                cell.selectionStyle = UITableViewCellSelectionStyleNone;
            }
            break;
        }
    }
    
    return cell;
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath {
    // Only one section right now
    if (indexPath.row == 2)
        return _themeSelector.frame.size.height + cardVerticalPadding;
    
    return [super tableView:tableView heightForRowAtIndexPath:indexPath];
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    
    if (indexPath.row == 0) // font family
        [self selectFont:nil];
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
