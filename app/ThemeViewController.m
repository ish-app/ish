//
//  ThemeViewController.m
//  libiSHApp
//
//  Created by Saagar Jha on 7/16/22.
//

#import "ThemeViewController.h"

#import "Theme.h"

#define COLORS 16

static NSString *colorNames[] = {
    @"Black",
    @"Red",
    @"Green",
    @"Yellow",
    @"Blue",
    @"Magenta",
    @"Cyan",
    @"White",
    @"Bright Black",
    @"Bright Red",
    @"Bright Green",
    @"Bright Yellow",
    @"Bright Blue",
    @"Bright Magenta",
    @"Bright Cyan",
    @"Bright White",
};

struct PaletteTextFields {
    UITextField *foregroundTextField;
    UITextField *backgroundTextField;
    UITextField *cursorTextField;
    NSArray<UITextField *> *colorTextFields;
};

@implementation ThemeViewController {
    UITextField *_nameTextField;
    UISwitch *_singlePaletteSwitch;
    UISwitch *_lightOverrideSwitch;
    UISwitch *_darkOverrideSwitch;
    BOOL _touchedOverrideSwitches;
    struct PaletteTextFields _paletteTextFields[2];
    BOOL _duplicated;
}

- (UITextField *)detailTextFieldWithText:(NSString *)text monospaced:(BOOL)monospaced {
    UITextField *textField = [UITextField new];
    textField.tag = 1;
    [textField addTarget:self action:@selector(textFieldChanged:) forControlEvents:UIControlEventEditingChanged];
    textField.text = textField.placeholder = text;
    textField.translatesAutoresizingMaskIntoConstraints = NO;
    textField.textAlignment = NSTextAlignmentRight;
    if (@available(iOS 13.0, *)) {
        if (monospaced) {
            textField.font = [UIFont monospacedSystemFontOfSize:textField.font.pointSize weight:UIFontWeightRegular];
        }
    }
    return textField;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.navigationItem.title = self.theme.name;
    
    _nameTextField = [self detailTextFieldWithText:_theme.name monospaced: NO];
    _singlePaletteSwitch = [UISwitch new];
    _singlePaletteSwitch.on = self.theme.lightPalette == self.theme.darkPalette;
    [_singlePaletteSwitch addTarget:self action:@selector(singlePaletteChanged:) forControlEvents:UIControlEventValueChanged];
    
    for (int i = 0; i < sizeof(_paletteTextFields) / sizeof(*_paletteTextFields); ++i) {
        Palette *palette = i ? self.theme.darkPalette : self.theme.lightPalette;
        _paletteTextFields[i].foregroundTextField = [self detailTextFieldWithText:palette.foregroundColor monospaced:YES];
        _paletteTextFields[i].backgroundTextField = [self detailTextFieldWithText:palette.backgroundColor monospaced:YES];
        _paletteTextFields[i].cursorTextField = [self detailTextFieldWithText:palette.cursorColor monospaced:YES];
        NSMutableArray<UITextField *> *textFields = [NSMutableArray new];
        for (int j = 0; j < COLORS; ++j) {
            UITextField *textField = [self detailTextFieldWithText:palette.colorPaletteOverrides ? palette.colorPaletteOverrides[j] : nil monospaced: YES];
            textField.autocorrectionType = UITextAutocorrectionTypeNo;
            textField.autocapitalizationType = UITextAutocapitalizationTypeNone;
            [textFields addObject:textField];
        }
        _paletteTextFields[i].colorTextFields = textFields;
    }
    
    if (!self.isEditable) {
        _singlePaletteSwitch.enabled = NO;
    }
    
    _lightOverrideSwitch = [UISwitch new];
    _lightOverrideSwitch.on = self.theme.appearance.lightOverride;
    [_lightOverrideSwitch addTarget:self action:@selector(touchedOverrideSwitch:) forControlEvents:UIControlEventValueChanged];
    _darkOverrideSwitch = [UISwitch new];
    _darkOverrideSwitch.on = self.theme.appearance.darkOverride;
    [_darkOverrideSwitch addTarget:self action:@selector(touchedOverrideSwitch:) forControlEvents:UIControlEventValueChanged];
    
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Duplicate" style:UIBarButtonItemStylePlain target:self action:@selector(duplicate:)];
}

- (void)duplicate:(UIBarButtonItem *)sender {
    [self.theme duplicateAsUserTheme];
    self->_duplicated = YES;
    [self.navigationController popViewControllerAnimated:YES];
}

Palette *createPalette(struct PaletteTextFields *paletteTextFields) {
    NSMutableArray<NSString *> *colors = [NSMutableArray new];
    for (UITextField *textField in paletteTextFields->colorTextFields) {
        if (textField.text.length) {
            [colors addObject:textField.text];
        }
    }
    return [[Palette alloc] initWithForegroundColor:paletteTextFields->foregroundTextField.text
                                    backgroundColor:paletteTextFields->backgroundTextField.text
                                        cursorColor:paletteTextFields->cursorTextField.text.length ? paletteTextFields->cursorTextField.text : nil
                              colorPaletteOverrides:colors.count == COLORS ? colors : nil];
}

- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated];
    if (self.isEditable && !self->_duplicated && [self validateTheme]) {
        Theme *theme;
        ThemeAppearance *appearance = self->_touchedOverrideSwitches ? [[ThemeAppearance alloc] initWithLightOverride:self->_lightOverrideSwitch.on darkOverride:self->_darkOverrideSwitch.on] : nil;
        if (_singlePaletteSwitch.on) {
            theme = [[Theme alloc] initWithName:_nameTextField.text palette:createPalette(_paletteTextFields + 0) appearance:appearance];
        } else {
            theme = [[Theme alloc] initWithName:_nameTextField.text lightPalette:createPalette(_paletteTextFields + 0) darkPalette:createPalette(_paletteTextFields + 1) appearance:appearance];
        }
        [self.theme replaceWithUserTheme:theme];
    }
}

#pragma mark - Table view data source

enum {
    NameSection,
    SinglePaletteSection,
    PaletteSection,
    PaletteSection2,
    UIOverrideSection,
    NumberOfSections,
};

enum {
    ForegroundRow,
    BackgroundRow,
    CursorRow,
    NumberOfRows = CursorRow + COLORS + 1,
};

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return NumberOfSections;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    if ([self shouldHideSection:section]) {
        return 0;
    }
    switch (section) {
        case NameSection:
            return 1;
        case SinglePaletteSection:
            return 1;
        case PaletteSection:
        case PaletteSection2:
            return NumberOfRows;
        case UIOverrideSection:
            return 2;
        default:
            NSAssert(NO, @"unhandled section"); return 0;
    }
}

- (BOOL)shouldHideSection:(NSInteger)section {
    return section == PaletteSection2 && _singlePaletteSwitch.on;
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    if ([self shouldHideSection:section]) {
        return nil;
    }
    switch (section) {
        case PaletteSection:
            return _singlePaletteSwitch.on ? @"Palette" : @"Light Palette";
        case PaletteSection2:
            return @"Dark Palette";
        case UIOverrideSection:
            return @"UI Overrides";
        default:
            return nil;
    }
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    if ([self shouldHideSection:section]) {
        return nil;
    }
    switch (section) {
        case NameSection:
            return ![_nameTextField.text isEqualToString:self.theme.name] && [Theme themeForName:_nameTextField.text includingDefaultThemes:NO] ? @"A user theme with this name already exists." : nil;
        case SinglePaletteSection:
            return @"When this is enabled, light and dark color schemes will share a single palette.";
        case UIOverrideSection:
            return @"Use a customized color scheme for user interface elements (keyboard, status bar) rather than one that matches the current palette.";
        default:
            return nil;
    }
}

- (CGFloat)tableView:(UITableView *)tableView heightForHeaderInSection:(NSInteger)section {
    return [self shouldHideSection:section] ? CGFLOAT_MIN : UITableViewAutomaticDimension;
}

- (CGFloat)tableView:(UITableView *)tableView heightForFooterInSection:(NSInteger)section {
    return [self shouldHideSection:section] ? CGFLOAT_MIN : UITableViewAutomaticDimension;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:@"ThemeSetting" forIndexPath:indexPath];
    [[cell viewWithTag:1] removeFromSuperview];
    if (self.isEditable) {
        cell.detailTextLabel.hidden = YES;
    } else {
        cell.detailTextLabel.hidden = NO;
        cell.detailTextLabel.enabled = NO;
    }
    cell.accessoryView = nil;
    switch (indexPath.section) {
        case NameSection:
            cell.textLabel.text = @"Name";
            if (self.isEditable) {
                [cell.contentView addSubview:_nameTextField];
                [NSLayoutConstraint activateConstraints:@[
                    [_nameTextField.leadingAnchor constraintEqualToSystemSpacingAfterAnchor:cell.textLabel.trailingAnchor multiplier:1],
                    [_nameTextField.trailingAnchor constraintEqualToAnchor:cell.detailTextLabel.trailingAnchor],
                    [_nameTextField.firstBaselineAnchor constraintEqualToAnchor:cell.detailTextLabel.firstBaselineAnchor],
                ]];
            } else {
                cell.detailTextLabel.text = self.theme.name;
                if (@available(iOS 13.0, *)) {
                    cell.detailTextLabel.font = [UIFont systemFontOfSize:cell.detailTextLabel.font.pointSize];
                }
            }
            break;
        case SinglePaletteSection:
            cell.textLabel.text = @"Single Palette";
            cell.detailTextLabel.hidden = YES;
            cell.accessoryView = _singlePaletteSwitch;
            break;
        case PaletteSection:
        case PaletteSection2: {
            UITextField *detailTextField;
            switch (indexPath.row) {
                case ForegroundRow:
                    cell.textLabel.text = @"Foreground Color";
                    detailTextField = _paletteTextFields[indexPath.section - PaletteSection].foregroundTextField;
                    break;
                case BackgroundRow:
                    cell.textLabel.text = @"Background Color";
                    detailTextField = _paletteTextFields[indexPath.section - PaletteSection].backgroundTextField;
                    break;
                case CursorRow:
                    cell.textLabel.text = @"Cursor Color";
                    detailTextField = _paletteTextFields[indexPath.section - PaletteSection].cursorTextField;
                    break;
                default:
                    cell.textLabel.text = colorNames[indexPath.row - CursorRow - 1];
                    detailTextField = _paletteTextFields[indexPath.section - PaletteSection].colorTextFields[indexPath.row - CursorRow - 1];
                    break;
            }
            if (self.isEditable) {
                [cell.contentView addSubview:detailTextField];
                [NSLayoutConstraint activateConstraints:@[
                    [detailTextField.leadingAnchor constraintEqualToSystemSpacingAfterAnchor:cell.textLabel.trailingAnchor multiplier:1],
                    [detailTextField.trailingAnchor constraintEqualToAnchor:cell.detailTextLabel.trailingAnchor],
                    [detailTextField.firstBaselineAnchor constraintEqualToAnchor:cell.detailTextLabel.firstBaselineAnchor],
                ]];
            } else {
                cell.detailTextLabel.text = detailTextField.text;
                if (@available(iOS 13.0, *)) {
                    cell.detailTextLabel.font = [UIFont monospacedSystemFontOfSize:cell.detailTextLabel.font.pointSize weight:UIFontWeightRegular];
                }
            }
            break;
        }
        case UIOverrideSection:
            cell.detailTextLabel.hidden = YES;
            switch (indexPath.row) {
                case 0:
                    cell.textLabel.text = @"Use Dark UI for Light Color Scheme";
                    cell.accessoryView = self->_lightOverrideSwitch;
                    break;
                case 1:
                    cell.textLabel.text = @"Use Light UI for Dark Color Scheme";
                    cell.accessoryView = self->_darkOverrideSwitch;
                    break;
                default:
                    NSAssert(NO, @"Invalid row");
            }
            break;
    }
    
    if (!self.isEditable) {
        cell.textLabel.enabled = NO;
    }
    
    return cell;
}

- (BOOL)validateTheme {
    BOOL validName = _nameTextField.text.length && ([_nameTextField.text isEqualToString:self.theme.name] || ![Theme themeForName:_nameTextField.text includingDefaultThemes:NO]);
    _nameTextField.textColor = validName ? nil : UIColor.systemRedColor;
    [self.tableView reloadSections:[NSIndexSet indexSetWithIndex:NameSection] withRowAnimation:UITableViewRowAnimationNone];
    BOOL validColors = YES;
    BOOL (^validColor)(UITextField *) = ^(UITextField *textField) {
        BOOL valid = !![[UIColor alloc] ish_initWithHexString:textField.text];
        textField.textColor = valid ? nil : UIColor.systemRedColor;
        return valid;
    };
    for (int i = 0; i < sizeof(_paletteTextFields) / sizeof(*_paletteTextFields) - _singlePaletteSwitch.on; ++i) {
        validColors &= validColor(_paletteTextFields[i].foregroundTextField);
        validColors &= validColor(_paletteTextFields[i].backgroundTextField);
        validColors &= !_paletteTextFields[i].cursorTextField.text.length || validColor(_paletteTextFields[i].cursorTextField);
        int empty = 0;
        int valid = 0;
        for (int j = 0; j < COLORS; ++j) {
            empty += !_paletteTextFields[i].colorTextFields[j].text.length;
            valid += validColor(_paletteTextFields[i].colorTextFields[j]);
        }
        validColors &= (empty == COLORS || valid == COLORS);
    }
    return !(self.navigationItem.hidesBackButton = !validName || !validColors);
}

- (void)textFieldChanged:(UITextField *)sender {
    // Hack to keep the keyboard up across a table view update
    UITextRange *selectedRange = sender.isFirstResponder ? sender.selectedTextRange : nil;
    [self validateTheme];
    if (selectedRange) {
        [sender becomeFirstResponder];
        [sender setSelectedTextRange:selectedRange];
    }
}

- (void)singlePaletteChanged:(UISwitch *)sender {
    [self.tableView performBatchUpdates:^{
        for (int i = 0; i < NumberOfRows; ++i) {
            if (sender.on) {
                [self.tableView deleteRowsAtIndexPaths:@[[NSIndexPath indexPathForRow:i inSection:PaletteSection2]] withRowAnimation:UITableViewRowAnimationFade];
            } else {
                [self.tableView insertRowsAtIndexPaths:@[[NSIndexPath indexPathForRow:i inSection:PaletteSection2]] withRowAnimation:UITableViewRowAnimationFade];
            }
        }
        [self.tableView reloadSections:[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(PaletteSection, PaletteSection2 - PaletteSection)] withRowAnimation:UITableViewRowAnimationAutomatic];
    } completion:nil];
    [self validateTheme];
}

- (void)touchedOverrideSwitch:(UISwitch *)sender {
    self->_touchedOverrideSwitches = YES;
}

@end
