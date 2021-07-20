/*
 *   Copyright (c) 2021 c0dine
 *   All rights reserved.
 *   Feel free to contribute!
 */
//
//  AboutViewController.m
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
#import "EditSchemeViewController.h"

@interface AboutAppearanceViewController ()
@property UIFontPickerViewController *fontPicker API_AVAILABLE(ios(13));
@end
@implementation AboutAppearanceViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [UserPreferences.shared observe:@[@"scheme", @"fontSize", @"fontFamily", @"hideStatusBar"]
                            options:0 owner:self usingBlock:^(typeof(self) self) {
        [self.tableView reloadData];
        [self setNeedsStatusBarAppearanceUpdate];
    }];
    [self setupSchemeOptionButton];
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
    StatusBarSection,
    NumberOfSections,
};

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return Scheme.schemeNames.count + 2;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    switch (section) {
        case CustomizationSection: return 2;
        case StatusBarSection: return 1;
        default: return 1;
    }
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    switch (section) {
        case CustomizationSection: return @"Customization";
        case StatusBarSection: return @"Status Bar";
        default: return Scheme.schemeNames[section - 2];
    }
}


- (NSString *)reuseIdentifierForIndexPath:(NSIndexPath *)indexPath {
    switch (indexPath.section) {
        case CustomizationSection: {
            return @[@"Font", @"Font Size"][indexPath.row];
        }
        case StatusBarSection: {
            return @"Status Bar";
        };
        default: {
            return @"Scheme Card";
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
            Scheme *currentScheme = [prefs schemeFromName:Scheme.schemeNames[indexPath.section - 2]];
            switch (indexPath.row) {
                case 0: {
                    if (prefs.scheme.name == currentScheme.name) {
                        cell.accessoryType = UITableViewCellAccessoryCheckmark;
                    } else {
                        cell.accessoryType = UITableViewCellAccessoryNone;
                    }
                    cell.backgroundColor = currentScheme.backgroundColor;
                    cell.textLabel.textColor = currentScheme.foregroundColor;
                    cell.textLabel.font = [UIFont fontWithName:UserPreferences.shared.fontFamily size:prefs.fontSize.doubleValue];
                    cell.textLabel.text = [NSString stringWithFormat:@"%@:~# ps aux", [UIDevice currentDevice].name];
                    cell.selectionStyle = UITableViewCellSelectionStyleNone;
                }
            }
            break;
        }
        case StatusBarSection: {
            cell.selectionStyle = UITableViewCellSelectionStyleNone;
            UISwitch *statusBarToggle = [[UISwitch alloc] initWithFrame:CGRectZero];
            cell.accessoryView = statusBarToggle;
            statusBarToggle.on = prefs.hideStatusBar;
            [statusBarToggle addTarget:self action:@selector(hideStatusBarChanged:) forControlEvents:UIControlEventValueChanged];
            break;
        }
    }
    
    return cell;
}

- (NSArray<UITableViewRowAction *> *)tableView:(UITableView *)tableView editActionsForRowAtIndexPath:(NSIndexPath *)indexPath {
    switch (indexPath.section) {
        case CustomizationSection:
            return nil;
        case StatusBarSection:
            return nil;
        default: {
            NSString *schemeName = Scheme.schemeNames[indexPath.section - 2];
            Scheme *currentScheme = [UserPreferences.shared schemeFromName:schemeName];
            UITableViewRowAction *editAction = [UITableViewRowAction rowActionWithStyle:UITableViewRowActionStyleNormal title:@"Edit" handler:^(UITableViewRowAction *action, NSIndexPath *indexPath) {
                [self editScheme:currentScheme.name];
            }];
            UITableViewRowAction *deleteAction = [UITableViewRowAction rowActionWithStyle:UITableViewRowActionStyleNormal title:@"Delete"  handler:^(UITableViewRowAction *action, NSIndexPath *indexPath) {
                [tableView beginUpdates];
                [tableView deleteSections:[NSIndexSet indexSetWithIndex:indexPath.section] withRowAnimation:UITableViewRowAnimationAutomatic];
                [UserPreferences.shared deleteScheme:schemeName];
                [tableView endUpdates];

            }];
            UITableViewRowAction *exportAction = [UITableViewRowAction rowActionWithStyle:UITableViewRowActionStyleNormal title:@"Export" handler:^(UITableViewRowAction * _Nonnull action, NSIndexPath * _Nonnull indexPath) {
                NSDictionary<NSString *, id> *props = currentScheme.properties;
                NSError *error = nil;
                NSURL *tmpUrl = [[NSFileManager defaultManager].temporaryDirectory URLByAppendingPathComponent:[NSString stringWithFormat:@"%@-scheme.plist", currentScheme.name]];
                [props writeToURL:tmpUrl error:&error];
                UIActivityViewController *controller = [[UIActivityViewController alloc] initWithActivityItems:@[tmpUrl] applicationActivities:nil];
                controller.popoverPresentationController.sourceView = self.tableView;
                [self presentViewController:controller animated:YES completion:nil];
            }];
            deleteAction.backgroundColor = [UIColor redColor];
            editAction.backgroundColor = [self adjustColor:currentScheme.backgroundColor];
            exportAction.backgroundColor = [self adjustColor:currentScheme.foregroundColor];
            
            if ([[Scheme.presets allKeys] containsObject:schemeName]) {
                return @[exportAction, editAction];
            } else {
                if (UserPreferences.shared.scheme.name == schemeName) {
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
    } else if (indexPath.section > CustomizationSection && indexPath.section != StatusBarSection) {
        NSString *currentName = Scheme.schemeNames[indexPath.section - 2];
        [UserPreferences.shared setSchemeToName:currentName];
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

#pragma mark Scheme Specifics
- (void) editScheme:(NSString *)schemeName {
    EditSchemeViewController *schemeEditor = [self.storyboard instantiateViewControllerWithIdentifier:@"SchemeEditor"];
    schemeEditor.navigationItem.title = [NSString stringWithFormat:@"Edit %@", schemeName];
    [schemeEditor setSchemeName:schemeName];
    schemeEditor.delegate = self;
    [self.navigationController pushViewController:schemeEditor animated:YES];
}

- (void) schemeChanged {
    [[self tableView] reloadSections:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, [self numberOfSectionsInTableView:[self tableView]])] withRowAnimation:UITableViewRowAnimationAutomatic];
}

- (UIColor *) adjustColor:(UIColor *)color {
    CGFloat hue, saturation, oldBrightness, alpha;
    [color getHue:&hue saturation:&saturation brightness:&oldBrightness alpha:&alpha];
    CGFloat newBrightness = color.isLight ? oldBrightness * 0.8 : (oldBrightness == 0 ? 0.1 : oldBrightness) * 2;
    return [UIColor colorWithHue:hue saturation:saturation brightness:newBrightness alpha:alpha];
}

- (void) setupSchemeOptionButton {
    UIBarButtonItem *barButton = [[UIBarButtonItem alloc] initWithTitle:@"•••" style:UIBarButtonItemStylePlain target:self action:@selector(schemeOptionButtonPressed)];
    NSDictionary<NSString *, id> *attr = @{
        NSFontAttributeName: [UIFont systemFontOfSize:27],
    };
    [barButton setTitleTextAttributes:attr forState:UIControlStateNormal];
    [barButton setTitleTextAttributes:attr forState:UIControlStateSelected];
    self.navigationItem.rightBarButtonItem = barButton;
}

- (void)schemeOptionButtonPressed {
    UIAlertController *popupSelector = [UIAlertController alertControllerWithTitle:@"Scheme Options" message:nil preferredStyle:UIAlertControllerStyleActionSheet];
    UIAlertAction *importAction = [UIAlertAction actionWithTitle:@"Import Scheme" style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
        UIDocumentPickerViewController *controller = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"com.apple.property-list"] inMode:UIDocumentPickerModeImport];
        controller.allowsMultipleSelection = false;
        controller.delegate = self;
        if (@available(iOS 13.0, *)) controller.shouldShowFileExtensions = true;
        [self presentViewController:controller animated:true completion:nil];
    }];
    UIAlertAction *cancelAction = [UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction * _Nonnull action) {
        // Bruh
    }];
    
    UIAlertAction *createAction = [UIAlertAction actionWithTitle:@"Add Scheme" style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
        Scheme *defaultProperties = [Scheme presets][@"Light"];
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
            if ([Scheme.schemeNames containsObject:nameField.text]) {
                failWithMessage (@"This name already exists");
            }
            
            defaultProperties.name = nameField.text;
            [UserPreferences.shared modifyScheme:defaultProperties.name properties:defaultProperties.properties];
            [self.tableView reloadData];
            [self editScheme:defaultProperties.name];
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
        UIAlertController *errorController = [UIAlertController alertControllerWithTitle:@"Error Importing Scheme" message:message preferredStyle:UIAlertControllerStyleAlert];
        UIAlertAction *okAction = [UIAlertAction actionWithTitle:@"Oops" style:UIAlertActionStyleDefault handler:^(UIAlertAction * _Nonnull action) {
            // 69 haha funny number
        }];
        [errorController addAction:okAction];
        [self presentViewController:errorController animated:YES completion:nil];
        return;
    };
    
    // There should only be one document selected explicitly
    NSURL *ourUrl = urls.firstObject;
    NSMutableDictionary<NSString *, id> *schemeData = [NSMutableDictionary dictionaryWithContentsOfURL:ourUrl];
    NSArray<NSString *> *propertyNames = [UserPreferences.shared.scheme.properties allKeys];
    for (NSString *name in propertyNames) { // sanity check
        if (schemeData[name] == nil) {
            // We have an issue with the data within the json because it doesn't meet all of the property requirements
            failWithMessage([NSString stringWithFormat:@"Scheme Export is missing the property %@", name]);
        }
        if ([@[@"forgroundColor", @"backgroundColor"] containsObject:name]) {
            schemeData[name] = [UIColor colorWithHexString:schemeData[name]];
        }
        if ([name isEqual: @"palette"]) {
            NSMutableArray *array = schemeData[name];
            for (int i = 0; i < array.count; i++) {
                NSString *color = array[i];
                array[i] = [UIColor colorWithHexString:color];
            }
            schemeData[name] = array;
        }
    }
    Scheme *schemeToImport = [[Scheme alloc] initWithProperties:schemeData];
    if ([Scheme.schemeNames containsObject:schemeToImport.name])
        failWithMessage([NSString stringWithFormat:@"The scheme %@ already exists", schemeToImport.name]);

    
    // OK we can now import the scheme
    [UserPreferences.shared modifyScheme:schemeToImport.name properties:schemeToImport.properties];
    
 }

- (void) hideStatusBarChanged:(UISwitch *)sender {
    UserPreferences.shared.hideStatusBar = sender.on;
    [self setNeedsStatusBarAppearanceUpdate];
}

@end
