//
//  EditSchemeViewController.m
//  iSH
//
//  Created by Corban Amouzou on 2021-06-11.
//

#import "EditSchemeViewController.h"
#import "NSObject+SaneKVO.h"
#import "UIColor+additions.h"
static NSString *kEditSchemeStatusBarToggleId = @"ToggleCell";
static NSString *kEditPreviewId = @"Preview";
static NSString *kEditColorPickerCellId = @"ColorPicker";
@implementation EditSchemeViewController

enum {
    PreviewSection,
    StatusBarSection,
    NumberOfSections,
};

- (void)viewDidLoad {
    [super viewDidLoad];
    self.tableView.sectionHeaderHeight = UITableViewAutomaticDimension;
    self.currentScheme = [UserPreferences.shared schemeFromName:self.schemeName];
    self.tableView.separatorStyle = UITableViewCellSeparatorStyleNone;
    oldBackgroundColor = self.navigationController.navigationBar.barTintColor;
    oldForegroundColor = self.navigationController.navigationBar.tintColor;
    [self observe:@[@"currentScheme"] options:0 owner:self usingBlock:^(typeof(self) self) {
        [self setAppearance];
        [[self tableView] reloadData];
    }];
    [self setAppearance];
}
- (void)setAppearance {
    self.navigationController.navigationBar.tintColor = _currentScheme.foregroundColor;
    self.navigationController.navigationBar.barTintColor = _currentScheme.backgroundColor;
    self.navigationController.navigationBar.titleTextAttributes = @{NSForegroundColorAttributeName:_currentScheme.foregroundColor};
    self.navigationController.navigationBar.translucent = NO;
    if (@available(iOS 13, *)) {
        self.tableView.backgroundColor = UIColor.systemGray6Color;
    }
}
- (void)hookBackButton {
    self.navigationItem.backBarButtonItem.target = self;
    self.navigationItem.backBarButtonItem.action = @selector(navigateBack);
}
- (void) navigateBack {
    [_delegate schemeChanged];
    [self.navigationController popToViewController:_delegate animated:true];
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
    headerView.backgroundColor = _currentScheme.backgroundColor;
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
            return kEditSchemeStatusBarToggleId;
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
                cell.textLabel.text = [NSString stringWithFormat:@"%@:~# %@", [UIDevice currentDevice].name, _currentScheme.name];
                cell.backgroundColor = _currentScheme.backgroundColor;
                cell.textLabel.textColor = _currentScheme.foregroundColor;
                cell.textLabel.font = [UIFont fontWithName:UserPreferences.shared.fontFamily size:UserPreferences.shared.fontSize.doubleValue];
            } else if (row == 1) {
                cell = [self colorPickerCell:cell name:@"Foreground Color" color:_currentScheme.foregroundColor];
            } else if (row == 2) {
                cell = [self colorPickerCell:cell name:@"Background Color" color:_currentScheme.backgroundColor];
            }
        }
    }
    
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    switch (indexPath.section) {
        case PreviewSection: {
            if (indexPath.row > 0) {
                NSString *property = indexPath.row == 1 ? @"foregroundColor" : @"backgroundColor";
                if (@available(iOS 14.0, *)) {
                    [self sendColorPicker:[_currentScheme valueForKey:property] supportsAlpha:NO propertyName:property];
                } else {
                    UIAlertController *alertController = [UIAlertController alertControllerWithTitle:@"Set New Color" message:@"Set a new color for your scheme." preferredStyle:UIAlertControllerStyleAlert];
                    [alertController addTextFieldWithConfigurationHandler:^(UITextField * _Nonnull textField) {
                        textField.placeholder = @"#RRGGBB";
                    }];
                    
                    UIAlertAction *cancelAction = [UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction * _Nonnull action) {
                        // Memes 42069
                    }];
                    
                    UIAlertAction *saveAction = [UIAlertAction actionWithTitle:@"Save" style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
                        // Need to handle the new hex code
                        UITextField *alertField = alertController.textFields[0];
                        if (alertField.text.length == 6 || alertField.text.length == 7) {
                            [self setNewColor:[UIColor colorWithHexString:alertField.text] forProperty:property];
                        } else {
                            alertController.message = @"Hex value cannot be blank or not formatted properly";
                            [self presentViewController:alertController animated:true completion:nil];
                        }
                    }];
                    [alertController addAction:cancelAction];
                    [alertController addAction:saveAction];
                    [self presentViewController:alertController animated:true completion:nil];
                }
            }
        }
    }
}

- (void)sendColorPicker:(UIColor *)startingColor supportsAlpha:(BOOL)alpha propertyName:(NSString *)name API_AVAILABLE(ios(14.0)) {
    UIColorPickerViewController *viewController = [[UIColorPickerViewController alloc] init];
    viewController.selectedColor = startingColor;
    viewController.supportsAlpha = alpha;
    viewController.delegate = self;
    editingPropertyName = name;
    [self presentViewController:viewController animated:true completion:^{
        // memes (42069)
    }];
}

- (void) colorPickerViewControllerDidFinish:(UIColorPickerViewController *)viewController API_AVAILABLE(ios(14.0)) {
    [self setNewColor:viewController.selectedColor forProperty:editingPropertyName];
}

- (void) setNewColor:(UIColor *)color forProperty:(NSString *)property {
    [_currentScheme setValue:color forKey:property];
    [self setAppearance];
    [[self tableView] reloadData];
    [UserPreferences.shared modifyScheme:_currentScheme.name properties:_currentScheme.properties];
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
