//
//  ThemeViewController.m
//  iSH
//
//  Created by Charlie Melbye on 11/12/18.
//

#import "ThemeViewController.h"
#import "UserPreferences.h"

static NSString *const PreviewCellIdentifier = @"PreviewCell";
static NSString *const ThemeNameCellIdentifier = @"ThemeNameCell";

@interface ThemeViewController ()

@end

@implementation ThemeViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    [self.tableView registerClass:[UITableViewCell class] forCellReuseIdentifier:PreviewCellIdentifier];
    [self.tableView registerClass:[UITableViewCell class] forCellReuseIdentifier:ThemeNameCellIdentifier];
    
    [[UserPreferences shared] addObserver:self forKeyPath:@"theme" options:NSKeyValueObservingOptionNew context:nil];
}

- (void)dealloc {
    @try {
        [[UserPreferences shared] removeObserver:self forKeyPath:@"theme"];
    } @catch (NSException * __unused exception) {}
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
    [self.tableView reloadData];
    [self setNeedsStatusBarAppearanceUpdate];
}

#pragma mark - Table view data source

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return 2;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    if (section == 0) {
        return Theme.presetNames.count;
    } else if (section == 1) {
        // row used to preview selected theme
        return 1;
    }
    
    assert("unhandled section");
    return 0;
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    if (section == 1) {
        return @"Preview";
    }
    
    return nil;
}

- (Theme *)_themeForRow:(NSUInteger)row {
    return [Theme presetThemeNamed:Theme.presetNames[row]];
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    // reuse preview cell separately to avoid reuse issues
    NSString *reuseIdentifier = indexPath.section == 1 ? PreviewCellIdentifier : ThemeNameCellIdentifier;
    UserPreferences *prefs = [UserPreferences shared];
    
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:reuseIdentifier forIndexPath:indexPath];
    
    if (indexPath.section == 0) {
        cell.textLabel.text = Theme.presetNames[indexPath.row];
        if ([prefs.theme isEqual:[self _themeForRow:indexPath.row]]) {
            cell.accessoryType = UITableViewCellAccessoryCheckmark;
        } else {
            cell.accessoryType = UITableViewCellAccessoryNone;
        }
    } else if (indexPath.section == 1) {
        cell.backgroundColor = prefs.theme.backgroundColor;
        cell.textLabel.textColor = prefs.theme.foregroundColor;
        cell.textLabel.font = [UIFont fontWithName:@"Menlo-Regular" size:prefs.fontSize.doubleValue];
        cell.textLabel.text = [NSString stringWithFormat:@"%@:~# ps aux", [UIDevice currentDevice].name];
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
    }
    
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    
    // selection only necessary for theme name list
    if (indexPath.section != 0) {
        return;
    }
    
    UserPreferences.shared.theme = [self _themeForRow:indexPath.row];
}

@end
