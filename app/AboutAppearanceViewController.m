//
//  ThemeViewController.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//
#import <dispatch/dispatch.h>
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
    [self setupThemeOptionButton];
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
            UITableViewRowAction *exportAction = [UITableViewRowAction rowActionWithStyle:UITableViewRowActionStyleNormal title:@"Export" handler:^(UITableViewRowAction * _Nonnull action, NSIndexPath * _Nonnull indexPath) {
                NSDictionary<NSString *, id> *props = currentTheme.properties;
                NSError *error = nil;
                NSData *jsonData = [NSJSONSerialization dataWithJSONObject:props options:NSJSONWritingPrettyPrinted error:&error];
                NSURL *tmpUrl = [[NSFileManager defaultManager].temporaryDirectory URLByAppendingPathComponent:[NSString stringWithFormat:@"%@.theme", currentTheme.name]];
                [[[NSString alloc] initWithData:jsonData encoding:NSUTF8StringEncoding] writeToFile:tmpUrl.absoluteString atomically:NO encoding:NSUTF8StringEncoding error:&error];
                UIActivityViewController *controller = [[UIActivityViewController alloc] initWithActivityItems:@[tmpUrl] applicationActivities:nil];
                controller.popoverPresentationController.sourceView = self.tableView;
                [self presentViewController:controller animated:YES completion:nil];
            }];
            deleteAction.backgroundColor = [UIColor redColor];
            editAction.backgroundColor = [self adjustColor:currentTheme.backgroundColor];
            exportAction.backgroundColor = [self adjustColor:currentTheme.foregroundColor];
            
            if ([[Theme.presets allKeys] containsObject:themeName]) {
                return @[exportAction, editAction];
            } else {
                if (UserPreferences.shared.theme.name == themeName) {
                    return @[exportAction, editAction];
                } else {
                    return @[deleteAction, exportAction, editAction];
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
#pragma mark Font Specifics
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

#pragma mark Theme Specifics
- (void) editTheme:(NSString *)themeName {
    EditThemeViewController *themeEditor = [self.storyboard instantiateViewControllerWithIdentifier:@"ThemeEditor"];
    themeEditor.navigationItem.title = [NSString stringWithFormat:@"Edit %@", themeName];
    [themeEditor setThemeName:themeName];
    themeEditor.delegate = self;
    [self.navigationController pushViewController:themeEditor animated:YES];
}

- (void) themeChanged {
    [[self tableView] reloadSections:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, [self numberOfSectionsInTableView:[self tableView]])] withRowAnimation:UITableViewRowAnimationAutomatic];
}

- (UIColor *) adjustColor:(UIColor *)color {
    CGFloat hue, saturation, oldBrightness, alpha;
    [color getHue:&hue saturation:&saturation brightness:&oldBrightness alpha:&alpha];
    CGFloat newBrightness = color.isLight ? oldBrightness * 0.8 : (oldBrightness == 0 ? 0.1 : oldBrightness) * 2;
    return [UIColor colorWithHue:hue saturation:saturation brightness:newBrightness alpha:alpha];
}

- (void) setupThemeOptionButton {
    UIBarButtonItem *barButton = [[UIBarButtonItem alloc] initWithTitle:@"•••" style:UIBarButtonItemStylePlain target:self action:@selector(themeOptionButtonPressed)];
    NSDictionary<NSString *, id> *attr = @{
        NSFontAttributeName: [UIFont systemFontOfSize:27],
    };
    [barButton setTitleTextAttributes:attr forState:UIControlStateNormal];
    [barButton setTitleTextAttributes:attr forState:UIControlStateSelected];
    self.navigationItem.rightBarButtonItem = barButton;
}

- (void)themeOptionButtonPressed {
    UIAlertController *popupSelector = [UIAlertController alertControllerWithTitle:@"Theme Options" message:nil preferredStyle:UIAlertControllerStyleActionSheet];
    UIAlertAction *importAction = [UIAlertAction actionWithTitle:@"Import Theme" style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
        UIDocumentPickerViewController *controller = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"public.data"] inMode:UIDocumentPickerModeImport];
        controller.allowsMultipleSelection = false;
        controller.delegate = self;
        if (@available(iOS 13.0, *)) controller.shouldShowFileExtensions = true;
        [self presentViewController:controller animated:true completion:nil];
    }];
    UIAlertAction *cancelAction = [UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction * _Nonnull action) {
        // Bruh
    }];
    
    UIAlertAction *createAction = [UIAlertAction actionWithTitle:@"Add Theme" style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
        Theme *defaultProperties = [Theme presets][@"Light"];
        UIAlertController *nameController = [UIAlertController alertControllerWithTitle:@"Set Name" message:nil preferredStyle:UIAlertControllerStyleAlert];
        [nameController addTextFieldWithConfigurationHandler:^(UITextField * _Nonnull textField) {
            textField.placeholder = @"Name";
        }];
        UIAlertAction *cancelAction = [UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction * _Nonnull action) {
            // Do nothing
        }];
        
        UIAlertAction *continueAction = [UIAlertAction actionWithTitle:@"Continue" style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
            UITextField *nameField = nameController.textFields[0];
            void (^failWithMessage)(NSString *) = ^void(NSString *message) {
                nameController.message = message;
                [self presentViewController:nameController animated:true completion:nil];
                return;
            };
            
            if (nameField.text.length == 0) {
                failWithMessage(@"The name cannot be blank");
            }
            if ([Theme.themeNames containsObject:nameField.text]) {
                failWithMessage (@"This name already exists");
            }
            
            defaultProperties.name = nameField.text;
            [UserPreferences.shared modifyTheme:defaultProperties.name properties:defaultProperties.properties];
            [self.tableView reloadData];
            [self editTheme:defaultProperties.name];
        }];
        
        [nameController addAction:cancelAction];
        [nameController addAction:continueAction];
        [self presentViewController:nameController animated:true completion:nil];
        
    }];
    [popupSelector addAction:createAction];
    [popupSelector addAction:importAction];
    [popupSelector addAction:cancelAction];
    
    [self presentViewController:popupSelector animated:true completion:nil];
    //TODO: Make batch import/export a thing...
    
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    
    void (^failWithMessage)(NSString *) = ^void(NSString *message) {
        UIAlertController *errorController = [UIAlertController alertControllerWithTitle:@"Error Importing Theme" message:message preferredStyle:UIAlertControllerStyleAlert];
        UIAlertAction *okAction = [UIAlertAction actionWithTitle:@"Oops" style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
            // 69 haha funny number
        }];
        [errorController addAction:okAction];
        [self presentViewController:errorController animated:YES completion:nil];
        return;
    };
    
    // There should only be one document selected explicitly
    NSURL *ourUrl = urls.firstObject;
    NSData *jsonData = [NSData dataWithContentsOfURL:ourUrl];
    NSError *error = nil;
    NSDictionary<NSString *, id> *themeData = [NSJSONSerialization JSONObjectWithData:jsonData options:0 error:&error];
    NSArray<NSString *> *propertyNames = [UserPreferences.shared.theme.properties allKeys];
    for (NSString *name in propertyNames) { // sanity check
        if (themeData[name] == nil) {
            // We have an issue with the data within the json because it doesn't meet all of the property requirements
            failWithMessage([NSString stringWithFormat:@"Theme Export is missing the property %@", name]);
        }
    }
    Theme *themeToImport = [[Theme alloc] initWithProperties:themeData];
    if ([Theme.themeNames containsObject:themeToImport.name])
        failWithMessage([NSString stringWithFormat:@"The theme %@ already exists", themeToImport.name]);

    
    // OK we can now import the theme
    [UserPreferences.shared modifyTheme:themeToImport.name properties:themeToImport.properties];
    
 }

@end
