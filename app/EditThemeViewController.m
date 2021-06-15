//
//  EditThemeViewController.m
//  iSH
//
//  Created by Corban Amouzou on 2021-06-11.
//

#import "EditThemeViewController.h"

static NSString *kEditThemeStatusBarToggleId = @"ToggleCell";
static NSString *kEditPreviewId = @"Preview";
static NSString *kEditColorPickerCellId = @"ColorPicker";
@implementation EditThemeViewController

enum {
    PreviewSection,
    StatusBarSection,
    NumberOfSections,
};

- (void)viewDidLoad {
    [super viewDidLoad];
    self.tableView.sectionHeaderHeight = UITableViewAutomaticDimension;
    self.currentTheme = [UserPreferences.shared themeFromName:self.themeName];
    self.tableView.separatorStyle = UITableViewCellSeparatorStyleNone;
    oldBackgroundColor = self.navigationController.navigationBar.barTintColor;
    oldForegroundColor = self.navigationController.navigationBar.tintColor;
    self.navigationController.navigationBar.tintColor = _currentTheme.foregroundColor;
    self.navigationController.navigationBar.barTintColor = _currentTheme.backgroundColor;
    self.navigationController.navigationBar.titleTextAttributes = @{NSForegroundColorAttributeName:_currentTheme.foregroundColor};
    self.navigationController.navigationBar.translucent = NO;
    if (@available(iOS 13, *)) {
        if (UserPreferences.shared.theme.keyboardAppearance == UIKeyboardAppearanceLight) {
            self.tableView.backgroundColor = [UIColor colorWithRed: 0.95 green: 0.95 blue: 0.97 alpha: 1.00];
        }
    }
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    
    if (self.isMovingFromParentViewController || self.isBeingDismissed) {
        self.navigationController.navigationBar.barTintColor = oldBackgroundColor;
        self.navigationController.navigationBar.tintColor = oldForegroundColor;
        self.navigationController.navigationBar.translucent = YES;
        self.navigationController.navigationBar.titleTextAttributes = @{};
    }
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return PreviewSection + 1;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return 3;
}

- (UIView *)tableView:(UITableView *)tableView viewForHeaderInSection:(NSInteger)section {
    UIView *headerView = [[UIView alloc] initWithFrame:CGRectMake(0, 0, INFINITY, INFINITY)];
    headerView.backgroundColor = _currentTheme.backgroundColor;
    return headerView;
}

- (CGFloat)tableView:(UITableView *)tableView estimatedHeightForHeaderInSection:(NSInteger)section {
    if (section == 0) return 45;
    return [super tableView:tableView estimatedHeightForHeaderInSection:section];
}

- (NSString *)reuseIdentifierForIndexPath:(NSIndexPath *)indexPath {
    switch (indexPath.section) {
        case PreviewSection: {
            return @[kEditPreviewId, kEditColorPickerCellId, kEditColorPickerCellId][indexPath.row];
        }
        case StatusBarSection: {
            return kEditThemeStatusBarToggleId;
        }
        default: return nil;
    }
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:[self reuseIdentifierForIndexPath:indexPath]];
    switch (indexPath.section) {
        case PreviewSection: {
            NSInteger row = indexPath.row;
            if (row == 0) {
                cell.textLabel.text = [NSString stringWithFormat:@"%@:~# %@", [UIDevice currentDevice].name, _currentTheme.name];
                cell.backgroundColor = _currentTheme.backgroundColor;
                cell.textLabel.textColor = _currentTheme.foregroundColor;
                cell.textLabel.font = [UIFont fontWithName:UserPreferences.shared.fontFamily size:UserPreferences.shared.fontSize.doubleValue];
            } else if (row == 1) {
                cell = [self colorPickerCell:cell name:@"Foreground Color" color:_currentTheme.foregroundColor];
            } else if (row == 2) {
                cell = [self colorPickerCell:cell name:@"Background Color" color:_currentTheme.backgroundColor];
            }
        }
    }
    
    return cell;
}

//MARK: Custom Cells

-(UITableViewCell *)colorPickerCell:(UITableViewCell *)cell name:(NSString *)name color:(UIColor *)color {
    // Set Title
    cell.textLabel.text = name;
    
    // Set Accessory
    UIView *accessoryView = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 80, cell.frame.size.height - 15)];
    accessoryView.layer.borderWidth = 2;
    accessoryView.layer.borderColor = UIColor.grayColor.CGColor;
    accessoryView.layer.cornerRadius = 10;
    accessoryView.backgroundColor = color;
    
    cell.accessoryView = accessoryView;
    
    return cell;
}
@end
