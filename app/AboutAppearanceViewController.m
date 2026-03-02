//
//  ThemeViewController.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import "AboutAppearanceViewController.h"
#import "FontPickerViewController.h"
#import "TerminalView.h"
#import "ThemesViewController.h"
#import "UserPreferences.h"
#import "NSObject+SaneKVO.h"

@interface AboutAppearanceViewController ()
@property (strong, nonatomic) IBOutlet UISwitch *blinkCursor;
@property (strong, nonatomic) IBOutlet UISegmentedControl *cursorStyle;
@property (strong, nonatomic) IBOutlet UISwitch *hideStatusBar;
@property UIFontPickerViewController *fontPicker API_AVAILABLE(ios(13));
@end

char *previewString = "# cat /proc/ish/colors\r\n"
"\x1B[30m" "iSH" "\x1B[39m "
"\x1B[31m" "iSH" "\x1B[39m "
"\x1B[32m" "iSH" "\x1B[39m "
"\x1B[33m" "iSH" "\x1B[39m "
"\x1B[34m" "iSH" "\x1B[39m "
"\x1B[35m" "iSH" "\x1B[39m "
"\x1B[36m" "iSH" "\x1B[39m "
"\x1B[37m" "iSH" "\x1B[39m" "\r\n\x1B[7m"
"\x1B[40m" "iSH" "\x1B[39m "
"\x1B[41m" "iSH" "\x1B[39m "
"\x1B[42m" "iSH" "\x1B[39m "
"\x1B[43m" "iSH" "\x1B[39m "
"\x1B[44m" "iSH" "\x1B[39m "
"\x1B[45m" "iSH" "\x1B[39m "
"\x1B[46m" "iSH" "\x1B[39m "
"\x1B[47m" "iSH" "\x1B[39m" "\x1B[0m\x1B[1m\r\n"
"\x1B[90m" "iSH" "\x1B[39m "
"\x1B[91m" "iSH" "\x1B[39m "
"\x1B[92m" "iSH" "\x1B[39m "
"\x1B[93m" "iSH" "\x1B[39m "
"\x1B[94m" "iSH" "\x1B[39m "
"\x1B[95m" "iSH" "\x1B[39m "
"\x1B[96m" "iSH" "\x1B[39m "
"\x1B[97m" "iSH" "\x1B[39m" "\r\n\x1B[7m"
"\x1B[100m" "iSH" "\x1B[39m "
"\x1B[101m" "iSH" "\x1B[39m "
"\x1B[102m" "iSH" "\x1B[39m "
"\x1B[103m" "iSH" "\x1B[39m "
"\x1B[104m" "iSH" "\x1B[39m "
"\x1B[105m" "iSH" "\x1B[39m "
"\x1B[106m" "iSH" "\x1B[39m "
"\x1B[107m" "iSH" "\x1B[39m" "\x1B[0m\r\n"
"# ";

@implementation AboutAppearanceViewController {
    TerminalView *_terminalView;
    Terminal *_terminal;
    struct tty *_tty;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    [UserPreferences.shared observe:@[@"theme", @"fontSize", @"fontFamily", @"colorScheme"]
                            options:0 owner:self usingBlock:^(typeof(self) self) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.tableView reloadData];
        });
    }];
    
    [UserPreferences.shared observe:@[@"cursorStyle", @"blinkCursor", @"hideStatusBar"]
                            options:0 owner:self usingBlock:^(typeof(self) self) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self updateOtherControls];
        });
    }];
    [self updateOtherControls];
    
#if !ISH_LINUX
    if (![NSUserDefaults.standardUserDefaults boolForKey:@"recovery"]) {
        _terminal = [Terminal createPseudoTerminal:&_tty];
        [_terminal sendOutput:previewString length:(int)strlen(previewString)];
    }
#endif
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
        self.fontPicker.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Reset" style:UIBarButtonItemStylePlain target:self action:@selector(resetFont:)];
    }
}

#pragma mark - Table view data source

enum {
    PreviewSection,
    MainSection,
    ColorSchemeSection,
    CursorSection,
    StatusBarSection,
    NumberOfSections,
};

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return NumberOfSections;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    switch (section) {
        case PreviewSection: return 2;
        case MainSection: return 3;
        case ColorSchemeSection: return 3;
        case CursorSection: return 2;
        case StatusBarSection: return 1;
        default: NSAssert(NO, @"unhandled section"); return 0;
    }
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    switch (section) {
        case PreviewSection: return @"Preview";
        case ColorSchemeSection: return @"Color Scheme";
        case CursorSection: return @"Cursor";
        case StatusBarSection: return @"Status Bar";
        default: return nil;
    }
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    switch (section) {
        case PreviewSection: return @"Change the color scheme used for the preview.";
        default: return nil;
    }
}

- (NSString *)reuseIdentifierForIndexPath:(NSIndexPath *)indexPath {
    switch (indexPath.section) {
        case PreviewSection: return @[@"Preview", @"Color Scheme Preview"][indexPath.row];
        case MainSection: return @[@"Theme Name", @"Font", @"Font Size"][indexPath.row];
        case ColorSchemeSection: return @"Color Scheme";
        case CursorSection: return @[@"Cursor Style", @"Blink Cursor"][indexPath.row];
        case StatusBarSection: return @"Status Bar";
        default: return nil;
    }
}

- (CGFloat)tableView:(UITableView *)tableView heightForRowAtIndexPath:(NSIndexPath *)indexPath {
    if (indexPath.section == PreviewSection && indexPath.row == 0) {
        // Try a best-effort guess as to how big the preview should be.
        return [@"\n\n\n\n\n\n" sizeWithAttributes:@{NSFontAttributeName: UserPreferences.shared.approximateFont}].height + 10;
    } else {
        return UITableViewAutomaticDimension;
    }
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:[self reuseIdentifierForIndexPath:indexPath] forIndexPath:indexPath];
    cell.selectionStyle = UITableViewCellSelectionStyleDefault;
    
    switch (indexPath.section) {
        case PreviewSection:
            switch (indexPath.row) {
                case 0:
                    _terminalView = [cell viewWithTag:1];
                    _terminalView.userInteractionEnabled = NO;
                    _terminalView.terminal = _terminal;
                    break;
                case 1: {
                    UISegmentedControl *segmentedControl = [cell viewWithTag:1];
                    [segmentedControl addTarget:self action:@selector(changePreviewTheme:) forControlEvents:UIControlEventValueChanged];
                    [self changePreviewTheme:segmentedControl];
                    cell.selectionStyle = UITableViewCellSelectionStyleNone;
                    break;
                }
            }
            break;
            
        case MainSection:
            switch (indexPath.row) {
                case 0:
                    cell.detailTextLabel.text = UserPreferences.shared.theme.name;
                    break;
                case 1:
                    cell.detailTextLabel.text = UserPreferences.shared.fontFamilyUserFacingName;
                    cell.detailTextLabel.font = [UIFont fontWithName:UserPreferences.shared.fontFamily size:cell.detailTextLabel.font.pointSize];
                    break;
                case 2: {
                    UserPreferences *prefs = [UserPreferences shared];
                    UILabel *label = [cell viewWithTag:1];
                    UIStepper *stepper = [cell viewWithTag:2];
                    label.text = prefs.fontSize.stringValue;
                    stepper.value = prefs.fontSize.doubleValue;
                    cell.selectionStyle = UITableViewCellSelectionStyleNone;
                    break;
                }
            }
            break;
            
        case ColorSchemeSection:
            switch (indexPath.row) {
                case 0:
                    cell.textLabel.text = @"Match System";
                    break;
                case 1:
                    cell.textLabel.text = @"Light";
                    break;
                case 2:
                    cell.textLabel.text = @"Dark";
                    break;
            }
            cell.accessoryType = indexPath.row == UserPreferences.shared.colorScheme ? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
            break;
            
        case CursorSection:
        case StatusBarSection:
            cell.selectionStyle = UITableViewCellSelectionStyleNone;
            break;
    }
    
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    
    switch (indexPath.section) {
        case MainSection:
            switch (indexPath.row) {
                case 0: { // theme
                    ThemesViewController *themesViewController = [self.storyboard instantiateViewControllerWithIdentifier:@"Themes"];
                    [self.navigationController pushViewController:themesViewController animated:YES];
                    break;
                }
                case 1: // font family
                    [self selectFont:nil];
                    break;
            }
            break;
        case ColorSchemeSection:
            [UserPreferences.shared setColorScheme:indexPath.row];
    }
}

- (void)updateOtherControls {
    self.hideStatusBar.on = UserPreferences.shared.hideStatusBar;
    self.cursorStyle.selectedSegmentIndex = UserPreferences.shared.cursorStyle;
    self.blinkCursor.on = UserPreferences.shared.blinkCursor;
    [self setNeedsStatusBarAppearanceUpdate];
}

- (void)changePreviewTheme:(UISegmentedControl *)sender {
    _terminalView.overrideAppearance = sender.selectedSegmentIndex ? OverrideAppearanceDark : OverrideAppearanceLight;
    _terminalView.backgroundColor = [[UIColor alloc] ish_initWithHexString:(sender.selectedSegmentIndex ? UserPreferences.shared.theme.darkPalette : UserPreferences.shared.theme.lightPalette).backgroundColor];
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

- (IBAction)resetFont:(UIBarButtonItem *)sender API_AVAILABLE(ios(13)) {
    UserPreferences.shared.fontFamily = nil;
    [self.navigationController popToViewController:self animated:YES];
}

- (IBAction)fontSizeChanged:(UIStepper *)sender {
    UserPreferences.shared.fontSize = @((int) sender.value);
}

- (IBAction)hideStatusBarChanged:(UISwitch *)sender {
    UserPreferences.shared.hideStatusBar = sender.on;
    [self setNeedsStatusBarAppearanceUpdate];
}

- (IBAction)cursorStyleChanged:(UISegmentedControl *)sender {
    [UserPreferences.shared setCursorStyle:sender.selectedSegmentIndex];
}

- (IBAction)blinkCursorChanged:(UISwitch *)sender {
    [UserPreferences.shared setBlinkCursor:sender.on];
}
@end
